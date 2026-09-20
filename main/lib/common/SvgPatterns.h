#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

// --------------------------------------------------------------
// The device's built-in designs: a calibration grid and a test pattern,
// generated as ordinary SVG.
//
// They are SVG, and not raster written straight into a job, for one reason:
// what you PREVIEW has to be what PRINTS. The preview path is `render svg` and
// the print path is `print svg`, and both take a design, fit it to the medium
// and place it at the medium's calibrated position. A grid rasterised directly
// into the job bypasses all of that, so it could never have been previewed -
// and a calibration you cannot preview is one you can only judge by spending
// labels.
//
// So the patterns go through the same pipeline as a label, and the Print button
// prints exactly the picture on the screen.
//
// ── Why the grid is centred ──
//
// The old grid was drawn in HEAD coordinates with its origin at the head's own
// (0,0): it answered "where is the paper under the head". That is the right
// question when you know nothing, and the wrong one once there is a medium,
// because the answer has to be translated into an offset by hand.
//
// This one is a DESIGN the size of the label, with its origin at the label's
// CENTRE. Print it, and the centre marker lands wherever the artwork currently
// lands. If it is not in the middle of the label, the distance and direction it
// is out by IS the alignment correction, readable straight off the rings - and
// because it goes through the normal path, changing the alignment and printing
// again moves the marker by exactly what you changed.
//
// Rings every 25 dots outward from the centre, heavier every 100, so a distance
// can be counted rather than measured. At 300 DPI 25 dots is 2.12 mm and 100 is
// 8.47 mm.
// --------------------------------------------------------------

namespace svg
{

/// Which built-in design. `None` is what a caller asked for when it named no
/// pattern at all, so a command can tell "not asked for" from "not known".
enum class Pattern { None, Calibration, Test };

/// Parse the name a command was given. False when it is not one of them.
inline bool ParsePattern(const char* name, Pattern& out)
{
    if (!name || !name[0]) { out = Pattern::None; return true; }
    if (strcmp(name, "calibration") == 0) { out = Pattern::Calibration; return true; }
    if (strcmp(name, "test")        == 0) { out = Pattern::Test;        return true; }
    return false;
}

/// Spacing of the calibration grid, in dots. Deliberately constants rather than
/// arguments: the whole value of the grid is that everyone reading one is
/// counting the same rings.
inline constexpr int PATTERN_MINOR_DOTS = 25;
inline constexpr int PATTERN_MAJOR_DOTS = 100;

namespace detail
{

/// Append with the remaining capacity, tracking the length that WOULD be
/// needed. Same discipline as snprintf: never writes past `cap`, and the
/// caller can measure by passing a zero cap.
struct Appender
{
    char*  out;
    size_t cap;
    size_t n = 0;

    void Put(const char* s)
    {
        const size_t k = strlen(s);
        if (out && n + k < cap) memcpy(out + n, s, k);
        n += k;
    }

    void Fmt(const char* fmt, int a = 0, int b = 0, int c = 0, int d = 0)
    {
        char buf[160];
        const int k = snprintf(buf, sizeof(buf), fmt, a, b, c, d);
        if (k > 0) Put(buf);
    }
};

}  // namespace detail

/// Build the centre-origin calibration grid for a design `w` x `h` dots.
///
/// Returns the length the SVG needs, NOT counting the terminating NUL; pass a
/// zero `cap` to measure. The output is always NUL-terminated when it fits.
///
/// Everything is drawn from the centre outwards, so the two halves of each axis
/// are symmetric by construction and a marker that is off-centre on paper is
/// off-centre because the ARTWORK is, not because the drawing was.
inline size_t BuildCalibrationSvg(int w, int h, char* out, size_t cap)
{
    using namespace detail;

    Appender a{ out, cap };
    if (w <= 0 || h <= 0) { if (out && cap) out[0] = '\0'; return 0; }

    const int cx = w / 2;
    const int cy = h / 2;

    a.Fmt("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
          "viewBox=\"0 0 %d %d\">", w, h, w, h);
    a.Fmt("<rect x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"#ffffff\"/>", w, h);

    // Minor rings first, so the heavier majors draw over them.
    for (int d = PATTERN_MINOR_DOTS; d <= (w > h ? w : h); d += PATTERN_MINOR_DOTS)
    {
        if ((d % PATTERN_MAJOR_DOTS) == 0) continue;   // a major, drawn below
        const int t = 1;

        if (cx - d >= 0)
            a.Fmt("<rect x=\"%d\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cx - d, t, h);
        if (cx + d < w)
            a.Fmt("<rect x=\"%d\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cx + d, t, h);
        if (cy - d >= 0)
            a.Fmt("<rect x=\"0\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cy - d, w, t);
        if (cy + d < h)
            a.Fmt("<rect x=\"0\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cy + d, w, t);
    }

    for (int d = PATTERN_MAJOR_DOTS; d <= (w > h ? w : h); d += PATTERN_MAJOR_DOTS)
    {
        const int t = 3;
        if (cx - d >= 0)
            a.Fmt("<rect x=\"%d\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cx - d, t, h);
        if (cx + d < w)
            a.Fmt("<rect x=\"%d\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cx + d, t, h);
        if (cy - d >= 0)
            a.Fmt("<rect x=\"0\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cy - d, w, t);
        if (cy + d < h)
            a.Fmt("<rect x=\"0\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#000000\"/>", cy + d, w, t);
    }

    // The centre axes, full width and height and unmistakably heavier. If only
    // one of these is on the paper, that alone says which way the artwork is
    // out and roughly by how much.
    a.Fmt("<rect x=\"%d\" y=\"0\" width=\"5\" height=\"%d\" fill=\"#000000\"/>", cx - 2, h);
    a.Fmt("<rect x=\"0\" y=\"%d\" width=\"%d\" height=\"5\" fill=\"#000000\"/>", cy - 2, w);

    // The centre marker: a filled square in an open ring, so it survives being
    // printed faintly and is still findable when the grid around it is not.
    a.Fmt("<rect x=\"%d\" y=\"%d\" width=\"21\" height=\"21\" fill=\"none\" "
          "stroke=\"#000000\" stroke-width=\"3\"/>", cx - 10, cy - 10);
    a.Fmt("<rect x=\"%d\" y=\"%d\" width=\"9\" height=\"9\" fill=\"#000000\"/>", cx - 4, cy - 4);

    // The design's own edge. Where this lands relative to the paper's edge is
    // the other half of the reading.
    a.Fmt("<rect x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"none\" "
          "stroke=\"#000000\" stroke-width=\"3\"/>", w, h);

    a.Put("</svg>");

    if (out && cap) out[a.n < cap ? a.n : cap - 1] = '\0';
    return a.n;
}

/// Build the test pattern for a design `w` x `h` dots: broad bars and a sweep
/// of greys, for telling a printer fault from a label fault.
///
/// The greys are the reason this is worth previewing rather than only printing.
/// They are what the threshold turns into ink, so the pattern shows where the
/// current threshold falls - a strip that is solid on screen and blank on paper
/// is a threshold to change, not a printer to fix.
inline size_t BuildTestSvg(int w, int h, char* out, size_t cap)
{
    using namespace detail;

    Appender a{ out, cap };
    if (w <= 0 || h <= 0) { if (out && cap) out[0] = '\0'; return 0; }

    a.Fmt("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"%d\" height=\"%d\" "
          "viewBox=\"0 0 %d %d\">", w, h, w, h);
    a.Fmt("<rect x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"#ffffff\"/>", w, h);

    // A solid border, so every edge of the printable area is visible at once.
    a.Fmt("<rect x=\"0\" y=\"0\" width=\"%d\" height=\"%d\" fill=\"none\" "
          "stroke=\"#000000\" stroke-width=\"5\"/>", w, h);

    // Vertical bars across the head: every second one inked, 16 dots wide, to
    // show dropped or smeared columns.
    const int barTop = h / 8;
    const int barH   = h / 5 > 0 ? h / 5 : 1;
    for (int x = 16; x + 16 < w; x += 32)
        a.Fmt("<rect x=\"%d\" y=\"%d\" width=\"16\" height=\"%d\" fill=\"#000000\"/>", x, barTop, barH);

    // Horizontal bars along the feed, for dropped lines.
    const int rowLeft = w / 8;
    const int rowW    = w - 2 * rowLeft > 0 ? w - 2 * rowLeft : 1;
    for (int y = barTop + barH + 16; y + 16 < h / 2 + h / 6; y += 32)
        a.Fmt("<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"16\" fill=\"#000000\"/>", rowLeft, y, rowW);

    // A grey sweep: eight steps from near-white to near-black. Whichever step
    // is the first to ink is where the threshold currently sits.
    const int greyTop  = h - h / 4;
    const int greyH    = h / 6 > 0 ? h / 6 : 1;
    const int greyLeft = w / 12;
    const int greyW    = w - 2 * greyLeft > 8 ? w - 2 * greyLeft : 8;
    for (int i = 0; i < 8; ++i)
    {
        const int level = 224 - i * 28;          // 224 down to 28
        // Inset, and the step boundaries computed from the span rather than
        // accumulated, so the eighth step ends exactly on the right edge of the
        // sweep instead of leaving whatever the division threw away. The sweep
        // clears the border too: a grey running under it hides the one line
        // that says where the printable area ends.
        const int x0 = greyLeft + (greyW * i) / 8;
        const int x1 = greyLeft + (greyW * (i + 1)) / 8;
        char fill[16];
        snprintf(fill, sizeof(fill), "#%02x%02x%02x", level, level, level);
        a.Fmt("<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" ", x0, greyTop, x1 - x0, greyH);
        a.Put("fill=\"");
        a.Put(fill);
        a.Put("\"/>");
    }

    a.Put("</svg>");

    if (out && cap) out[a.n < cap ? a.n : cap - 1] = '\0';
    return a.n;
}

}  // namespace svg
