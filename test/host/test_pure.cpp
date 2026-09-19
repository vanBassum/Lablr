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
//   xml::DecodeCharData - the one pass that rewrites a label's bytes before the
//                         renderer parses them, where getting the bookkeeping
//                         wrong corrupts an SVG rather than mis-drawing it.
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
#include "XmlEntities.h"

#include <cstdio>
#include <cstring>
#include <string>
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

// ──────────────────────────────────────────────────────────────
// xml::DecodeCharData
// ──────────────────────────────────────────────────────────────

/// Decode `in` and compare against `want`. A helper rather than a macro because
/// every case is the same shape: a buffer, a length back, a string comparison.
static void checkDecode(const char* in, const char* want, int line)
{
    std::vector<char> buf(in, in + std::strlen(in));
    buf.push_back('\0');
    const size_t n = xml::DecodeCharData(buf.data(), std::strlen(in));
    const std::string got(buf.data(), n);
    if (got != want)
    {
        std::printf("FAIL %s:%d  decode(\"%s\")\n      got  \"%s\"\n      want \"%s\"\n",
                    __FILE__, line, in, got.c_str(), want);
        ++failures;
    }
}

#define CHECK_DECODE(in, want) checkDecode((in), (want), __LINE__)

static void test_decode_the_bug_that_started_it()
{
    // The whole reason this exists: ThorVG printed the entity instead of '&'.
    CHECK_DECODE("<text>AT&amp;T</text>", "<text>AT&T</text>");
}

static void test_decode_named_references()
{
    CHECK_DECODE("<t>a &gt; b</t>",    "<t>a > b</t>");
    CHECK_DECODE("<t>&quot;hi&quot;</t>", "<t>\"hi\"</t>");
    CHECK_DECODE("<t>it&apos;s</t>",   "<t>it's</t>");
}

static void test_decode_leaves_less_than_alone()
{
    // Decoding these would put a '<' into character data, and ThorVG finds tags
    // by scanning for '<' - the rest of the file would parse as markup. A wrong
    // character beats a destroyed label.
    CHECK_DECODE("<t>a &lt; b</t>",   "<t>a &lt; b</t>");
    CHECK_DECODE("<t>&#60;</t>",      "<t>&#60;</t>");
    CHECK_DECODE("<t>&#x3C;</t>",     "<t>&#x3C;</t>");
}

static void test_decode_is_one_pass()
{
    // `&amp;lt;` is the four characters `&lt;`, NOT '<'. A decoder that rescans
    // its own output gets this wrong, and gets it wrong in the direction that
    // breaks parsing.
    CHECK_DECODE("<t>&amp;lt;</t>", "<t>&lt;</t>");
    CHECK_DECODE("<t>&amp;amp;</t>", "<t>&amp;</t>");
}

static void test_decode_numeric_references()
{
    CHECK_DECODE("<t>&#65;</t>",    "<t>A</t>");
    CHECK_DECODE("<t>&#x41;</t>",   "<t>A</t>");
    CHECK_DECODE("<t>&#38;</t>",    "<t>&</t>");
    // Multi-byte, and the output is shorter than the reference either way.
    CHECK_DECODE("<t>&#233;</t>",   "<t>\xc3\xa9</t>");        // e-acute, 2 bytes
    CHECK_DECODE("<t>&#8364;</t>",  "<t>\xe2\x82\xac</t>");    // euro, 3 bytes
    CHECK_DECODE("<t>&#128512;</t>", "<t>\xf0\x9f\x98\x80</t>"); // emoji, 4 bytes
}

static void test_decode_leaves_nonsense_alone()
{
    // An ampersand that is not a reference is just an ampersand, and an
    // undeclared name is one ThorVG would not have honoured either.
    CHECK_DECODE("<t>a & b</t>",       "<t>a & b</t>");
    CHECK_DECODE("<t>&nbsp;</t>",      "<t>&nbsp;</t>");
    CHECK_DECODE("<t>&amp</t>",        "<t>&amp</t>");     // no semicolon
    CHECK_DECODE("<t>&#;</t>",         "<t>&#;</t>");      // no digits
    CHECK_DECODE("<t>&#xZZ;</t>",      "<t>&#xZZ;</t>");   // not hex
    CHECK_DECODE("<t>&#0;</t>",        "<t>&#0;</t>");     // NUL is not a character
    CHECK_DECODE("<t>&#55296;</t>",    "<t>&#55296;</t>"); // surrogate half
    CHECK_DECODE("<t>&#1114112;</t>",  "<t>&#1114112;</t>"); // past the last codepoint
    CHECK_DECODE("<t>&#999999999999;</t>", "<t>&#999999999999;</t>"); // no overflow
}

static void test_decode_leaves_markup_alone()
{
    // An attribute value belongs to ThorVG's own entity-trimming pass, so it is
    // copied through exactly as written.
    CHECK_DECODE("<t font-family=\"A&amp;B\">x</t>", "<t font-family=\"A&amp;B\">x</t>");

    // A CDATA section is literal by definition.
    CHECK_DECODE("<t><![CDATA[a&amp;b]]>c&amp;d</t>", "<t><![CDATA[a&amp;b]]>c&d</t>");

    // A comment may contain a bare '>' without ending, and what follows it is
    // still character data.
    CHECK_DECODE("<t><!-- a > b &amp; c -->d&amp;e</t>", "<t><!-- a > b &amp; c -->d&e</t>");
}

static void test_decode_handles_truncation()
{
    // A file that ends mid-tag must not walk off the end. The answer does not
    // have to be useful, only in bounds.
    CHECK_DECODE("<t>a&amp;b</t", "<t>a&b</t");
    CHECK_DECODE("<!-- unterminated", "<!-- unterminated");
    CHECK_DECODE("<![CDATA[unterminated", "<![CDATA[unterminated");
    CHECK_DECODE("&", "&");
    CHECK_DECODE("<", "<");

    // And the empty case has an answer rather than a crash.
    CHECK(xml::DecodeCharData(nullptr, 0) == 0);
    char nothing[1] = { 'x' };
    CHECK(xml::DecodeCharData(nothing, 0) == 0);
}

static void test_decode_a_whole_label()
{
    // The shape a real label arrives in, to prove the pieces compose.
    const char* in =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\">"
        "<rect width=\"100\" height=\"50\" fill=\"#fff\"/>"
        "<text x=\"5\" y=\"30\" font-family=\"DejaVuSans\">R&amp;D &#8364;5</text>"
        "</svg>";
    const char* want =
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"100\" height=\"50\">"
        "<rect width=\"100\" height=\"50\" fill=\"#fff\"/>"
        "<text x=\"5\" y=\"30\" font-family=\"DejaVuSans\">R&D \xe2\x82\xac""5</text>"
        "</svg>";
    CHECK_DECODE(in, want);
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

    test_decode_the_bug_that_started_it();
    test_decode_named_references();
    test_decode_leaves_less_than_alone();
    test_decode_is_one_pass();
    test_decode_numeric_references();
    test_decode_leaves_nonsense_alone();
    test_decode_leaves_markup_alone();
    test_decode_handles_truncation();
    test_decode_a_whole_label();

    if (failures == 0) std::printf("all host tests passed\n");
    else               std::printf("%d host check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
