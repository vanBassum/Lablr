// ──────────────────────────────────────────────────────────────
// The parts of this firmware that need no firmware to check.
//
// There is no test framework here and no device: this file is compiled by a
// host compiler and run, and it covers exactly the functions whose answer is
// arithmetic rather than hardware. That is a deliberately short list, and the
// two things on it are the two that hurt most when wrong:
//
//   path::ResolveUnder  - the only barrier between a path a client chose and
//                         the rest of the VFS.
//   raster::BytesPerLine / RasteriseRow
//                       - where a wrong byte shears a printed label, and where
//                         "print it and look" is the slowest possible feedback.
//
// Everything else about this firmware needs a printer, a WiFi network or a
// flash partition, and is checked by driving the device over its own wire. See
// CLAUDE.md. Do not grow this file into a mock of the device.
//
//   g++ -std=c++17 -Wall -Wextra -Werror -I main/lib/common -I main/app/PrintManager test/host/test_pure.cpp -o test_pure
//   ./test_pure
// ──────────────────────────────────────────────────────────────

#include "PathResolve.h"
#include "Raster.h"

#include <cstdio>
#include <cstring>
#include <vector>

static int failures = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

// ──────────────────────────────────────────────────────────────
// path::ResolveUnder
// ──────────────────────────────────────────────────────────────

static void test_resolve_joins()
{
    char out[64];

    CHECK(path::ResolveUnder("/storage", "/labels/x.svg", out, sizeof(out)));
    CHECK(std::strcmp(out, "/storage/labels/x.svg") == 0);

    // A leading slash is optional and must not double up.
    CHECK(path::ResolveUnder("/storage", "labels/x.svg", out, sizeof(out)));
    CHECK(std::strcmp(out, "/storage/labels/x.svg") == 0);

    // The root of the mount.
    CHECK(path::ResolveUnder("/storage", "/", out, sizeof(out)));
    CHECK(std::strcmp(out, "/storage/") == 0);
}

static void test_resolve_refuses_traversal()
{
    char out[64];

    // ".." anywhere, in every shape somebody would try.
    CHECK(!path::ResolveUnder("/storage", "..", out, sizeof(out)));
    CHECK(!path::ResolveUnder("/storage", "../../etc/passwd", out, sizeof(out)));
    CHECK(!path::ResolveUnder("/storage", "/labels/../../x", out, sizeof(out)));
    CHECK(!path::ResolveUnder("/storage", "/labels/..", out, sizeof(out)));
    CHECK(!path::ResolveUnder("/storage", "labels/subdir/../x.svg", out, sizeof(out)));

    // A file whose NAME merely contains dots is not traversal, and refusing it
    // would be a bug of its own.
    CHECK(path::ResolveUnder("/storage", "/labels/v1.2.svg", out, sizeof(out)));
    CHECK(path::ResolveUnder("/storage", "/labels/.hidden", out, sizeof(out)));
}

static void test_resolve_refuses_overflow()
{
    char small[16];
    // Truncation must be a refusal, not a silently shortened path: a truncated
    // path names a different file.
    CHECK(!path::ResolveUnder("/storage", "/labels/a-very-long-name.svg",
                              small, sizeof(small)));

    char out[64];
    CHECK(!path::ResolveUnder("/storage", nullptr, out, sizeof(out)));
    CHECK(!path::ResolveUnder(nullptr, "/x", out, sizeof(out)));
    CHECK(!path::ResolveUnder("/storage", "/x", out, 0));
}

// ──────────────────────────────────────────────────────────────
// raster::BytesPerLine
// ──────────────────────────────────────────────────────────────

static void test_bytes_per_line()
{
    constexpr uint32_t HEAD = 672;          // the LabelWriter's head, 84 bytes

    // fullHead is the whole head whatever the design is.
    CHECK(raster::BytesPerLine(0, 1, HEAD, true) == 84);
    CHECK(raster::BytesPerLine(500, 100, HEAD, true) == 84);

    // Measured from column 0, because ESC D is a count and not an origin: the
    // offset is part of the width, which is the bug this guards.
    CHECK(raster::BytesPerLine(0, 8, HEAD, false) == 1);
    CHECK(raster::BytesPerLine(0, 9, HEAD, false) == 2);
    CHECK(raster::BytesPerLine(120, 283, HEAD, false) == 51);   // (120+283+7)/8

    // Never wider than the head, whatever is asked for.
    CHECK(raster::BytesPerLine(600, 400, HEAD, false) == 84);
    CHECK(raster::BytesPerLine(0, 100000, HEAD, false) == 84);

    // A design entirely left of the head still emits a line rather than zero
    // bytes - a zero-byte line would desync the printer.
    CHECK(raster::BytesPerLine(-50, 10, HEAD, false) == 1);
    CHECK(raster::BytesPerLine(-50, 60, HEAD, false) == 2);     // right edge = 10
}

// ──────────────────────────────────────────────────────────────
// raster::RasteriseRow
// ──────────────────────────────────────────────────────────────

static constexpr uint32_t BLACK = 0xFF000000u;
static constexpr uint32_t WHITE = 0xFFFFFFFFu;
static constexpr uint32_t CLEAR = 0x00000000u;   // transparent

static void test_rasterise_bit_order()
{
    // MSB is the leftmost dot. Getting this backwards mirrors every label.
    const uint32_t row[8] = { BLACK, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE };
    uint8_t line[1] = { 0 };
    const uint32_t black = raster::RasteriseRow(row, 8, line, 1, 0, 128, false);
    CHECK(black == 1);
    CHECK(line[0] == 0x80);

    const uint32_t row2[8] = { WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, WHITE, BLACK };
    uint8_t line2[1] = { 0 };
    raster::RasteriseRow(row2, 8, line2, 1, 0, 128, false);
    CHECK(line2[0] == 0x01);
}

static void test_rasterise_transparent_is_paper()
{
    // An SVG that does not cover its whole box must not print a solid block.
    const uint32_t row[4] = { CLEAR, CLEAR, CLEAR, CLEAR };
    uint8_t line[1] = { 0 };
    const uint32_t black = raster::RasteriseRow(row, 4, line, 1, 0, 128, false);
    CHECK(black == 0);
    CHECK(line[0] == 0x00);
}

static void test_rasterise_offset_and_clipping()
{
    const uint32_t row[4] = { BLACK, BLACK, BLACK, BLACK };

    // Shifted right by 4: bits 4..7 of the first byte.
    uint8_t line[2] = { 0, 0 };
    CHECK(raster::RasteriseRow(row, 4, line, 2, 4, 128, false) == 4);
    CHECK(line[0] == 0x0f);
    CHECK(line[1] == 0x00);

    // Negative offset drops what falls off the left rather than wrapping it
    // round to the right of the head.
    uint8_t line2[1] = { 0 };
    CHECK(raster::RasteriseRow(row, 4, line2, 1, -2, 128, false) == 2);
    CHECK(line2[0] == 0xc0);

    // Past the end of the line, the rest is dropped, not written out of bounds.
    uint8_t line3[1] = { 0 };
    CHECK(raster::RasteriseRow(row, 4, line3, 1, 6, 128, false) == 2);
    CHECK(line3[0] == 0x03);
}

static void test_rasterise_threshold_and_invert()
{
    // Mid grey either side of the default threshold.
    const uint32_t dark  = 0xFF404040u;   // luminance ~64
    const uint32_t light = 0xFFC0C0C0u;   // luminance ~192
    const uint32_t row[2] = { dark, light };

    uint8_t line[1] = { 0 };
    CHECK(raster::RasteriseRow(row, 2, line, 1, 0, 128, false) == 1);
    CHECK(line[0] == 0x80);

    // Raising the threshold makes the lighter pixel ink too, which is what the
    // -threshold argument is for.
    uint8_t line2[1] = { 0 };
    CHECK(raster::RasteriseRow(row, 2, line2, 1, 0, 200, false) == 2);
    CHECK(line2[0] == 0xc0);

    // Invert flips the decision, transparent included.
    uint8_t line3[1] = { 0 };
    CHECK(raster::RasteriseRow(row, 2, line3, 1, 0, 128, true) == 1);
    CHECK(line3[0] == 0x40);
}

static void test_rasterise_never_writes_past_the_line()
{
    // A wide design against a narrow line: the guard band must come back clean.
    std::vector<uint32_t> row(2000, BLACK);
    uint8_t buf[16];
    std::memset(buf, 0, sizeof(buf));

    const uint32_t bytesPerLine = 4;
    raster::RasteriseRow(row.data(), static_cast<uint32_t>(row.size()),
                         buf, bytesPerLine, 0, 128, false);

    for (size_t i = 0; i < bytesPerLine; ++i) CHECK(buf[i] == 0xff);
    for (size_t i = bytesPerLine; i < sizeof(buf); ++i) CHECK(buf[i] == 0x00);
}

int main()
{
    test_resolve_joins();
    test_resolve_refuses_traversal();
    test_resolve_refuses_overflow();

    test_bytes_per_line();

    test_rasterise_bit_order();
    test_rasterise_transparent_is_paper();
    test_rasterise_offset_and_clipping();
    test_rasterise_threshold_and_invert();
    test_rasterise_never_writes_past_the_line();

    if (failures == 0) std::printf("all host tests passed\n");
    else               std::printf("%d host check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
