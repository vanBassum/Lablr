#include "PrintManager.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "RenderManager.h"
#include "StorageManager.h"
#include "UsbHostManager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <cstring>
#include <cstdio>

PrintManager::PrintManager(AppProvider& app)
    : app_(app)
{
}

void PrintManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    app_.getStrux().getCommandManager().Register(this, commands_);

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

// ──────────────────────────────────────────────────────────────
// ARGB8888S to one bit per dot
// ──────────────────────────────────────────────────────────────

uint32_t PrintManager::Rasterise(const uint32_t* pixels, uint32_t width, uint32_t height,
                                 uint8_t* job, uint32_t bytesPerLine, uint32_t offsetX,
                                 uint32_t threshold, bool invert)
{
    uint32_t black = 0;
    size_t   out   = 0;

    for (uint32_t y = 0; y < height; ++y)
    {
        job[out++] = SYN;
        uint8_t* line = job + out;
        memset(line, 0x00, bytesPerLine);          // paper
        out += bytesPerLine;

        const uint32_t* src = pixels + static_cast<size_t>(y) * width;
        for (uint32_t x = 0; x < width; ++x)
        {
            const uint32_t dot = offsetX + x;
            if (dot >= bytesPerLine * 8u) break;   // past the right edge of the head

            // ARGB8888S is one little-endian 32-bit word per pixel, so the bytes
            // are B,G,R,A and the word is 0xAARRGGBB however it is read here.
            const uint32_t p = src[x];
            const uint32_t a = (p >> 24) & 0xff;
            const uint32_t r = (p >> 16) & 0xff;
            const uint32_t g = (p >>  8) & 0xff;
            const uint32_t b =  p        & 0xff;

            // Transparent is paper, not black: an SVG that does not cover its
            // whole box would otherwise print a solid rectangle. Luminance is
            // the cheap integer approximation, which is all a 1-bit threshold
            // can justify.
            const uint32_t lum = (a == 0) ? 255 : (r * 77 + g * 151 + b * 28) >> 8;

            bool ink = lum < threshold;
            if (invert) ink = !ink;
            if (!ink) continue;

            // MSB is the leftmost dot in each byte.
            line[dot >> 3] |= static_cast<uint8_t>(0x80u >> (dot & 7u));
            ++black;
        }
    }

    return black;
}

// ──────────────────────────────────────────────────────────────
// The job
// ──────────────────────────────────────────────────────────────

const char* PrintManager::PrintSvg(const char* wirePath, uint32_t width, uint32_t height,
                                   uint32_t headDots, uint32_t offsetX, uint32_t threshold,
                                   bool invert, bool feed, JobStats& stats)
{
    auto& usb = app_.getUsbHostManager();
    if (!usb.IsReady()) return "no printer attached";
    if (!app_.getStorageManager().IsMounted()) return "not mounted";
    if (height == 0 || height > MAX_LINES) return "height must be 1 to 4000 dots";
    if (headDots == 0 || headDots > 4096) return "headWidth out of range";
    if (offsetX + width > headDots) return "label does not fit the head at that offset";

    char full[256];
    if (!StorageManager::Resolve(wirePath, full, sizeof(full))) return "bad path";

    // ── Render, through the one rasteriser ──
    const int64_t t0 = esp_timer_get_time();
    RenderManager::Bitmap bmp;
    // White background: the threshold below reads paper as white, and a
    // transparent canvas would make every uncovered dot ambiguous.
    if (const char* err = app_.getRenderManager().Render(full, width, height, 0xFFFFFFFFu, bmp))
        return err;

    const int64_t t1 = esp_timer_get_time();

    const uint32_t bytesPerLine = (headDots + 7u) / 8u;
    // ESC x100, ESC @, ESC L hi lo, ESC D n = 107 bytes, then the lines, then ESC E.
    const size_t headerMax = 100 + 2 + 4 + 3;
    const size_t jobSize   = headerMax + static_cast<size_t>(height) * (1 + bytesPerLine) + 2;

    // PSRAM: a full-length label is a couple of hundred kilobytes and the
    // internal heap is what the radios and the USB driver live in.
    uint8_t* job = static_cast<uint8_t*>(
        heap_caps_malloc(jobSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!job)
    {
        heap_caps_free(bmp.pixels);
        return "no PSRAM for the job";
    }

    size_t n = 0;
    // A printer that was interrupted mid-command is still waiting for its
    // operands; 100 ESCs are swallowed as no-ops by a printer in any state and
    // put it back at a command boundary.
    memset(job + n, ESC, 100); n += 100;
    job[n++] = ESC; job[n++] = 0x40;                                  // reset
    job[n++] = ESC; job[n++] = 0x4c;                                  // label length, in dots
    job[n++] = static_cast<uint8_t>((height >> 8) & 0xff);
    job[n++] = static_cast<uint8_t>(height & 0xff);
    job[n++] = ESC; job[n++] = 0x44;                                  // bytes per line
    job[n++] = static_cast<uint8_t>(bytesPerLine);

    const uint32_t black = Rasterise(bmp.pixels, width, height,
                                     job + n, bytesPerLine, offsetX, threshold, invert);
    n += static_cast<size_t>(height) * (1 + bytesPerLine);

    if (feed) { job[n++] = ESC; job[n++] = 0x45; }                    // form feed to the tear bar

    heap_caps_free(bmp.pixels);
    const int64_t t2 = esp_timer_get_time();

    ESP_LOGI(TAG, "job for %s: %ux%u at offset %u on a %u-dot head, "
                  "%u bytes/line, %u lines, %u bytes, %u black dots",
             wirePath, (unsigned)width, (unsigned)height, (unsigned)offsetX,
             (unsigned)headDots, (unsigned)bytesPerLine, (unsigned)height,
             (unsigned)n, (unsigned)black);

    // 30 s: a long label at the printer's own feed rate is slow, and a timeout
    // shorter than the paper takes is a truncated label.
    const int sent = usb.Send(job, n, 30000);
    const int64_t t3 = esp_timer_get_time();
    heap_caps_free(job);

    stats.headDots     = headDots;
    stats.bytesPerLine = bytesPerLine;
    stats.lines        = height;
    stats.jobBytes     = static_cast<uint32_t>(n);
    stats.blackDots    = black;
    stats.renderMs     = static_cast<uint32_t>((t1 - t0) / 1000);
    stats.convertMs    = static_cast<uint32_t>((t2 - t1) / 1000);
    stats.sendMs       = static_cast<uint32_t>((t3 - t2) / 1000);
    stats.psramUsed    = bmp.psramUsed;
    stats.internalUsed = bmp.internalUsed;
    stats.workerStack  = bmp.stackLeft;
    stats.scale        = bmp.scale;

    if (sent < 0) return "usb transfer failed";
    if (static_cast<size_t>(sent) != n) return "printer stopped accepting the job";
    return nullptr;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError PrintManager::Cmd_PrintSvg(CommandContext& ctx)
{
    char     path[192] = {};
    uint32_t width = 0, height = 0;
    uint32_t headWidth = HEAD_DOTS_DEFAULT;
    uint32_t offsetX   = 0;
    uint32_t threshold = THRESHOLD_DEFAULT;
    uint32_t invert    = 0;
    uint32_t feed      = 1;

    RETURN_IF_ERROR(ctx.readArgs(
        Required("path",      path,
                 "SVG to print, rooted at the label filesystem, e.g. "
                 "'/labels/test.svg'."),
        Required("width",     width,
                 "Label width in PRINTER DOTS. At 300 DPI, millimetres x 11.81."),
        Required("height",    height,
                 "Label length in PRINTER DOTS - how far the paper feeds. "
                 "1 to 4000."),
        Optional("headWidth", headWidth,
                 "Width of the print head in dots. Default 672, the 300 DPI "
                 "LabelWriter head. The raster is built at this width because "
                 "the head prints from its own left edge, not the label's."),
        Optional("offsetX",   offsetX,
                 "Where to place the label across the head, in dots from the "
                 "left. Default 0. This is how a narrow label is moved to where "
                 "the media actually sits."),
        Optional("threshold", threshold,
                 "Luminance below which a pixel becomes ink, 1 to 255. Default "
                 "128. Raise it to make thin anti-aliased text print heavier."),
        Optional("invert",    invert,
                 "1 to print the negative. Default 0."),
        Optional("feed",      feed,
                 "1 to advance the label to the tear bar when done, which is "
                 "what you want unless you are printing several in a row. "
                 "Default 1.")
    ));

    JobStats stats;
    const char* error = PrintSvg(path, width, height, headWidth, offsetX,
                                 threshold ? threshold : THRESHOLD_DEFAULT,
                                 invert != 0, feed != 0, stats);

    auto resp = ctx.reply.object();
    resp.field("ok", error == nullptr);
    if (error) resp.field("error", error);
    resp.field("path", path);
    resp.field("width", width);
    resp.field("height", height);
    resp.field("headWidth", stats.headDots);
    resp.field("bytesPerLine", stats.bytesPerLine);
    resp.field("jobBytes", stats.jobBytes);
    resp.field("blackDots", stats.blackDots);
    resp.field("scale", stats.scale);
    resp.field("renderMs", stats.renderMs);
    resp.field("convertMs", stats.convertMs);
    resp.field("sendMs", stats.sendMs);
    resp.field("psramUsed", stats.psramUsed);
    resp.field("internalUsed", stats.internalUsed);
    resp.field("workerStackLeft", stats.workerStack);
    // A job with no ink is the quiet failure this whole phase is watching for -
    // a missing font renders nothing and still prints a blank label happily.
    if (error == nullptr && stats.blackDots == 0)
        resp.field("warning",
                   "the label is entirely blank - check the SVG's font-family "
                   "against 'render fonts'");
    return RequestError::Ok;
}

RequestError PrintManager::Cmd_PrintTest(CommandContext& ctx)
{
    uint32_t headWidth = HEAD_DOTS_DEFAULT;
    uint32_t height    = 300;

    RETURN_IF_ERROR(ctx.readArgs(
        Optional("headWidth", headWidth,
                 "Width of the print head in dots. Default 672."),
        Optional("height",    height,
                 "How many dot rows to print. Default 300, about an inch at "
                 "300 DPI.")
    ));

    auto& usb = app_.getUsbHostManager();

    const char* error = nullptr;
    uint32_t bytesPerLine = 0;
    size_t   n = 0;

    if (!usb.IsReady())                              error = "no printer attached";
    else if (height == 0 || height > MAX_LINES)      error = "height must be 1 to 4000 dots";
    else if (headWidth == 0 || headWidth > 4096)     error = "headWidth out of range";

    uint8_t* job = nullptr;
    if (!error)
    {
        bytesPerLine = (headWidth + 7u) / 8u;
        const size_t jobSize = 109 + static_cast<size_t>(height) * (1 + bytesPerLine) + 2;
        job = static_cast<uint8_t*>(
            heap_caps_malloc(jobSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!job) error = "no PSRAM for the job";
    }

    if (!error)
    {
        memset(job + n, ESC, 100); n += 100;
        job[n++] = ESC; job[n++] = 0x40;
        job[n++] = ESC; job[n++] = 0x4c;
        job[n++] = static_cast<uint8_t>((height >> 8) & 0xff);
        job[n++] = static_cast<uint8_t>(height & 0xff);
        job[n++] = ESC; job[n++] = 0x44;
        job[n++] = static_cast<uint8_t>(bytesPerLine);

        // A frame with 16-dot stripes inside it. The frame proves the edges of
        // the head, the stripes prove line sync: if bytes-per-line were wrong
        // the stripes would shear instead of lying flat.
        for (uint32_t y = 0; y < height; ++y)
        {
            job[n++] = SYN;
            const bool border = (y < 8) || (y >= height - 8);
            const bool stripe = ((y / 16u) & 1u) == 0;
            for (uint32_t b = 0; b < bytesPerLine; ++b)
            {
                uint8_t v = (border || stripe) ? 0xff : 0x00;
                if (b == 0)                v |= 0x80;
                if (b == bytesPerLine - 1) v |= 0x01;
                job[n++] = v;
            }
        }
        job[n++] = ESC; job[n++] = 0x45;

        ESP_LOGI(TAG, "test pattern: %u dots wide, %u lines, %u bytes",
                 (unsigned)headWidth, (unsigned)height, (unsigned)n);

        const int64_t t0 = esp_timer_get_time();
        const int sent = usb.Send(job, n, 30000);
        const int64_t t1 = esp_timer_get_time();
        heap_caps_free(job);

        auto resp = ctx.reply.object();
        const bool ok = (sent >= 0 && static_cast<size_t>(sent) == n);
        resp.field("ok", ok);
        if (!ok) resp.field("error", sent < 0 ? "usb transfer failed"
                                              : "printer stopped accepting the job");
        resp.field("headWidth", headWidth);
        resp.field("bytesPerLine", bytesPerLine);
        resp.field("lines", height);
        resp.field("jobBytes", static_cast<uint32_t>(n));
        resp.field("sendMs", static_cast<uint32_t>((t1 - t0) / 1000));
        return RequestError::Ok;
    }

    auto resp = ctx.reply.object();
    resp.field("ok", false);
    resp.field("error", error);
    return RequestError::Ok;
}

RequestError PrintManager::Cmd_PrintStatus(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    auto& usb = app_.getUsbHostManager();

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    resp.field("ready", usb.IsReady());

    if (!usb.IsReady())
    {
        resp.field("note",
                   "no printer on the USB host port. It goes on the S3's native "
                   "USB pins with 5V supplied to VBUS from outside - the board "
                   "cannot source it.");
        return RequestError::Ok;
    }

    char vidpid[16];
    snprintf(vidpid, sizeof(vidpid), "%04x:%04x", usb.VendorId(), usb.ProductId());
    resp.field("id", vidpid);
    resp.field("product", usb.ProductName());

    char deviceId[256] = {};
    if (usb.GetDeviceId(deviceId, sizeof(deviceId)))
        resp.field("deviceId", deviceId);

    uint8_t portStatus = 0;
    if (usb.GetPortStatus(portStatus))
    {
        resp.field("paperEmpty", (portStatus & 0x20) != 0);
        resp.field("selected",   (portStatus & 0x10) != 0);
        resp.field("noError",    (portStatus & 0x08) != 0);
    }

    return RequestError::Ok;
}
