#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "Mutex.h"
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// PrintManager — the DYMO LabelWriter's raster dialect, and the commands that
// put a stored SVG on paper.
//
//   /labels/x.svg + a medium from /media
//        -> physical size -> DPI -> raster size
//        -> RenderManager (ThorVG) -> ARGB8888S in PSRAM
//        -> threshold -> 1 bit per dot
//        -> placed at the medium's calibrated offsets
//        -> LabelWriter job bytes -> UsbHostManager -> bulk OUT
//
// It owns no USB, no rasteriser and no media. RenderManager draws the label -
// the same call `render svg` makes, so preview and print come from one bitmap -
// MediaManager says what the paper is, and UsbHostManager moves the bytes.
//
// ── The wire format ──
//
// The LabelWriter's raster mode is a small ESC language over bulk OUT:
//
//   ESC x100     flush any half-finished command left in the printer
//   ESC @        reset
//   ESC L hi lo  label length in dots (how far to feed)
//   ESC D n      bytes per raster line
//   SYN <n bytes> one raster line, MSB = leftmost dot, 1 = black    (x height)
//   ESC E        form feed, advancing the label to the tear bar
//
// Two details cost real paper if got wrong. The line prefix is SYN (0x16) on
// every line, not once. And ESC D is BYTES, not dots - every line must be
// exactly that many bytes or the printer loses sync and prints diagonal noise.
//
// ── The coordinate system, which is the whole of the calibration ──
//
// There are two frames and the medium is the map between them.
//
//   HEAD frame     dot column 0..HEAD_DOTS-1 across the paper, and raster line
//                  0,1,2... in the order lines are emitted. This is what the
//                  printer understands and the only thing the job contains.
//
//   LABEL frame    the design's own pixels, 0,0 at its top-left.
//
// A medium says where the label sits in the head frame: its left edge at head
// column `offsetX`, its top edge at raster line `offsetY`. Both are signed.
// offsetY < 0 means raster line 0 already lands inside the label - the printer
// starts late - so the top of the design is cropped by that much rather than
// shifted down. Total emitted lines are therefore `height + offsetY` for either
// sign, which falls out of the arithmetic rather than needing two cases.
//
// ── Why the raster is narrowed but not moved ──
//
// ESC D is a COUNT of bytes from head column 0; there is no "start at column N"
// in this dialect. So the job always begins at head column 0 and the label's
// position is expressed by leaving those columns blank. What can safely shrink
// is the far end: bytes beyond the label's right edge carry nothing, and
// dropping them is arithmetic on the count alone, with the origin untouched.
// Hence bytesPerLine = ceil((offsetX + width) / 8), not ceil(width / 8) - the
// second would print the label at the head's left edge, wherever the paper is.
// ──────────────────────────────────────────────────────────────

class PrintManager
{
    static constexpr const char* TAG = "PrintManager";

    static constexpr uint8_t ESC = 0x1b;
    static constexpr uint8_t SYN = 0x16;

    /// A job is header + one prefixed line per dot row. Bounded so a bad
    /// argument is refused rather than eating PSRAM.
    static constexpr uint32_t MAX_LINES = 4000;      ///< ~340 mm at 300 DPI

    /// Luminance at or above this is paper, below it is ink. Midpoint, and an
    /// argument, because anti-aliased text sits either side of it.
    static constexpr uint32_t THRESHOLD_DEFAULT = 128;

public:
    // ── The printer's own facts ──
    // Hardware, not configuration, and public because the media layer converts
    // millimetres with them. They deliberately do NOT appear in a medium: a roll
    // of paper does not have a resolution, and a `dpi` field on a medium would
    // be a second place for one truth.
    //
    // 300 DPI is confirmed rather than assumed - a design rendered at 295 dots
    // measured 25 mm on paper, and 295 / 300 inch is 24.98 mm.

    /// Dots per inch, both axes.
    static constexpr uint32_t DPI = 300;

    /// Print head width in dots. 672 at 300 DPI is 56.9 mm - wider than any
    /// label this printer takes, which is why a medium has to say where its
    /// paper sits inside it.
    static constexpr uint32_t HEAD_DOTS = 672;

    explicit PrintManager(AppProvider& app);

    PrintManager(const PrintManager&) = delete;
    PrintManager& operator=(const PrintManager&) = delete;
    PrintManager(PrintManager&&) = delete;
    PrintManager& operator=(PrintManager&&) = delete;

    void Init();

private:
    AppProvider& app_;
    InitState    initState_;
    Mutex        printLock_;     ///< one job on the wire at a time

    /// Where a design goes in the head frame. Everything a job needs that is
    /// not the pixels.
    struct Placement
    {
        uint32_t width   = 0;      ///< design raster width, dots
        uint32_t height  = 0;      ///< design raster height, dots
        int32_t  offsetX = 0;      ///< head column of the design's left edge
        int32_t  offsetY = 0;      ///< raster line of the design's top edge
        uint32_t threshold = THRESHOLD_DEFAULT;
        bool     invert  = false;
        bool     feed    = true;
        bool     fullHead = false; ///< emit all HEAD_DOTS columns, not just to the right edge
    };

    /// What a finished job cost, reported back so the bench has numbers.
    struct JobStats
    {
        uint32_t bytesPerLine  = 0;
        uint32_t lines         = 0;
        uint32_t jobBytes      = 0;
        uint32_t blackDots     = 0;
        uint32_t renderMs      = 0;
        uint32_t convertMs     = 0;
        uint32_t sendMs        = 0;
        uint32_t psramUsed     = 0;
        uint32_t internalUsed  = 0;
        uint32_t workerStack   = 0;
        float    scale         = 1.0f;
    };

    // ── Telling a caller how far along a print is ──
    //
    // A print is seconds of work behind one reply, and the browser used to have
    // nothing but a spinner for it - and, worse, a 10 second client-side reply
    // timeout, so a long label reported a timeout while the printer was still
    // happily feeding paper. Progress records fix both: they give the UI a bar,
    // and each one resets that idle timer.
    //
    // The shape is `partition write`'s, because a caller that already understands
    // one streaming command should not have to learn a second dialect.
    struct Progress
    {
        CommandContext* ctx    = nullptr;
        size_t          total  = 0;   ///< job bytes, so a record can carry a fraction
        size_t          last   = 0;   ///< bytes at the last record, for throttling
    };

    /// How often a send reports. The job is a few hundred KB and a record is a
    /// WebSocket frame of its own, so every chunk would be chatter.
    static constexpr size_t REPORT_EVERY = 32 * 1024;

    /// Write one `{"phase":...}` record and push it now. Null ctx is a no-op, so
    /// the internal callers that have no session need no special case.
    static void Report(CommandContext* ctx, const char* phase,
                       uint32_t done = 0, uint32_t total = 0);

    /// Render `wirePath` and print it at `p`. Returns null, or a static reason.
    const char* PrintSvg(const char* wirePath, const Placement& p, JobStats& stats,
                         CommandContext* ctx = nullptr);

    /// Emit one complete job into a PSRAM buffer and push it at the printer.
    /// `pixels` may be null, which prints blank lines - used by the patterns.
    const char* SendJob(const uint32_t* pixels, const Placement& p, JobStats& stats,
                        CommandContext* ctx = nullptr);

    /// How many bytes per raster line this placement needs. Always measured
    /// from head column 0, because ESC D is a count and not an origin.
    static uint32_t BytesPerLine(const Placement& p);

    // ── Job framing, in one place ──
    //
    // Every job - a label, the test pattern, the calibration grid - opens with
    // the same ESC sequence and closes with the same form feed. It used to be
    // written out three times, and its length appeared once as an expression
    // and twice as the literal 109, which is two chances for a size to stop
    // agreeing with what is written into it.

    /// What WriteJobHeader emits: ESC x100, ESC @, ESC L hi lo, ESC D n.
    static constexpr size_t HEADER_BYTES = 100 + 2 + 4 + 3;

    /// The trailing ESC E.
    static constexpr size_t FEED_BYTES = 2;

    /// How many bytes a job of `lines` raster lines of `bytesPerLine` each
    /// occupies, form feed included.
    static constexpr size_t JobSize(uint32_t lines, uint32_t bytesPerLine)
    {
        return HEADER_BYTES + static_cast<size_t>(lines) * (1 + bytesPerLine) + FEED_BYTES;
    }

    /// Open a job. Returns the bytes written, which is always HEADER_BYTES.
    static size_t WriteJobHeader(uint8_t* job, uint32_t lines, uint32_t bytesPerLine);

    /// One design row into one already-blanked raster line. Returns ink dots set.
    static uint32_t RasteriseRow(const uint32_t* row, uint32_t width, uint8_t* line,
                                 uint32_t bytesPerLine, int32_t offsetX,
                                 uint32_t threshold, bool invert);

    /// Resolve the geometry for a print: from a medium if one is named, from
    /// explicit dots otherwise, with either overridable for calibration.
    const char* Resolve(const char* mediaId, Placement& p,
                        bool haveWidth, bool haveHeight,
                        bool haveOffsetX, bool haveOffsetY);

    // ── Commands ──
    RequestError Cmd_PrintSvg(CommandContext& ctx);
    RequestError Cmd_PrintTest(CommandContext& ctx);
    RequestError Cmd_PrintCalibrate(CommandContext& ctx);
    RequestError Cmd_PrintStatus(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "print", "svg",       &InvokeCommand<&PrintManager::Cmd_PrintSvg>,
          "Render a stored SVG and PRINT it. Name a medium with -media and the "
          "geometry comes from it - size, and the calibrated offsets that put "
          "the design where the paper actually is - so a caller needs to know "
          "nothing about dots. The SVG is fitted exactly as 'render svg' fits "
          "it, so the preview is what comes out. This is the PHYSICAL one: it "
          "advances the roll and consumes a label, with no undo. 'render svg' "
          "with the medium's widthDots and heightDots shows the same picture "
          "and costs nothing." },
        { "print", "test",      &InvokeCommand<&PrintManager::Cmd_PrintTest>,
          "Print a built-in striped test pattern at full head width. No "
          "filesystem, no renderer and no medium, so it separates a USB or "
          "printer problem from a label or font problem." },
        { "print", "calibrate", &InvokeCommand<&PrintManager::Cmd_PrintCalibrate>,
          "Print a measuring grid across the whole head: a rule every 25 dots "
          "(2.12 mm) and a heavy rule every 100 dots (8.47 mm), with solid axes "
          "at head column 0 and raster line 0. This is how a medium's offsets "
          "are found - print it, measure where the label's edges fall against "
          "the grid, and put the result in 'media set'." },
        { "print", "status",    &InvokeCommand<&PrintManager::Cmd_PrintStatus>,
          "Whether a printer is attached and ready to take a job, with what it "
          "says about itself. 'usb status' has the full descriptor detail." },
    };
};
