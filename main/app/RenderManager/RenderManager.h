#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "Semaphore.h"
#include "Mutex.h"
#include "ContextLock.h"
#include <pthread.h>
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// SVG in, pixels out. ThorVG's software rasteriser, fed from the label
// filesystem, answering a command that returns the bitmap without printing.
//
// ── Why there is a worker thread, and why it is a pthread ──
//
// ThorVG is built with its thread pool compiled in (it cannot be built without
// it - see the note in sdkconfig.defaults) and its scheduler asks for the
// current thread's identity. On ESP-IDF that reaches pthread_self(), which
// hard-asserts when the caller is a FreeRTOS task that pthread did not create:
//
//     assert failed: pthread_self pthread.c:583 "Failed to find current thread ID!"
//
// Command handlers run on the httpd or relay task, so calling into ThorVG from
// a handler reboots the device. Every ThorVG call therefore happens on this
// worker, created with pthread_create: engine init, font loading and every
// render. The handler hands over a job and waits.
//
// That also settles the stack question. The rasteriser is recursive over the
// scene graph and the httpd task's 8 KB is not obviously enough; the worker
// gets a stack sized for the job and reports what it actually used.
//
// ── Why fonts are loaded here and not by ThorVG ──
//
// ThorVG can open a font by path, and on ESP that path runs through its
// portable fallback: fopen, then one malloc of the whole file. A font is
// hundreds of kilobytes and that malloc has a good chance of landing in
// internal RAM, which is the scarce kind. So this manager reads the file itself
// into PSRAM and hands ThorVG the block (tvg_font_load_data with copy=false),
// which also lets it choose the name.
//
// That name is the contract with the SVG. ThorVG resolves a <text> element's
// font-family against its font registry by exact name, so
//
//     /fonts/DejaVuSans.ttf   registers as   "DejaVuSans"
//     <text font-family="DejaVuSans">        matches
//
// The rule is: the filename without its extension. Nothing normalises case or
// strips spaces, and a font-family that matches nothing renders no text at all
// rather than falling back visibly - which is why `render fonts` exists.
//
// Buffers are never freed once registered: copy=false means ThorVG holds the
// pointer, fonts are few, and re-reading them per render would cost hundreds of
// kilobytes of PSRAM churn per label.
// ──────────────────────────────────────────────────────────────

namespace svg { struct QrDiagnostic; }

class RenderManager
{
    static constexpr const char* TAG = "RenderManager";

    /// Bounds a render request. This cap keeps a typo from becoming an
    /// allocation failure deep inside the engine.
    static constexpr uint32_t MAX_DIMENSION = 2000;
    static constexpr uint32_t MAX_PIXELS    = 4u * 1024u * 1024u / 4u;   // 4 MB of ARGB

    static constexpr size_t MAX_FONTS = 8;

    /// The worker's stack. Generous on purpose - it is one task, and the
    /// alternative to guessing high is a stack overflow inside a rasteriser.
    /// The render command reports the high-water mark so this can be trimmed
    /// on evidence rather than on nerve.
    static constexpr size_t WORKER_STACK = 16 * 1024;

public:
    explicit RenderManager(AppProvider& app);

    RenderManager(const RenderManager&) = delete;
    RenderManager& operator=(const RenderManager&) = delete;
    RenderManager(RenderManager&&) = delete;
    RenderManager& operator=(RenderManager&&) = delete;

    void Init();

    /// A rendered label, owned by the caller: free `pixels` with heap_caps_free.
    /// This is the same buffer `render svg` ships to a browser, which is the
    /// point - the printer must not get a second rasteriser, or a preview would
    /// stop predicting a print.
    struct Bitmap
    {
        uint32_t* pixels = nullptr;      ///< ARGB8888S in PSRAM, width*height words
        uint32_t  width  = 0;
        uint32_t  height = 0;
        float     scale  = 1.0f;         ///< what the SVG was fitted by
        uint32_t  psramUsed = 0;
        uint32_t  internalUsed = 0;
        uint32_t  stackLeft = 0;
    };

    /// Render a stored SVG into `out`. `vfsPath` is a resolved VFS path, not a
    /// wire path - callers get one from StorageManager::Resolve. Returns null on
    /// success, or a static reason string.
    const char* Render(const char* vfsPath, uint32_t width, uint32_t height,
                       uint32_t background, Bitmap& out);

    /// The bounds Render enforces, so a caller can refuse before allocating.
    static constexpr uint32_t MaxDimension() { return MAX_DIMENSION; }
    static constexpr uint32_t MaxPixels()    { return MAX_PIXELS; }

private:
    AppProvider& app_;
    InitState initState_;

    // ── The worker ──
    pthread_t  worker_ = 0;
    bool       workerUp_ = false;
    bool       engineUp_ = false;
    Semaphore  ready_;        ///< worker posts once the engine and fonts are up
    Semaphore  jobRequest_;
    Semaphore  jobDone_;
    Mutex      renderLock_;   ///< one render at a time; there is one worker

    static void* WorkerEntry(void* self);
    void WorkerLoop();

    /// What the handler asks for and what comes back. One instance, guarded by
    /// renderLock_ - a second render waits rather than queues, because the
    /// worker is single and a queue would only hide that.
    struct Job
    {
        // in
        char     path[256];
        uint32_t width;
        uint32_t height;
        uint32_t background;
        // out
        uint32_t*   pixels;       ///< PSRAM, owned by the handler once done
        const char* error;        ///< null on success
        float       scale;
        uint32_t    psramUsed;
        uint32_t    internalUsed;
        uint32_t    stackLeft;
        /// Backing store for `error` when the message carries numbers - a QR
        /// box that is too small has to say how small and what would fit.
        char        errorBuf[192];
    };
    Job job_{};

    void RunJob();

    /// Turn a QR expansion failure into a message that says what to change.
    void DescribeQrFailure(const svg::QrDiagnostic& diag);

    struct LoadedFont
    {
        char     name[32];
        uint32_t size;
    };
    LoadedFont fonts_[MAX_FONTS];
    size_t     fontCount_ = 0;

    void LoadFonts();
    bool LoadFont(const char* dirPath, const char* fileName);

    /// Read a whole file into a PSRAM block. Caller owns it. Null on failure.
    static uint8_t* ReadFileToPsram(const char* fullPath, size_t& sizeOut);

    // ── Commands ──
    RequestError Cmd_RenderSvg(CommandContext& ctx);
    RequestError Cmd_Fonts(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "render", "svg",   &InvokeCommand<&RenderManager::Cmd_RenderSvg>,
          "Render a stored SVG to a bitmap and return it WITHOUT printing. The "
          "reply is a JSON header line (width, height, format, contentType), a "
          "newline, then the image. Pass format='png' to get an ordinary PNG "
          "you can simply look at; the default 'raw' is ARGB8888S pixels, for a "
          "caller that will draw or print them itself. This is the preview: use "
          "it to see what a label will look like before committing it. Nothing "
          "is consumed and no paper moves, so it can be repeated freely - "
          "unlike 'print svg', which spends a label. QR CODES: do not "
          "generate QR modules yourself - put the payload on a rect, as "
          "<rect x=.. y=.. width=.. height=.. data-qr=\"<text>\" "
          "data-qr-ecc=\"M\"/>, and this device encodes it while "
          "rendering. See 'system describe' for the attributes, the "
          "defaults and the minimum box size." },
        { "render", "fonts", &InvokeCommand<&RenderManager::Cmd_Fonts>,
          "List the fonts registered with the renderer. An SVG's font-family "
          "must match one of these names exactly or its text renders as "
          "nothing. Names come from the filenames in /fonts, without the "
          "extension." },
    };
};
