#include "PrintManager.h"
#include "Raster.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "RenderManager.h"
#include "StorageManager.h"
#include "MediaManager.h"
#include "PrinterManager/PrinterManager.h"
#include "DotGeometry.h"
#include "UsbHostManager.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include <climits>
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
    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    ESP_LOGI(TAG, "Initialized (%s: %lu DPI, %lu-dot head)",
             pr.id, (unsigned long)pr.dpi, (unsigned long)pr.headDots);
}

// ──────────────────────────────────────────────────────────────
// Geometry
// ──────────────────────────────────────────────────────────────

size_t PrintManager::WriteJobHeader(uint8_t* job, uint32_t lines, uint32_t bytesPerLine)
{
    size_t n = 0;
    // A printer interrupted mid-command is still waiting for its operands; 100
    // ESCs are swallowed as no-ops in any state and put it back at a boundary.
    memset(job + n, ESC, 100); n += 100;
    job[n++] = ESC; job[n++] = 0x40;                                  // reset
    job[n++] = ESC; job[n++] = 0x4c;                                  // label length, dots
    job[n++] = static_cast<uint8_t>((lines >> 8) & 0xff);
    job[n++] = static_cast<uint8_t>(lines & 0xff);
    job[n++] = ESC; job[n++] = 0x44;                                  // bytes per line
    job[n++] = static_cast<uint8_t>(bytesPerLine);
    return n;
}

uint32_t PrintManager::BytesPerLine(const Placement& p)
{
    return raster::BytesPerLine(p.offsetX, p.width, p.headDots, p.fullHead);
}

uint32_t PrintManager::RasteriseRow(const uint32_t* row, uint32_t width, uint8_t* line,
                                    uint32_t bytesPerLine, int32_t offsetX,
                                    uint32_t threshold, bool invert)
{
    return raster::RasteriseRow(row, width, line, bytesPerLine, offsetX, threshold, invert);
}

// ──────────────────────────────────────────────────────────────
// The job
// ──────────────────────────────────────────────────────────────

void PrintManager::Report(CommandContext* ctx, const char* phase,
                          uint32_t done, uint32_t total)
{
    if (!ctx) return;
    {
        auto rec = ctx->reply.object();
        rec.field("phase", phase);
        if (total) { rec.field("p", done); rec.field("total", total); }
    }   // closed before the flush, or the chunk carries half a record
    ctx->out.flush();
}

const char* PrintManager::SendJob(const uint32_t* pixels, const Placement& p, JobStats& stats,
                                  CommandContext* ctx)
{
    auto& usb = app_.getUsbHostManager();
    if (!usb.IsReady()) return "no printer attached";

    // Lines run from raster 0 to the design's last row, so the count is
    // height + offsetY for either sign of offsetY: a positive offset adds blank
    // lead-in, a negative one crops that many rows off the top.
    const int64_t total = static_cast<int64_t>(p.height) + p.offsetY;
    if (total <= 0) return "the design is entirely above the first raster line";
    if (total > static_cast<int64_t>(p.maxLines))
        return "too many raster lines - check the medium's height";

    const uint32_t lines        = static_cast<uint32_t>(total);
    const uint32_t bytesPerLine = BytesPerLine(p);

    const size_t jobSize = JobSize(lines, bytesPerLine);

    // PSRAM: a full-length label is a couple of hundred kilobytes, and the
    // internal heap is what the radios and the USB driver live in.
    uint8_t* job = static_cast<uint8_t*>(
        heap_caps_malloc(jobSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!job) return "no PSRAM for the job";

    size_t n = WriteJobHeader(job, lines, bytesPerLine);

    const int64_t t0 = esp_timer_get_time();
    uint32_t black = 0;
    for (uint32_t l = 0; l < lines; ++l)
    {
        job[n++] = SYN;
        uint8_t* line = job + n;
        memset(line, 0x00, bytesPerLine);      // paper
        n += bytesPerLine;

        // Which design row lands on this raster line. Outside the design is
        // blank, which covers both the lead-in and a short design.
        const int64_t row = static_cast<int64_t>(l) - p.offsetY;
        if (!pixels || row < 0 || row >= p.height) continue;

        black += RasteriseRow(pixels + static_cast<size_t>(row) * p.width, p.width,
                              line, bytesPerLine, p.offsetX, p.threshold, p.invert);
    }
    const int64_t t1 = esp_timer_get_time();

    if (p.feed) { job[n++] = ESC; job[n++] = 0x45; }   // form feed to the tear bar

    ESP_LOGI(TAG, "job: design %lux%lu at head (%ld,%ld), %lu bytes/line, "
                  "%lu lines, %u bytes, %lu ink dots",
             (unsigned long)p.width, (unsigned long)p.height,
             (long)p.offsetX, (long)p.offsetY,
             (unsigned long)bytesPerLine, (unsigned long)lines,
             (unsigned)n, (unsigned long)black);

    // 30 s: a long label at the printer's own feed rate is slow, and a timeout
    // shorter than the paper takes is a truncated label.
    //
    // The callback is capture-less on purpose, so it converts to a plain
    // function pointer and the transfer loop allocates nothing. Everything it
    // needs travels in `prog`.
    Progress prog{ ctx, n, 0 };
    Report(ctx, "send", 0, static_cast<uint32_t>(n));
    const int sent = usb.Send(job, n, 30000,
        [](void* v, size_t done) {
            auto* pr = static_cast<Progress*>(v);
            // Throttled, and the last chunk always reports: a bar that stops at
            // 97% because the tail was under the threshold looks like a hang.
            if (done - pr->last < REPORT_EVERY && done != pr->total) return;
            pr->last = done;
            Report(pr->ctx, "send", static_cast<uint32_t>(done),
                   static_cast<uint32_t>(pr->total));
        },
        &prog);
    const int64_t t2 = esp_timer_get_time();
    heap_caps_free(job);

    stats.bytesPerLine = bytesPerLine;
    stats.lines        = lines;
    stats.jobBytes     = static_cast<uint32_t>(n);
    stats.blackDots    = black;
    stats.convertMs    = static_cast<uint32_t>((t1 - t0) / 1000);
    stats.sendMs       = static_cast<uint32_t>((t2 - t1) / 1000);

    if (sent < 0) return "usb transfer failed";
    if (static_cast<size_t>(sent) != n) return "printer stopped accepting the job";
    return nullptr;
}

const char* PrintManager::PrintSvg(const char* wirePath, svg::Pattern pattern,
                                   const Placement& p, JobStats& stats,
                                   CommandContext* ctx)
{
    char full[288];
    if (pattern == svg::Pattern::None)
    {
        if (!app_.getStorageManager().IsMounted()) return "not mounted";
        if (!StorageManager::Resolve(wirePath, full, sizeof(full))) return "bad path";
    }

    // One job at a time on the wire, and one render behind it.
    LOCK(printLock_);

    // ThorVG reports nothing as it draws, so this is a phase marker rather than
    // a number: the UI shows an indeterminate bar until the send starts.
    Report(ctx, "render");

    const int64_t t0 = esp_timer_get_time();
    RenderManager::Bitmap bmp;
    const char* renderErr = nullptr;

    if (pattern == svg::Pattern::None)
    {
        // White background: the threshold reads paper as white, and a
        // transparent canvas would make every uncovered dot ambiguous.
        renderErr = app_.getRenderManager().Render(full, p.width, p.height,
                                                   0xFFFFFFFFu, bmp);
    }
    else
    {
        // A built-in design, generated at the job's own dot size and then put
        // through the very same renderer. Measure, allocate, fill - the SVG is
        // a few kilobytes of rectangles and it goes in PSRAM, not on the
        // command task's stack.
        char* text = nullptr;
        const size_t need = (pattern == svg::Pattern::Calibration)
                              ? svg::BuildCalibrationSvg((int)p.width, (int)p.height, nullptr, 0)
                              : svg::BuildTestSvg((int)p.width, (int)p.height, nullptr, 0);
        if (need == 0) return "cannot build that pattern at this size";

        text = static_cast<char*>(
            heap_caps_malloc(need + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!text) return "no PSRAM for the pattern";

        if (pattern == svg::Pattern::Calibration)
            svg::BuildCalibrationSvg((int)p.width, (int)p.height, text, need + 1);
        else
            svg::BuildTestSvg((int)p.width, (int)p.height, text, need + 1);

        renderErr = app_.getRenderManager().RenderText(text, need, p.width, p.height,
                                                       0xFFFFFFFFu, bmp);
        heap_caps_free(text);
    }

    if (renderErr) return renderErr;
    const int64_t t1 = esp_timer_get_time();

    stats.renderMs     = static_cast<uint32_t>((t1 - t0) / 1000);
    stats.psramUsed    = bmp.psramUsed;
    stats.internalUsed = bmp.internalUsed;
    stats.workerStack  = bmp.stackLeft;
    stats.scale        = bmp.scale;

    const char* err = SendJob(bmp.pixels, p, stats, ctx);
    heap_caps_free(bmp.pixels);
    return err;
}

const char* PrintManager::Resolve(const char* mediaId, Placement& p,
                                  bool haveWidth, bool haveHeight,
                                  bool haveOffsetX, bool haveOffsetY)
{
    // The machine first: its resolution converts every micrometre below, and
    // its dead zone is half of where the artwork lands.
    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    p.headDots = pr.headDots;
    p.maxLines = pr.maxLines;
    p.dpi      = pr.dpi;

    if (mediaId && mediaId[0])
    {
        MediaManager::Medium m;
        if (!app_.getMediaManager().Load(mediaId, m))
            return "no such medium - 'media list' says what there is";

        // One resolver for the whole model, shared with `media get` and the
        // preview, so none of them can disagree about where a label is:
        //
        //     placement = alignment - printerDead - mediumMargin
        //
        // and a medium still carrying the pre-split offsets has them honoured
        // verbatim instead, which is what keeps a calibrated roll where it is.
        dots::LabelGeometry g;
        dots::Resolve(m.spec(), pr.deadLeftUm, pr.deadTopUm, pr.dpi, g);

        // The medium supplies everything the caller did not override. Overrides
        // exist for calibration: finding an alignment means printing the same
        // design at several of them before one is worth storing.
        if (!haveWidth)   p.width   = static_cast<uint32_t>(g.widthDots);
        if (!haveHeight)  p.height  = static_cast<uint32_t>(g.heightDots);
        if (!haveOffsetX) p.offsetX = g.placeXDots;
        if (!haveOffsetY) p.offsetY = g.placeYDots;
    }
    else if (!haveWidth || !haveHeight)
    {
        return "name a medium with -media, or give -width and -height in dots";
    }

    if (p.width == 0 || p.height == 0) return "width and height must be non-zero";
    if (p.height > p.maxLines)         return "height is more than the printer will feed";
    if (p.offsetX >= static_cast<int32_t>(p.headDots))
        return "offsetX puts the label off the right of the head";
    if (p.threshold == 0 || p.threshold > 255) return "threshold must be 1 to 255";
    return nullptr;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError PrintManager::Cmd_PrintSvg(CommandContext& ctx)
{
    static constexpr int32_t  UNSET_I = INT32_MIN;
    static constexpr uint32_t UNSET_U = 0;

    char     path[192]    = {};
    char     media[32]    = {};
    char     patternName[24] = {};
    uint32_t width      = UNSET_U;
    uint32_t height     = UNSET_U;
    int32_t  offsetX    = UNSET_I;
    int32_t  offsetY    = UNSET_I;
    uint32_t threshold  = THRESHOLD_DEFAULT;
    bool     invert     = false;
    bool     feed       = true;
    bool     fullHead   = false;

    RETURN_IF_ERROR(ctx.readArgs(
        Optional("path",      path,
                 "SVG to print, rooted at the label filesystem, e.g. "
                 "'/labels/test.svg'."),
        Optional("media",     media,
                 "Which label stock this is going on, as 'media list' reports "
                 "it. Supplies the size AND the calibrated offsets, so nothing "
                 "else here is needed. Give this OR -width and -height."),
        Optional("width",     width,
                 "Design width in PRINTER DOTS, overriding the medium. At 300 "
                 "DPI one millimetre is 11.81 dots."),
        Optional("height",    height,
                 "Design height in PRINTER DOTS, overriding the medium."),
        Optional("offsetX",   offsetX,
                 "Head column the design's left edge goes to, overriding the "
                 "medium. For calibration; a settled value belongs in the "
                 "medium, not in every call."),
        Optional("offsetY",   offsetY,
                 "Raster line the design's top edge goes to, overriding the "
                 "medium. Negative crops that many rows off the top, which is "
                 "what a printer that starts late needs. For calibration."),
        Optional("threshold", threshold,
                 "Luminance below which a pixel becomes ink, 1 to 255. Default "
                 "128. Raise it to make thin anti-aliased text print heavier."),
        Optional("invert",    invert,
                 "true to print the negative. Default false."),
        Optional("feed",      feed,
                 "true to advance the label to the tear bar when done, which is "
                 "what you want unless printing several in a row. Default true."),
        Optional("fullHead",  fullHead,
                 "true to emit all 672 head columns instead of stopping at the "
                 "label's right edge. Same picture, a bigger job; for proving "
                 "that the narrowed raster prints identically."),
        Optional("pattern",   patternName,
                 "Draw a BUILT-IN design instead of a stored file, and leave -path out: "
                 "'calibration' is the centre-origin alignment grid, 'test' is "
                 "the bars and grey sweep for telling a printer fault from a "
                 "label fault. A pattern is generated at the medium's own dot "
                 "size and then goes through exactly the same renderer, fit and "
                 "placement as a label, so what you preview is what prints.")
    ));

    svg::Pattern pattern = svg::Pattern::None;
    if (!svg::ParsePattern(patternName, pattern))
    {
        auto bad = ctx.reply.object();
        bad.field("ok", false);
        bad.field("error", "pattern must be 'calibration' or 'test'");
        return RequestError::Ok;
    }
    if (pattern == svg::Pattern::None && !path[0])
    {
        auto bad = ctx.reply.object();
        bad.field("ok", false);
        bad.field("error", "give -path, or -pattern calibration|test");
        return RequestError::Ok;
    }

    Placement p;
    p.threshold = threshold;
    p.invert    = invert;
    p.feed      = feed;
    p.fullHead  = fullHead;
    if (width  != UNSET_U) p.width   = width;
    if (height != UNSET_U) p.height  = height;
    if (offsetX != UNSET_I) p.offsetX = offsetX;
    if (offsetY != UNSET_I) p.offsetY = offsetY;

    JobStats stats;
    const char* error = Resolve(media, p,
                                width != UNSET_U, height != UNSET_U,
                                offsetX != UNSET_I, offsetY != UNSET_I);
    if (!error) error = PrintSvg(path, pattern, p, stats, &ctx);

    auto resp = ctx.reply.object();
    resp.field("ok", error == nullptr);
    if (error) resp.field("error", error);
    if (pattern == svg::Pattern::None) resp.field("path", path);
    else resp.field("pattern", patternName);
    if (media[0]) resp.field("media", media);
    resp.field("width", p.width);
    resp.field("height", p.height);
    resp.field("offsetX", p.offsetX);
    resp.field("offsetY", p.offsetY);
    resp.field("headDots", p.headDots);
    resp.field("bytesPerLine", stats.bytesPerLine);
    resp.field("lines", stats.lines);
    resp.field("jobBytes", stats.jobBytes);
    resp.field("blackDots", stats.blackDots);
    resp.field("scale", stats.scale);
    resp.field("renderMs", stats.renderMs);
    resp.field("convertMs", stats.convertMs);
    resp.field("sendMs", stats.sendMs);
    resp.field("psramUsed", stats.psramUsed);
    resp.field("internalUsed", stats.internalUsed);
    resp.field("workerStackLeft", stats.workerStack);
    // A job with no ink is the quiet failure worth naming: a missing font
    // renders nothing and still prints a happy, empty label.
    if (error == nullptr && stats.blackDots == 0)
        resp.field("warning",
                   "the label is entirely blank - check the SVG's font-family "
                   "against 'render fonts'");
    return RequestError::Ok;
}

RequestError PrintManager::Cmd_PrintTest(CommandContext& ctx)
{
    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    uint32_t height = 300;
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("height", height,
                 "How many dot rows to print. Default 300, about an inch at "
                 "300 DPI.")));

    auto& usb = app_.getUsbHostManager();

    const char* error = nullptr;
    uint32_t bytesPerLine = (pr.headDots + 7u) / 8u;
    size_t   n = 0;
    uint8_t* job = nullptr;
    int      sent = -1;
    int64_t  t0 = 0, t1 = 0;

    if (!usb.IsReady())                          error = "no printer attached";
    else if (height == 0 || height > pr.maxLines)  error = "height must be 1 to 4000 dots";

    if (!error)
    {
        const size_t jobSize = JobSize(height, bytesPerLine);
        job = static_cast<uint8_t*>(
            heap_caps_malloc(jobSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!job) error = "no PSRAM for the job";
    }

    if (!error)
    {
        LOCK(printLock_);

        n = WriteJobHeader(job, height, bytesPerLine);

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

        t0 = esp_timer_get_time();
        sent = usb.Send(job, n, 30000);
        t1 = esp_timer_get_time();
    }
    if (job) heap_caps_free(job);

    auto resp = ctx.reply.object();
    const bool ok = !error && sent >= 0 && static_cast<size_t>(sent) == n;
    resp.field("ok", ok);
    if (error)     resp.field("error", error);
    else if (!ok)  resp.field("error", sent < 0 ? "usb transfer failed"
                                                : "printer stopped accepting the job");
    resp.field("headDots", pr.headDots);
    resp.field("bytesPerLine", bytesPerLine);
    resp.field("lines", height);
    resp.field("jobBytes", static_cast<uint32_t>(n));
    resp.field("sendMs", static_cast<uint32_t>((t1 - t0) / 1000));
    return RequestError::Ok;
}

RequestError PrintManager::Cmd_PrintCalibrate(CommandContext& ctx)
{
    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    uint32_t height = 400;
    uint32_t minor  = 25;
    uint32_t major  = 100;

    RETURN_IF_ERROR(ctx.readArgs(
        Optional("height", height,
                 "How many raster lines to print. Default 400 - about 34 mm, "
                 "deliberately longer than a small label so the paper's own "
                 "edge is visible against the grid."),
        Optional("minor",  minor,
                 "Spacing of the thin rules, in dots. Default 25, which is "
                 "2.12 mm at 300 DPI."),
        Optional("major",  major,
                 "Spacing of the heavy rules, in dots. Default 100, 8.47 mm.")));

    auto& usb = app_.getUsbHostManager();

    const char* error = nullptr;
    const uint32_t bytesPerLine = (pr.headDots + 7u) / 8u;
    size_t   n = 0;
    uint8_t* job = nullptr;
    int      sent = -1;

    if (!usb.IsReady())                         error = "no printer attached";
    else if (height == 0 || height > pr.maxLines) error = "height must be 1 to 4000 dots";
    else if (minor == 0 || major == 0)          error = "minor and major must be non-zero";

    if (!error)
    {
        const size_t jobSize = JobSize(height, bytesPerLine);
        job = static_cast<uint8_t*>(
            heap_caps_malloc(jobSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!job) error = "no PSRAM for the job";
    }

    if (!error)
    {
        LOCK(printLock_);

        n = WriteJobHeader(job, height, bytesPerLine);

        // A grid in HEAD coordinates, so whatever lands on the paper says where
        // the paper is. The axes at column 0 and line 0 are solid and 4 wide, so
        // the origin is unmistakable even if the rest is faint - and if neither
        // is on the label, that is itself the measurement.
        for (uint32_t y = 0; y < height; ++y)
        {
            job[n++] = SYN;
            uint8_t* line = job + n;
            memset(line, 0x00, bytesPerLine);
            n += bytesPerLine;

            const bool axisY  = (y < 4);                       // raster line 0
            const bool ruleY  = (y % minor) == 0;
            const bool heavyY = (y % major) < 3;

            if (axisY || heavyY || ruleY)
            {
                // A full-width rule.
                memset(line, 0xff, bytesPerLine);
                continue;
            }

            for (uint32_t x = 0; x < pr.headDots; ++x)
            {
                const bool axisX  = (x < 4);                   // head column 0
                const bool ruleX  = (x % minor) == 0;
                const bool heavyX = (x % major) < 3;
                if (!(axisX || ruleX || heavyX)) continue;
                line[x >> 3] |= static_cast<uint8_t>(0x80u >> (x & 7u));
            }
        }
        job[n++] = ESC; job[n++] = 0x45;

        sent = usb.Send(job, n, 30000);
    }
    if (job) heap_caps_free(job);

    auto resp = ctx.reply.object();
    const bool ok = !error && sent >= 0 && static_cast<size_t>(sent) == n;
    resp.field("ok", ok);
    if (error)     resp.field("error", error);
    else if (!ok)  resp.field("error", sent < 0 ? "usb transfer failed"
                                                : "printer stopped accepting the job");
    resp.field("headDots", pr.headDots);
    resp.field("lines", height);
    resp.field("minorDots", minor);
    resp.field("majorDots", major);
    resp.field("minorUm", static_cast<uint32_t>(minor * 25400u / DPI));
    resp.field("majorUm", static_cast<uint32_t>(major * 25400u / DPI));
    resp.field("jobBytes", static_cast<uint32_t>(n));
    return RequestError::Ok;
}

RequestError PrintManager::Cmd_PrintStatus(CommandContext& ctx)
{
    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    RETURN_IF_ERROR(ctx.readArgs());

    auto& usb = app_.getUsbHostManager();

    // One copy of what is attached, taken once, so every field below describes
    // the same device even if it is unplugged halfway through this reply.
    UsbHostManager::Attached dev;
    const bool attached = usb.Snapshot(dev);

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    resp.field("ready", attached);
    resp.field("dpi", DPI);
    resp.field("headDots", pr.headDots);

    if (!attached)
    {
        resp.field("note",
                   "no printer on the USB host port. It goes on the S3's native "
                   "USB pins with 5V supplied to VBUS from outside - the board "
                   "cannot source it.");
        return RequestError::Ok;
    }

    char vidpid[16];
    snprintf(vidpid, sizeof(vidpid), "%04x:%04x", dev.vid, dev.pid);
    resp.field("id", vidpid);
    resp.field("product", dev.product);

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
