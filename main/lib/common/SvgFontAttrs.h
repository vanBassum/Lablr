#pragma once

#include <cstddef>
#include <cstring>

// --------------------------------------------------------------
// Pushing inherited font properties onto <text>, for a parser that inherits
// none.
//
// font-size and font-family are INHERITED properties in SVG, so
//
//     <g font-family="DejaVuSans" font-size="42">
//       <text x="500" y="245">Laserblanks</text>
//     </g>
//
// draws 42-unit text in every conforming renderer. ThorVG's SVG loader reads
// neither property from anything but the <text> or <tspan> element itself:
// _createTextNode in tvgSvgLoader.cpp opens with
//
//     ctx->parser->node->node.text.fontSize = DEFAULT_FONT_SIZE;   // 10.0f
//
// and the only font-size and font-family cases in the file sit inside
// _attrParseTextNode. SvgStyleProperty - the struct that DOES inherit down the
// tree - has no font fields at all, and neither name appears in styleTags[],
// which is also why style="font-size:42" is ignored even written directly on a
// <text>. So the group above draws at 10, whatever it says, and a label comes
// out with its title right and its list a quarter of the size it asked for.
// Measured on the bench: ink 44 px tall for a title sized on its own <text>,
// 7-10 px for list lines sized on their <g>, where 32-33 px was asked for.
//
// Nothing fails and nothing is logged, because from ThorVG's side no error
// happened - a <text> with no font-size of its own is a <text> at the default
// size. That is what makes it worth fixing here rather than documenting: an
// author cannot see it in the file, and the renderer will not say it.
//
// This runs over the buffer before ThorVG parses it and writes the inherited
// value onto the element as an ordinary presentation attribute, which is the
// one form the loader does read.
//
// -- What it deliberately does not do --
//
// ONLY <text> is written to, never <tspan>. A <tspan> already inherits
// correctly: _buildTspanScene walks up to its <text> and takes the first
// fontSize and fontFamily it finds, so fixing the <text> fixes every tspan
// under it. Writing them onto the tspan as well would be worse than redundant -
// _spliceTspanClose merges an unstyled <tspan> into its parent's text run, and
// giving one attributes it did not have would stop that merge and change how
// the text lays out.
//
// It is not a CSS cascade either. There is no stylesheet, no selector and no
// specificity here: an element's own attribute wins, then its own style="",
// then whatever the nearest ancestor had by the same rule. That is the whole of
// the font inheritance an SVG file can express without a stylesheet, and a
// stylesheet is already out of scope for this renderer.
//
// Like xml::DecodeCharData next door, the real fix belongs in ThorVG - font
// properties on SvgStyleProperty, inherited with the rest. Upstream has not got
// there: v1.1.2 and main still open _createTextNode with the same constant, and
// thorvg#4538 is still open against the style="" half of it. Until that lands,
// this is the seam where a valid SVG can be made into one ThorVG reads the same
// way.
//
// It lives in lib/ because it names no layer: bytes in, bytes out, no knowledge
// of who is asking. That also makes it testable on a PC, and it is
// (test/host/test_pure.cpp).
// --------------------------------------------------------------

namespace svg
{

/// A slice of the input. `p` is null when the property was not declared.
struct Slice
{
    const char* p = nullptr;
    size_t      n = 0;

    bool Empty() const { return p == nullptr || n == 0; }
};

namespace detail
{

inline bool IsSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/// True when the name at `name` (length `n`) equals the literal `lit`. XML
/// element names are case-sensitive and SVG's are lower case, so this is too.
inline bool NameIs(const char* name, size_t n, const char* lit)
{
    const size_t len = strlen(lit);
    return n == len && memcmp(name, lit, len) == 0;
}

/// Find the value of attribute `key` in the attribute region [p, end).
/// Returns an empty slice when the attribute is absent.
inline Slice FindAttr(const char* p, const char* end, const char* key)
{
    const size_t keyLen = strlen(key);

    while (p < end)
    {
        while (p < end && (IsSpace(*p) || *p == '/')) ++p;
        if (p >= end) break;

        const char* nameStart = p;
        while (p < end && !IsSpace(*p) && *p != '=' && *p != '/') ++p;
        const size_t nameLen = static_cast<size_t>(p - nameStart);
        if (nameLen == 0) { ++p; continue; }

        while (p < end && IsSpace(*p)) ++p;
        if (p >= end || *p != '=') continue;            // valueless attribute
        ++p;
        while (p < end && IsSpace(*p)) ++p;
        if (p >= end) break;

        const char quote = *p;
        if (quote != '"' && quote != 0x27) continue;    // unquoted: not XML
        ++p;

        const char* valStart = p;
        while (p < end && *p != quote) ++p;
        const size_t valLen = static_cast<size_t>(p - valStart);
        if (p < end) ++p;                               // past the closing quote

        if (nameLen == keyLen && memcmp(nameStart, key, keyLen) == 0)
            return Slice{ valStart, valLen };
    }

    return Slice{};
}

/// Find the value of the longhand declaration `prop` in a style="" attribute.
/// Only exact longhand names match, so the `font:` shorthand is left alone
/// rather than half-understood.
inline Slice FindStyleProp(Slice style, const char* prop)
{
    if (style.Empty()) return Slice{};

    const size_t      propLen = strlen(prop);
    const char*       p       = style.p;
    const char* const end     = style.p + style.n;

    while (p < end)
    {
        const char* declEnd = p;
        while (declEnd < end && *declEnd != ';') ++declEnd;

        const char* colon = p;
        while (colon < declEnd && *colon != ':') ++colon;

        if (colon < declEnd)
        {
            const char* nameStart = p;
            const char* nameEnd   = colon;
            while (nameStart < nameEnd && IsSpace(*nameStart))  ++nameStart;
            while (nameEnd > nameStart && IsSpace(nameEnd[-1])) --nameEnd;

            if (static_cast<size_t>(nameEnd - nameStart) == propLen &&
                memcmp(nameStart, prop, propLen) == 0)
            {
                const char* valStart = colon + 1;
                const char* valEnd   = declEnd;
                while (valStart < valEnd && IsSpace(*valStart))  ++valStart;
                while (valEnd > valStart && IsSpace(valEnd[-1])) --valEnd;
                if (valEnd > valStart)
                    return Slice{ valStart, static_cast<size_t>(valEnd - valStart) };
            }
        }

        p = (declEnd < end) ? declEnd + 1 : end;
    }

    return Slice{};
}

/// A value this may write back as a double-quoted attribute. A value holding a
/// double quote is left where it is rather than escaped: it cannot have come
/// from a double-quoted presentation attribute, and inventing an escape for a
/// style="" value would be guessing at what the author meant.
inline bool Quotable(Slice v)
{
    if (v.Empty()) return false;
    for (size_t i = 0; i < v.n; ++i)
        if (v.p[i] == '"') return false;
    return true;
}

/// Writes when there is room, counts always - so one implementation measures
/// and fills, and the two can never disagree about the length.
struct Sink
{
    char*  out;
    size_t cap;
    size_t n = 0;

    void Put(const char* s, size_t k)
    {
        if (out && n + k <= cap) memcpy(out + n, s, k);
        n += k;
    }
    void Put(const char* s) { Put(s, strlen(s)); }
    void Put(Slice s)       { Put(s.p, s.n); }
};

}  // namespace detail

/// The most elements deep this tracks inheritance. Past it the innermost
/// tracked values keep applying, which is the answer inheritance would give for
/// everything but a font property declared deeper than this - and a label is a
/// handful of levels.
inline constexpr size_t MAX_DEPTH = 32;

/// Copy the XML in [in, in+len) to `out`, giving every <text> start tag an
/// explicit font-size and font-family when it has none of its own and an
/// ancestor supplies one. An element's own style="font-size:..." counts as
/// having none, because ThorVG does not read that either, so it is rewritten as
/// the attribute the loader does read.
///
/// Returns the length the output needs. Call with `cap` 0 (or `out` null) to
/// measure; a result equal to `len` means there was nothing to add and the
/// input can be parsed as it stands. Nothing is written past `cap`.
inline size_t PushDownFontAttrs(const char* in, size_t len, char* out, size_t cap)
{
    using namespace detail;

    if (!in || len == 0) return 0;

    Sink sink{ out, cap };

    struct Frame { Slice family; Slice size; };
    Frame  stack[MAX_DEPTH]{};      // every frame empty: nothing is inherited yet
    size_t depth = 0;

    const char*       src = in;
    const char* const end = in + len;

    while (src < end)
    {
        if (*src != '<') { sink.Put(src, 1); ++src; continue; }

        const size_t avail = static_cast<size_t>(end - src);

        // Markup that carries no attributes and no nesting, copied through to
        // its own terminator - which for two of them is not '>'. A CDATA
        // section matters most: its content is literal, so a '<' in there must
        // not be read as a tag.
        const char* closing    = nullptr;
        size_t      closingLen = 0;
        size_t      openingLen = 0;
        if      (avail >= 4 && memcmp(src, "<!--", 4) == 0)      { closing = "-->"; closingLen = 3; openingLen = 4; }
        else if (avail >= 9 && memcmp(src, "<![CDATA[", 9) == 0) { closing = "]]>"; closingLen = 3; openingLen = 9; }
        else if (avail >= 2 && src[1] == '?')                    { closing = "?>";  closingLen = 2; openingLen = 2; }
        else if (avail >= 2 && src[1] == '!')                    { closing = ">";   closingLen = 1; openingLen = 2; }

        if (closing)
        {
            const char* next = end;
            for (const char* p = src + openingLen; p + closingLen <= end; ++p)
                if (memcmp(p, closing, closingLen) == 0) { next = p + closingLen; break; }
            sink.Put(src, static_cast<size_t>(next - src));
            src = next;
            continue;
        }

        // A start or end tag. Its '>' is the first one outside a quoted value.
        const char* tagEnd = src + 1;
        char        quote  = 0;
        while (tagEnd < end)
        {
            if (quote)                                   { if (*tagEnd == quote) quote = 0; }
            else if (*tagEnd == '"' || *tagEnd == 0x27)  { quote = *tagEnd; }
            else if (*tagEnd == '>')                     { break; }
            ++tagEnd;
        }
        if (tagEnd >= end)        // unterminated: copy the remainder and stop
        {
            sink.Put(src, static_cast<size_t>(end - src));
            break;
        }
        const char* const afterTag = tagEnd + 1;

        if (src[1] == '/')
        {
            if (depth > 0) --depth;
            sink.Put(src, static_cast<size_t>(afterTag - src));
            src = afterTag;
            continue;
        }

        const char* nameStart = src + 1;
        const char* nameEnd   = nameStart;
        while (nameEnd < tagEnd && !IsSpace(*nameEnd) && *nameEnd != '/') ++nameEnd;
        const size_t nameLen = static_cast<size_t>(nameEnd - nameStart);

        const bool selfClosing = (tagEnd > src + 1 && tagEnd[-1] == '/');

        // What this element declares for itself. The attribute is what ThorVG
        // reads; style="" is what it ignores, and is therefore treated as
        // undeclared and re-emitted as an attribute below.
        const Slice styleAttr = FindAttr(nameEnd, tagEnd, "style");
        const Slice ownFamily = FindAttr(nameEnd, tagEnd, "font-family");
        const Slice ownSize   = FindAttr(nameEnd, tagEnd, "font-size");
        const Slice styFamily = FindStyleProp(styleAttr, "font-family");
        const Slice stySize   = FindStyleProp(styleAttr, "font-size");

        const Frame& inherited = stack[depth];

        // What this element, and everything under it, is drawn with.
        Frame effective;
        effective.family = !ownFamily.Empty() ? ownFamily
                         : !styFamily.Empty() ? styFamily : inherited.family;
        effective.size   = !ownSize.Empty()   ? ownSize
                         : !stySize.Empty()   ? stySize   : inherited.size;

        // Only <text> is written to, and only for what it does not already
        // carry as an attribute. See the note above on why never <tspan>.
        const bool  isText  = NameIs(nameStart, nameLen, "text");
        const Slice addFam  = (isText && ownFamily.Empty()) ? effective.family : Slice{};
        const Slice addSize = (isText && ownSize.Empty())   ? effective.size   : Slice{};

        if (Quotable(addFam) || Quotable(addSize))
        {
            // Everything up to the '>' - or up to the '/' of a '/>', so the
            // attributes land inside the tag rather than after it.
            const char* cut = selfClosing ? tagEnd - 1 : tagEnd;
            sink.Put(src, static_cast<size_t>(cut - src));
            if (Quotable(addFam))  { sink.Put(" font-family=\""); sink.Put(addFam);  sink.Put("\""); }
            if (Quotable(addSize)) { sink.Put(" font-size=\"");   sink.Put(addSize); sink.Put("\""); }
            sink.Put(cut, static_cast<size_t>(afterTag - cut));
        }
        else
        {
            sink.Put(src, static_cast<size_t>(afterTag - src));
        }

        if (!selfClosing && depth + 1 < MAX_DEPTH) stack[++depth] = effective;

        src = afterTag;
    }

    return sink.n;
}

}  // namespace svg
