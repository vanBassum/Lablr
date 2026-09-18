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
//   /labels/x.svg -> RenderManager (ThorVG) -> ARGB8888S in PSRAM
//                 -> threshold -> 1 bit per dot
//                 -> LabelWriter job bytes
//                 -> UsbHostManager -> bulk OUT -> printer
//
// It owns no USB and no rasteriser. RenderManager draws the label - the same
// call `render svg` makes, so the preview and the print come from one bitmap -
// and UsbHostManager moves the bytes.
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
// ── Head width, and why it is an argument ──
//
// The raster is left-aligned at the print head's origin, and a LabelWriter's
// head is wider than most media: a job narrower than the head prints at the
// left edge of the head rather than centred on the label. So the job is built
// at full head width and the rendered label is placed into it at an offset.
//
// HEAD_DOTS_DEFAULT is the 300 DPI LabelWriter 4xx/450 head (672 dots, 84
// bytes). It is a DEFAULT and an argument, not a constant, because the right
// number is a fact about the attached printer and this phase deliberately does
// not have the media layer that would know it. `usb status` reports the device
// id string, which is where the model comes from.
// ──────────────────────────────────────────────────────────────

class PrintManager
{
    static constexpr const char* TAG = "PrintManager";

    static constexpr uint8_t ESC = 0x1b;
    static constexpr uint8_t SYN = 0x16;

    /// 672 dots at 300 DPI - 56.9 mm, the LabelWriter 450 head.
    static constexpr uint32_t HEAD_DOTS_DEFAULT = 672;

    /// A job is header + one prefixed line per dot row. Bounded so a bad
    /// argument is refused rather than eating PSRAM.
    static constexpr uint32_t MAX_LINES = 4000;      ///< ~340 mm at 300 DPI

    /// Luminance at or above this is paper, below it is ink. Midpoint, and an
    /// argument, because anti-aliased text sits either side of it.
    static constexpr uint32_t THRESHOLD_DEFAULT = 128;

public:
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

    /// What a finished job cost, reported back so the bench has numbers.
    struct JobStats
    {
        uint32_t headDots      = 0;
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

    /// Render `wirePath`, threshold it, build the job and push it at the
    /// printer. Returns null on success or a static reason.
    const char* PrintSvg(const char* wirePath, uint32_t width, uint32_t height,
                         uint32_t headDots, uint32_t offsetX, uint32_t threshold,
                         bool invert, bool feed, JobStats& stats);

    /// ARGB8888S (little-endian words: B,G,R,A) to packed 1-bit rows inside an
    /// already-allocated job buffer. Returns the count of black dots, which is
    /// the cheapest way to notice a blank label before it is printed.
    static uint32_t Rasterise(const uint32_t* pixels, uint32_t width, uint32_t height,
                              uint8_t* job, uint32_t bytesPerLine, uint32_t offsetX,
                              uint32_t threshold, bool invert);

    // ── Commands ──
    RequestError Cmd_PrintSvg(CommandContext& ctx);
    RequestError Cmd_PrintTest(CommandContext& ctx);
    RequestError Cmd_PrintStatus(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "print", "svg",    &InvokeCommand<&PrintManager::Cmd_PrintSvg>,
          "Render a stored SVG and PRINT it on the attached LabelWriter. The "
          "SVG is fitted into the given dot box exactly as 'render svg' fits "
          "it, so the preview is what comes out. Sizes are in PRINTER DOTS, "
          "not millimetres: at 300 DPI one millimetre is 11.8 dots. There is "
          "no media layer yet, so the caller supplies the geometry." },
        { "print", "test",   &InvokeCommand<&PrintManager::Cmd_PrintTest>,
          "Print a built-in test pattern - a framed block of stripes at full "
          "head width. It uses no filesystem and no renderer, so it separates "
          "a USB or printer problem from an SVG or font problem." },
        { "print", "status", &InvokeCommand<&PrintManager::Cmd_PrintStatus>,
          "Whether a printer is attached and ready to take a job, with what it "
          "says about itself. 'usb status' has the full descriptor detail." },
    };
};
