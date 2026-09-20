#include "RenderManager.h"
#include "StorageManager/StorageManager.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "ReplyBody.h"
#include "PngStream.h"
#include "XmlEntities.h"
#include "SvgFontAttrs.h"
#include "SvgQrCode.h"
#include "EspQrEncoder.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_pthread.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <sys/stat.h>
#include <dirent.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

// ThorVG's C header declares a const-qualified scalar return, which is exactly
// what -Wignored-qualifiers is for. The component suppresses it for C only
// (COMPILE_LANGUAGE:C in its CMakeLists), and this is a C++ translation unit,
// so it is silenced here rather than by weakening the flag for our whole
// component - the warning is one we want on our own code.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wignored-qualifiers"
#include <thorvg_capi.h>
#pragma GCC diagnostic pop

RenderManager::RenderManager(AppProvider& app)
    : app_(app)
{
}

void RenderManager::Init()
{
    auto initAttempt = initState_.TryBeginInit();
    if (!initAttempt)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    // pthread_create, not a FreeRTOS task: ThorVG asks for the calling thread's
    // identity, and on ESP-IDF that assert-fails unless pthread created it.
    esp_pthread_cfg_t cfg = esp_pthread_get_default_config();
    cfg.stack_size   = WORKER_STACK;
    cfg.prio         = 5;
    cfg.thread_name  = "render";
    esp_pthread_set_cfg(&cfg);

    if (pthread_create(&worker_, nullptr, &RenderManager::WorkerEntry, this) != 0)
    {
        ESP_LOGE(TAG, "Could not start render worker - rendering is unavailable");
    }
    else
    {
        workerUp_ = true;
        // Wait for the engine and the fonts before declaring this manager up, so
        // that a `render fonts` immediately after boot reports the truth.
        ready_.Take();
    }

    app_.getStrux().getCommandManager().Register(this, commands_);

    initAttempt.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

void* RenderManager::WorkerEntry(void* self)
{
    static_cast<RenderManager*>(self)->WorkerLoop();
    return nullptr;
}

void RenderManager::WorkerLoop()
{
    // Everything ThorVG happens on this thread, including these two.
    if (tvg_engine_init(0) != TVG_RESULT_SUCCESS)
    {
        ESP_LOGE(TAG, "tvg_engine_init failed - rendering is unavailable");
    }
    else
    {
        engineUp_ = true;
        uint32_t major = 0, minor = 0, micro = 0;
        const char* version = nullptr;
        tvg_engine_version(&major, &minor, &micro, &version);
        ESP_LOGI(TAG, "ThorVG %s", version ? version : "?");
        LoadFonts();
    }

    ready_.Give();

    for (;;)
    {
        jobRequest_.Take();
        RunJob();
        jobDone_.Give();
    }
}

// ──────────────────────────────────────────────────────────────
// Fonts
// ──────────────────────────────────────────────────────────────

uint8_t* RenderManager::ReadFileToPsram(const char* fullPath, size_t& sizeOut)
{
    sizeOut = 0;

    struct stat st;
    if (stat(fullPath, &st) != 0 || st.st_size <= 0) return nullptr;

    const size_t size = static_cast<size_t>(st.st_size);

    // PSRAM explicitly, not malloc: this is the allocation class that must not
    // compete with the WiFi stack and the TLS buffers for internal RAM.
    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!buf)
    {
        ESP_LOGE(TAG, "No PSRAM for %u bytes (%s)", (unsigned)size, fullPath);
        return nullptr;
    }

    FILE* f = fopen(fullPath, "rb");
    if (!f) { heap_caps_free(buf); return nullptr; }

    size_t got = 0;
    while (got < size)
    {
        size_t n = fread(buf + got, 1, size - got, f);
        if (n == 0) break;
        got += n;
    }
    fclose(f);

    if (got != size) { heap_caps_free(buf); return nullptr; }

    sizeOut = size;
    return buf;
}

bool RenderManager::LoadFont(const char* dirPath, const char* fileName)
{
    if (fontCount_ >= MAX_FONTS)
    {
        ESP_LOGW(TAG, "Font table full, ignoring %s", fileName);
        return false;
    }

    // The registered name is the filename without its extension - the rule an
    // SVG's font-family has to follow. Derived here rather than by ThorVG,
    // because this takes the memory path where the name is ours to choose.
    char name[32];
    snprintf(name, sizeof(name), "%s", fileName);
    if (char* dot = strrchr(name, '.')) *dot = '\0';
    if (name[0] == '\0') return false;

    char full[320];
    snprintf(full, sizeof(full), "%s/%s", dirPath, fileName);

    size_t size = 0;
    uint8_t* data = ReadFileToPsram(full, size);
    if (!data) { ESP_LOGE(TAG, "Could not read font %s", fileName); return false; }

    // copy=false: ThorVG keeps this pointer, so the block is never freed. That
    // is the intent - a font is loaded once and used by every render.
    const Tvg_Result r = tvg_font_load_data(name, reinterpret_cast<const char*>(data),
                                            static_cast<uint32_t>(size), "ttf", false);
    if (r != TVG_RESULT_SUCCESS)
    {
        // TVG_RESULT_NOT_SUPPORTED here means the build has no sfnt loader.
        ESP_LOGE(TAG, "ThorVG refused font '%s' (result %d)", name, (int)r);
        heap_caps_free(data);
        return false;
    }

    LoadedFont& slot = fonts_[fontCount_++];
    snprintf(slot.name, sizeof(slot.name), "%s", name);
    slot.size = static_cast<uint32_t>(size);

    ESP_LOGI(TAG, "Font '%s' registered (%u bytes in PSRAM)", name, (unsigned)size);
    return true;
}

void RenderManager::LoadFonts()
{
    if (!app_.getStorageManager().IsMounted())
    {
        ESP_LOGW(TAG, "Storage not mounted - no fonts loaded");
        return;
    }

    char dirPath[64];
    snprintf(dirPath, sizeof(dirPath), "%s/fonts", StorageManager::BASE_PATH);

    DIR* dir = opendir(dirPath);
    if (!dir)
    {
        ESP_LOGW(TAG, "No /fonts directory");
        return;
    }

    struct dirent* e;
    while ((e = readdir(dir)) != nullptr)
    {
        if (e->d_type == DT_DIR) continue;
        const char* dot = strrchr(e->d_name, '.');
        if (!dot) continue;
        if (strcasecmp(dot, ".ttf") != 0 && strcasecmp(dot, ".otf") != 0) continue;
        LoadFont(dirPath, e->d_name);
    }
    closedir(dir);

    if (fontCount_ == 0)
        ESP_LOGW(TAG, "No fonts registered - SVG <text> will render nothing");
}

// ──────────────────────────────────────────────────────────────
// The render itself, on the worker thread
// ──────────────────────────────────────────────────────────────

// A QR failure is nearly always the caller's to fix, and the caller may be a
// model that cannot see this file - so the message carries the numbers and the
// remedy, not just the fault.
void RenderManager::DescribeQrFailure(const svg::QrDiagnostic& diag)
{
    // The payload can be up to 512 bytes; a message quoting all of it would
    // bury the part that says what to do.
    char shown[49];
    const size_t n = diag.payload.n < sizeof(shown) - 1 ? diag.payload.n : sizeof(shown) - 1;
    if (diag.payload.p) memcpy(shown, diag.payload.p, n);
    shown[diag.payload.p ? n : 0] = '\0';

    switch (diag.status)
    {
        case svg::QrStatus::MissingGeometry:
            snprintf(job_.errorBuf, sizeof(job_.errorBuf),
                     "qr: rect with data-qr=\"%s\" needs a positive width and height",
                     shown);
            break;

        case svg::QrStatus::BadEcc:
            snprintf(job_.errorBuf, sizeof(job_.errorBuf),
                     "qr: data-qr-ecc must be L, M, Q or H");
            break;

        case svg::QrStatus::PayloadTooLong:
            snprintf(job_.errorBuf, sizeof(job_.errorBuf),
                     "qr: data-qr payload is %u bytes, the limit is %u",
                     (unsigned)diag.payload.n, (unsigned)svg::QR_MAX_PAYLOAD);
            break;

        case svg::QrStatus::EncodeFailed:
            snprintf(job_.errorBuf, sizeof(job_.errorBuf),
                     "qr: cannot encode \"%s\" - shorten it or lower data-qr-ecc",
                     shown);
            break;

        case svg::QrStatus::BoxTooSmall:
            snprintf(job_.errorBuf, sizeof(job_.errorBuf),
                     "qr: box is %d dots but \"%s\" needs %d (%d modules + %d quiet "
                     "at %d dots each) - enlarge the rect, shorten the payload, or "
                     "lower data-qr-ecc",
                     diag.boxDots, shown, diag.needDots, diag.modules,
                     2 * svg::QR_QUIET_MODULES, svg::QR_MIN_MODULE_DOTS);
            break;

        case svg::QrStatus::Ok:
            snprintf(job_.errorBuf, sizeof(job_.errorBuf), "qr: no failure");
            break;
    }

    ESP_LOGW(TAG, "%s", job_.errorBuf);
}

void RenderManager::RunJob()
{
    job_.pixels = nullptr;
    job_.error  = nullptr;
    job_.scale  = 1.0f;

    if (!engineUp_) { job_.error = "render engine unavailable"; return; }

    const size_t freeInternalBefore = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t freePsramBefore    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    size_t svgSize = 0;
    uint8_t* svg = ReadFileToPsram(job_.path, svgSize);
    if (!svg) { job_.error = "cannot read svg"; return; }

    // ThorVG's SVG loader appends a text node's bytes verbatim and resolves no
    // character reference, so a label that correctly writes `AT&amp;T` prints
    // the `&amp;`. Resolve them before the parser sees the buffer - in place
    // and shrinking, so there is no second allocation and nothing to free.
    // See lib/common/XmlEntities.h for what it deliberately leaves alone.
    svgSize = xml::DecodeCharData(reinterpret_cast<char*>(svg), svgSize);

    // The same loader inherits no font property, so `<g font-size="42">` around
    // a <text> draws at ThorVG's default 10 and the label comes out with its
    // list a quarter of the size the SVG asked for. Write the inherited value
    // onto the element, where the loader does read it. This one GROWS, so it
    // measures first and only allocates when there is something to add - an SVG
    // that already sizes every <text> costs one extra pass and no memory.
    // See lib/common/SvgFontAttrs.h.
    const size_t inherited = svg::PushDownFontAttrs(
        reinterpret_cast<const char*>(svg), svgSize, nullptr, 0);
    if (inherited > svgSize)
    {
        uint8_t* grown = static_cast<uint8_t*>(
            heap_caps_malloc(inherited, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (grown)
        {
            svg::PushDownFontAttrs(reinterpret_cast<const char*>(svg), svgSize,
                                   reinterpret_cast<char*>(grown), inherited);
            heap_caps_free(svg);
            svg     = grown;
            svgSize = inherited;
        }
        else
        {
            // A label drawn at the wrong size beats no label at all, and the
            // render below is about to report its own memory trouble anyway.
            ESP_LOGW(TAG, "No PSRAM to apply inherited font attributes (%u bytes)",
                     (unsigned)inherited);
        }
    }

    // A QR code is an error-correcting code, not a drawing, so nothing upstream
    // of the device can be trusted to produce one: a label declares the PAYLOAD
    // on a <rect data-qr="...">, and this expands it into a white field and a
    // path of modules that ThorVG needs to know nothing about. It GROWS like
    // the pass above, and the encoder is deterministic, so measuring and then
    // filling produce the same bytes. See lib/common/SvgQrCode.h.
    {
        EspQrEncoder    encoder;
        svg::QrDiagnostic diag;

        const size_t expanded = svg::ExpandQrCodes(
            reinterpret_cast<const char*>(svg), svgSize, nullptr, 0, encoder, diag);

        if (diag.status != svg::QrStatus::Ok)
        {
            DescribeQrFailure(diag);
            heap_caps_free(svg);
            job_.error = job_.errorBuf;
            return;
        }

        if (expanded > svgSize)
        {
            uint8_t* grown = static_cast<uint8_t*>(
                heap_caps_malloc(expanded, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!grown)
            {
                // Unlike the font pass, carrying on is not an option: the
                // placeholder would reach ThorVG as a plain rect and print a
                // black square where a scannable code was asked for.
                heap_caps_free(svg);
                snprintf(job_.errorBuf, sizeof(job_.errorBuf),
                         "no PSRAM to expand QR codes (%u bytes)", (unsigned)expanded);
                job_.error = job_.errorBuf;
                return;
            }

            svg::ExpandQrCodes(reinterpret_cast<const char*>(svg), svgSize,
                               reinterpret_cast<char*>(grown), expanded, encoder, diag);
            heap_caps_free(svg);
            svg     = grown;
            svgSize = expanded;
        }
    }

    const size_t pixelBytes = static_cast<size_t>(job_.width) * job_.height * 4u;
    uint32_t* pixels = static_cast<uint32_t*>(
        heap_caps_malloc(pixelBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!pixels)
    {
        heap_caps_free(svg);
        job_.error = "no PSRAM for canvas";
        return;
    }

    // Pre-fill rather than asking draw() to clear: draw(clear=true) clears to
    // transparent, and a label wants paper underneath it.
    for (size_t i = 0; i < pixelBytes / 4; ++i) pixels[i] = job_.background;

    Tvg_Canvas canvas = tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT);
    Tvg_Paint  picture = nullptr;
    const char* failure = nullptr;

    if (!canvas) failure = "canvas creation failed";

    if (!failure &&
        tvg_swcanvas_set_target(canvas, pixels, job_.width, job_.width, job_.height,
                                TVG_COLORSPACE_ARGB8888S) != TVG_RESULT_SUCCESS)
        failure = "canvas target rejected";

    if (!failure)
    {
        picture = tvg_picture_new();
        if (!picture) failure = "picture creation failed";
    }

    if (!failure &&
        tvg_picture_load_data(picture, reinterpret_cast<const char*>(svg),
                              static_cast<uint32_t>(svgSize), "svg", nullptr,
                              false) != TVG_RESULT_SUCCESS)
        failure = "svg failed to parse";

    if (!failure)
    {
        // Fit inside the requested box, preserving aspect, centred. Stretching
        // would make a wrong-shaped SVG look right in the preview, which is the
        // opposite of what a preview is for.
        float nw = 0.0f, nh = 0.0f;
        tvg_picture_get_size(picture, &nw, &nh);
        if (nw > 0.0f && nh > 0.0f)
        {
            const float sx = static_cast<float>(job_.width) / nw;
            const float sy = static_cast<float>(job_.height) / nh;
            job_.scale = (sx < sy) ? sx : sy;
            tvg_picture_set_size(picture, nw * job_.scale, nh * job_.scale);
            tvg_paint_translate(picture,
                                (static_cast<float>(job_.width)  - nw * job_.scale) * 0.5f,
                                (static_cast<float>(job_.height) - nh * job_.scale) * 0.5f);
        }
    }

    if (!failure && tvg_canvas_add(canvas, picture) != TVG_RESULT_SUCCESS)
        failure = "canvas add failed";

    if (!failure)
    {
        // clear=false: the background is already in the buffer.
        if (tvg_canvas_draw(canvas, false) != TVG_RESULT_SUCCESS) failure = "draw failed";
        else if (tvg_canvas_sync(canvas) != TVG_RESULT_SUCCESS)   failure = "sync failed";
    }

    if (canvas) tvg_canvas_destroy(canvas);      // takes the picture with it
    heap_caps_free(svg);

    const size_t freeInternalAfter = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t freePsramAfter    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    job_.stackLeft    = static_cast<uint32_t>(uxTaskGetStackHighWaterMark(nullptr));
    job_.psramUsed    = static_cast<uint32_t>(
        freePsramBefore > freePsramAfter ? freePsramBefore - freePsramAfter : 0);
    job_.internalUsed = static_cast<uint32_t>(
        freeInternalBefore > freeInternalAfter ? freeInternalBefore - freeInternalAfter : 0);

    // DEBUG, not INFO: a render is routine and the browser asks for many of them
    // - one per label thumbnail - so at INFO this one line buried every other
    // message in the console. The numbers are bring-up numbers (PSRAM held,
    // stack headroom) and are still one log level away when they matter again.
    // A render that FAILS still speaks up, below.
    ESP_LOGD(TAG, "render %s %ux%u: psram peak-held %u, internal %u, worker stack left %u",
             job_.path, (unsigned)job_.width, (unsigned)job_.height,
             (unsigned)job_.psramUsed, (unsigned)job_.internalUsed,
             (unsigned)job_.stackLeft);

    if (failure)
    {
        // The counterpart to the demotion above: with the routine line at DEBUG,
        // this is the only thing a render leaves in the console, so a label that
        // will not draw still says so where somebody is looking.
        ESP_LOGE(TAG, "render %s %ux%u failed: %s",
                 job_.path, (unsigned)job_.width, (unsigned)job_.height, failure);
        heap_caps_free(pixels);
        job_.error = failure;
        return;
    }

    job_.pixels = pixels;
}

// ──────────────────────────────────────────────────────────────
// The public entry point: one rasteriser, two callers
// ──────────────────────────────────────────────────────────────

const char* RenderManager::Render(const char* vfsPath, uint32_t width, uint32_t height,
                                  uint32_t background, Bitmap& out)
{
    out = Bitmap{};

    if (!workerUp_ || !engineUp_)                             return "render engine unavailable";
    if (width == 0 || height == 0)                            return "width and height must be non-zero";
    if (width > MAX_DIMENSION || height > MAX_DIMENSION)      return "width or height too large";
    if (static_cast<uint64_t>(width) * height > MAX_PIXELS)   return "too many pixels";

    // One render at a time. The wait is the queue.
    LOCK(renderLock_);

    snprintf(job_.path, sizeof(job_.path), "%s", vfsPath);
    job_.width      = width;
    job_.height     = height;
    job_.background = background;

    jobRequest_.Give();
    jobDone_.Take();

    if (job_.error || !job_.pixels)
        return job_.error ? job_.error : "render produced nothing";

    out.pixels       = job_.pixels;
    out.width        = width;
    out.height       = height;
    out.scale        = job_.scale;
    out.psramUsed    = job_.psramUsed;
    out.internalUsed = job_.internalUsed;
    out.stackLeft    = job_.stackLeft;

    // The handler owns the buffer now; job_ must not free or reuse it.
    job_.pixels = nullptr;
    return nullptr;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError RenderManager::Cmd_Fonts(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    {
        auto arr = resp.array("fonts");
        for (size_t i = 0; i < fontCount_; ++i)
        {
            auto item = arr.object();
            item.field("name", fonts_[i].name);
            item.field("bytes", fonts_[i].size);
        }
    }
    return RequestError::Ok;
}

RequestError RenderManager::Cmd_RenderSvg(CommandContext& ctx)
{
    char     path[192] = {};
    uint32_t width = 0, height = 0;
    uint32_t background = 0xFFFFFFFFu;
    char     format[8] = "raw";

    RETURN_IF_ERROR(ctx.readArgs(
        Required("path",   path,
                 "SVG to render, rooted at the label filesystem, e.g. "
                 "'/labels/test.svg'."),
        Required("width",  width,
                 "Output width in pixels, 1 to 2000. To preview what a print "
                 "will look like, pass the medium's own widthDots from "
                 "'media list'."),
        Required("height", height,
                 "Output height in pixels, 1 to 2000. The medium's heightDots "
                 "for a print preview. The SVG is fitted to this box with its "
                 "aspect ratio preserved, so a box of another shape previews "
                 "the design but not the label."),
        Optional("background", background,
                 "Background colour as 0xAARRGGBB. Defaults to 0xFFFFFFFF, "
                 "opaque white, because a label is printed on white paper. "
                 "Pass 0 for a transparent background."),
        Optional("format", format,
                 "How to encode the pixels: 'raw' (the default) for ARGB8888S, "
                 "which is what a canvas and the printer want, or 'png' for an "
                 "ordinary PNG file. Ask for 'png' if you need to LOOK at the "
                 "result - it is a quarter the size and anything can display "
                 "it, while raw is only useful to something that knows the "
                 "pixel layout.")
    ));

    // Everything that can be refused before the worker is woken, is.
    const char* error = nullptr;
    char full[256];
    if (!app_.getStorageManager().IsMounted())                    error = "not mounted";
    else if (!StorageManager::Resolve(path, full, sizeof(full)))  error = "bad path";
    else if (strcmp(format, "raw") != 0 && strcmp(format, "png") != 0)
        error = "format must be 'raw' or 'png'";

    // Rendering itself is Render(), shared with the printer - a preview that
    // came from a different rasteriser would stop predicting a print.
    Bitmap bmp;
    if (!error) error = Render(full, width, height, background, bmp);

    if (error)
    {
        {
            auto head = ctx.reply.object();
            head.field("ok", false);
            head.field("error", error);
        }
        ctx.out.write("\n", 1);
        return RequestError::Ok;
    }

    const bool png = strcmp(format, "png") == 0;
    const size_t pixelBytes = static_cast<size_t>(width) * height * 4u;

    // ── The reply: header record, newline, the image ─────────
    {
        auto head = ctx.reply.object();
        head.field("ok", true);
        head.field("path", path);
        head.field("width", width);
        head.field("height", height);
        // Raw is little-endian 32-bit words, so the bytes on the wire are
        // B,G,R,A. Un-premultiplied, which is what makes the result inspectable
        // on a PC without undoing anything first.
        head.field("format", png ? "png" : "ARGB8888S");
        if (!png)
        {
            head.field("stride", width * 4u);
            head.field("bytes", static_cast<uint32_t>(pixelBytes));
        }
        head.field("scale", bmp.scale);
        head.field("psramUsed", bmp.psramUsed);
        head.field("internalUsed", bmp.internalUsed);
        head.field("workerStackLeft", bmp.stackLeft);

        // What follows the newline, in terms something that never heard of this
        // command can act on - a PNG is shown, raw pixels are bytes. See
        // lib/protocol/ReplyBody.h. A PNG's length is not known until it has
        // been encoded, because it streams, so only the raw form declares one.
        if (png) protocol::declareBody(head, "image/png");
        else     protocol::declareBody(head, "application/octet-stream",
                                       static_cast<uint32_t>(pixelBytes));
    }
    protocol::endHeader(ctx.out);

    if (png)
    {
        // Encodes and streams in one pass; it holds one scanline, never the
        // file. A failure here cannot un-send the header, so it is logged by
        // the encoder and the reply ends short rather than lying.
        WritePng(ctx.out, bmp.pixels, width, height);
    }
    else
    {
        // Straight out of the canvas into the transport's framing buffer.
        const uint8_t* src = reinterpret_cast<const uint8_t*>(bmp.pixels);
        size_t sent = 0;
        if (ctx.out.canLend())
        {
            while (sent < pixelBytes)
            {
                size_t avail = 0;
                uint8_t* dst = ctx.out.lendOutput(avail);
                if (!dst) break;                       // client went away
                const size_t n = (pixelBytes - sent < avail) ? (pixelBytes - sent) : avail;
                memcpy(dst, src + sent, n);
                ctx.out.commitOutput(n);
                sent += n;
            }
        }
        else
        {
            ctx.out.write(src, pixelBytes);
        }
    }

    heap_caps_free(bmp.pixels);
    return RequestError::Ok;
}
