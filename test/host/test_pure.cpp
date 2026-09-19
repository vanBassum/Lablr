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
//   svg::PushDownFontAttrs
//                       - the second such pass, and the one that has to agree
//                         with itself twice: it is called once to measure and
//                         once to fill, so a disagreement is a buffer overrun.
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
#include "SvgFontAttrs.h"

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

// --------------------------------------------------------------
// svg::PushDownFontAttrs
// --------------------------------------------------------------

/// Runs the two-call protocol the renderer uses - measure, allocate, fill - and
/// returns what was written. Measuring and filling disagreeing about the length
/// is the failure this is really guarding, so it is checked here every time.
static std::string PushDown(const char* in)
{
    const size_t len  = std::strlen(in);
    const size_t need = svg::PushDownFontAttrs(in, len, nullptr, 0);

    std::vector<char> out(need + 1, 0);
    const size_t wrote = svg::PushDownFontAttrs(in, len, out.data(), need);
    CHECK(wrote == need);

    return std::string(out.data(), need);
}

#define CHECK_PUSHDOWN(in, want)                                               \
    do {                                                                       \
        const std::string got = PushDown(in);                                  \
        if (got != (want)) {                                                   \
            std::printf("FAIL %s:%d\n  in   %s\n  got  %s\n  want %s\n",       \
                        __FILE__, __LINE__, (in), got.c_str(), (want));        \
            ++failures;                                                        \
        }                                                                      \
    } while (0)

static void test_pushdown_the_bug_that_started_it()
{
    // The list on laser-connectors-box-v4.svg. ThorVG read no font property off
    // the <g> and drew all five lines at its own default of 10.
    CHECK_PUSHDOWN(
        "<g font-family=\"DejaVuSans\" font-size=\"42\">"
        "<text x=\"500\" y=\"245\">Laserblanks</text>"
        "</g>",

        "<g font-family=\"DejaVuSans\" font-size=\"42\">"
        "<text x=\"500\" y=\"245\" font-family=\"DejaVuSans\" font-size=\"42\">Laserblanks</text>"
        "</g>");
}

static void test_pushdown_leaves_an_explicit_text_alone()
{
    // The title on the same label, which was always right. Nothing to add means
    // the output is the input byte for byte, which is what lets the renderer
    // skip the second allocation entirely.
    const char* in =
        "<text x=\"62\" y=\"120\" font-family=\"DejaVuSans-Bold\" font-size=\"58\">L</text>";
    CHECK_PUSHDOWN(in, in);
    CHECK(svg::PushDownFontAttrs(in, std::strlen(in), nullptr, 0) == std::strlen(in));
}

static void test_pushdown_own_attribute_beats_the_ancestor()
{
    CHECK_PUSHDOWN(
        "<g font-size=\"42\"><text font-size=\"58\">T</text></g>",
        "<g font-size=\"42\"><text font-size=\"58\">T</text></g>");

    // One of the two can be inherited while the other is the element's own.
    // What is added goes on the end of the tag, after what was already there.
    CHECK_PUSHDOWN(
        "<g font-family=\"A\" font-size=\"42\"><text font-size=\"58\">T</text></g>",
        "<g font-family=\"A\" font-size=\"42\"><text font-size=\"58\" font-family=\"A\">T</text></g>");
}

static void test_pushdown_nearest_ancestor_wins()
{
    CHECK_PUSHDOWN(
        "<g font-size=\"10\"><g font-size=\"42\"><text>T</text></g></g>",
        "<g font-size=\"10\"><g font-size=\"42\"><text font-size=\"42\">T</text></g></g>");

    // ...and the inner group's value must not leak out to a later sibling.
    CHECK_PUSHDOWN(
        "<g font-size=\"10\"><g font-size=\"42\"><text>A</text></g><text>B</text></g>",
        "<g font-size=\"10\"><g font-size=\"42\"><text font-size=\"42\">A</text></g>"
        "<text font-size=\"10\">B</text></g>");
}

static void test_pushdown_rewrites_style_the_loader_ignores()
{
    // font-size is not in ThorVG's styleTags[], so style="" is dropped even on
    // the <text> itself. Re-emitted as the attribute the loader does read.
    CHECK_PUSHDOWN(
        "<text style=\"font-size:58px\">T</text>",
        "<text style=\"font-size:58px\" font-size=\"58px\">T</text>");

    // And on an ancestor, where it has to be inherited as well as rewritten.
    CHECK_PUSHDOWN(
        "<g style=\"fill:red; font-family:DejaVuSans ; font-size: 42 \"><text>T</text></g>",
        "<g style=\"fill:red; font-family:DejaVuSans ; font-size: 42 \">"
        "<text font-family=\"DejaVuSans\" font-size=\"42\">T</text></g>");

    // An attribute on the same element still wins over its own style="".
    CHECK_PUSHDOWN(
        "<g font-size=\"42\" style=\"font-size:10\"><text>T</text></g>",
        "<g font-size=\"42\" style=\"font-size:10\"><text font-size=\"42\">T</text></g>");

    // The `font:` shorthand is not half-understood: it is not a longhand, so it
    // supplies nothing and nothing is written.
    CHECK_PUSHDOWN(
        "<g style=\"font:42px DejaVuSans\"><text>T</text></g>",
        "<g style=\"font:42px DejaVuSans\"><text>T</text></g>");
}

static void test_pushdown_never_touches_a_tspan()
{
    // A <tspan> inherits correctly from its <text> inside ThorVG already, and
    // writing attributes onto one would stop _spliceTspanClose merging it.
    CHECK_PUSHDOWN(
        "<g font-size=\"42\"><text><tspan>T</tspan></text></g>",
        "<g font-size=\"42\"><text font-size=\"42\"><tspan>T</tspan></text></g>");
}

static void test_pushdown_self_closing_tags()
{
    // A self-closing <text/> takes its attributes INSIDE the tag, before the
    // slash - not after it, which would put them in the character data.
    CHECK_PUSHDOWN(
        "<g font-size=\"42\"><text x=\"1\"/></g>",
        "<g font-size=\"42\"><text x=\"1\" font-size=\"42\"/></g>");

    // A self-closing element must not push a frame, or everything after it
    // inherits from a group that already closed.
    CHECK_PUSHDOWN(
        "<g font-size=\"42\"><rect font-size=\"10\" width=\"1\"/><text>T</text></g>",
        "<g font-size=\"42\"><rect font-size=\"10\" width=\"1\"/><text font-size=\"42\">T</text></g>");
}

static void test_pushdown_leaves_markup_alone()
{
    // A comment, a CDATA section and a declaration are copied through and none
    // of them opens a scope. The markup inside the CDATA is literal.
    CHECK_PUSHDOWN(
        "<?xml version=\"1.0\"?><!-- <g font-size=\"99\"> -->"
        "<g font-size=\"42\"><![CDATA[<text>x</text>]]><text>T</text></g>",

        "<?xml version=\"1.0\"?><!-- <g font-size=\"99\"> -->"
        "<g font-size=\"42\"><![CDATA[<text>x</text>]]><text font-size=\"42\">T</text></g>");
}

static void test_pushdown_survives_odd_input()
{
    // A '>' inside an attribute value does not end the tag.
    CHECK_PUSHDOWN(
        "<g data-note=\"a > b\" font-size=\"42\"><text>T</text></g>",
        "<g data-note=\"a > b\" font-size=\"42\"><text font-size=\"42\">T</text></g>");

    // Single-quoted values are values too.
    CHECK_PUSHDOWN(
        "<g font-size='42'><text>T</text></g>",
        "<g font-size='42'><text font-size=\"42\">T</text></g>");

    // A value holding a double quote cannot be written back as one, so it is
    // left where it is rather than escaped into something else.
    CHECK_PUSHDOWN(
        "<g style='font-family:He\"llo'><text>T</text></g>",
        "<g style='font-family:He\"llo'><text>T</text></g>");

    // Truncated markup terminates instead of running off the buffer.
    CHECK_PUSHDOWN("<g font-size=\"42\"><text", "<g font-size=\"42\"><text");
    CHECK(svg::PushDownFontAttrs("", 0, nullptr, 0) == 0);
    CHECK(svg::PushDownFontAttrs(nullptr, 7, nullptr, 0) == 0);

    // More closing tags than opening ones must not walk the stack negative.
    CHECK_PUSHDOWN("</g></g><text>T</text>", "</g></g><text>T</text>");
}

static void test_pushdown_never_writes_past_the_cap()
{
    // The renderer only ever passes the measured length, but a short buffer
    // must still be a truncated answer and not a corrupted heap.
    const char* in   = "<g font-size=\"42\"><text>T</text></g>";
    const size_t len = std::strlen(in);
    const size_t need = svg::PushDownFontAttrs(in, len, nullptr, 0);
    CHECK(need > len);

    const char GUARD = 0x7f;
    for (size_t cap = 0; cap < need; ++cap)
    {
        std::vector<char> buf(need + 8, GUARD);
        CHECK(svg::PushDownFontAttrs(in, len, buf.data(), cap) == need);
        for (size_t i = cap; i < buf.size(); ++i) CHECK(buf[i] == GUARD);
    }
}

static void test_pushdown_a_whole_label()
{
    // The representative label, cut down: a title that sizes itself, a rotated
    // group, and a list that does not. Only the list lines change.
    const char* in =
        "<svg width=\"638\" height=\"1193\" viewBox=\"0 0 638 1193\">"
        "<rect width=\"638\" height=\"1193\" fill=\"white\"/>"
        "<g transform=\"translate(638 0) rotate(90)\">"
        "<text x=\"62\" y=\"120\" font-family=\"DejaVuSans-Bold\" font-size=\"58\">LASER</text>"
        "<g font-family=\"DejaVuSans\" font-size=\"42\">"
        "<text x=\"500\" y=\"245\">A</text><text x=\"500\" y=\"315\">B</text>"
        "</g></g></svg>";
    const char* want =
        "<svg width=\"638\" height=\"1193\" viewBox=\"0 0 638 1193\">"
        "<rect width=\"638\" height=\"1193\" fill=\"white\"/>"
        "<g transform=\"translate(638 0) rotate(90)\">"
        "<text x=\"62\" y=\"120\" font-family=\"DejaVuSans-Bold\" font-size=\"58\">LASER</text>"
        "<g font-family=\"DejaVuSans\" font-size=\"42\">"
        "<text x=\"500\" y=\"245\" font-family=\"DejaVuSans\" font-size=\"42\">A</text>"
        "<text x=\"500\" y=\"315\" font-family=\"DejaVuSans\" font-size=\"42\">B</text>"
        "</g></g></svg>";
    CHECK_PUSHDOWN(in, want);
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

    test_pushdown_the_bug_that_started_it();
    test_pushdown_leaves_an_explicit_text_alone();
    test_pushdown_own_attribute_beats_the_ancestor();
    test_pushdown_nearest_ancestor_wins();
    test_pushdown_rewrites_style_the_loader_ignores();
    test_pushdown_never_touches_a_tspan();
    test_pushdown_self_closing_tags();
    test_pushdown_leaves_markup_alone();
    test_pushdown_survives_odd_input();
    test_pushdown_never_writes_past_the_cap();
    test_pushdown_a_whole_label();

    if (failures == 0) std::printf("all host tests passed\n");
    else               std::printf("%d host check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
