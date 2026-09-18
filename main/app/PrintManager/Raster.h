#pragma once

#include <cstdint>

// ──────────────────────────────────────────────────────────────
// The two pure functions in the print path, separated from the manager so they
// can be compiled and run on a PC (test/host/test_pure.cpp).
//
// They are where a wrong byte shears a label, and they need no device to be
// wrong: given pixels in and a byte count out, the answer is arithmetic. Every
// other part of printing needs a printer to check. These do not, so they are
// the part that is checked on every push.
//
// Nothing here knows about a Placement, a medium or a manager - the caller
// unpacks those. That is the whole reason this file has no includes but cstdint.
// ──────────────────────────────────────────────────────────────

namespace raster
{

/// How many bytes one raster line needs.
///
/// Always measured from head column 0, because ESC D is a COUNT and cannot be
/// given an origin: what can shrink is the far end, once the design's right
/// edge is passed. `fullHead` emits every column instead, which prints the same
/// picture in a bigger job.
inline uint32_t BytesPerLine(int32_t offsetX, uint32_t width,
                             uint32_t headDots, bool fullHead)
{
    const uint32_t max = (headDots + 7u) / 8u;
    if (fullHead) return max;

    const int64_t right = static_cast<int64_t>(offsetX) + width;
    if (right <= 0) return 1;

    const uint32_t bytes = static_cast<uint32_t>((right + 7) / 8);
    return bytes > max ? max : bytes;
}

/// Threshold one row of ARGB8888S pixels into `line`, at head column `offsetX`.
/// Returns how many dots came out black.
///
/// `line` must already hold `bytesPerLine` bytes of paper (zeroes); this only
/// sets bits. Pixels landing left of column 0 or past what the line carries are
/// dropped rather than wrapped.
inline uint32_t RasteriseRow(const uint32_t* row, uint32_t width, uint8_t* line,
                             uint32_t bytesPerLine, int32_t offsetX,
                             uint32_t threshold, bool invert)
{
    const int64_t headDots = static_cast<int64_t>(bytesPerLine) * 8;
    uint32_t black = 0;

    for (uint32_t x = 0; x < width; ++x)
    {
        const int64_t dot = static_cast<int64_t>(offsetX) + x;
        if (dot < 0) continue;            // off the left of the head
        if (dot >= headDots) break;       // past what this line carries

        // ARGB8888S is one little-endian 32-bit word per pixel, so the bytes
        // are B,G,R,A and the word reads as 0xAARRGGBB here.
        const uint32_t px = row[x];
        const uint32_t a = (px >> 24) & 0xff;
        const uint32_t r = (px >> 16) & 0xff;
        const uint32_t g = (px >>  8) & 0xff;
        const uint32_t b =  px        & 0xff;

        // Transparent is paper, not black: an SVG that does not cover its whole
        // box would otherwise print a solid rectangle. Luminance is the cheap
        // integer approximation, which is all a 1-bit threshold can justify.
        const uint32_t lum = (a == 0) ? 255 : (r * 77 + g * 151 + b * 28) >> 8;

        bool ink = lum < threshold;
        if (invert) ink = !ink;
        if (!ink) continue;

        line[dot >> 3] |= static_cast<uint8_t>(0x80u >> (dot & 7u));   // MSB is leftmost
        ++black;
    }
    return black;
}

}  // namespace raster
