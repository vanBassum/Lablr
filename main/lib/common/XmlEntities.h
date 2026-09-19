#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// ──────────────────────────────────────────────────────────────
// Resolving XML character references, in place, for a parser that does not.
//
// ThorVG's SVG loader appends a text node's bytes verbatim - see
// _svgLoaderParserText in tvgSvgLoader.cpp, which is one call to append() over
// the raw slice - so it never turns `&amp;` back into '&'. A label whose SVG
// correctly writes `AT&amp;T` therefore PRINTS the five characters `&amp;`.
// The only place the loader looks at a reference at all is
// _xmlSkipXmlEntities, which TRIMS leading and trailing ones as if they were
// whitespace, and not inside <text>, where the strip pass is skipped entirely.
//
// So this runs over the buffer before ThorVG parses it, and resolves the
// references the loader is then safe to read literally.
//
// ── The one it must NOT resolve ──
//
// `&lt;`, and any numeric reference to '<', is left exactly as written.
// ThorVG finds tags by scanning for '<', so putting one into character data
// would make the rest of the file parse as markup - a destroyed label instead
// of a wrong character. That residue is why the real fix belongs in ThorVG
// (decode inside _svgLoaderParserText, where the structure is already
// resolved); everything a label is actually likely to contain - '&', '>',
// quotes, and numeric references to any other codepoint - comes out right
// here.
//
// ── Two properties worth stating ──
//
// ONE left-to-right pass, never a rescan, so `&amp;lt;` becomes the four
// characters `&lt;` and is not then re-read as '<'.
//
// Output is never longer than input - the shortest reference, `&#0;`, is four
// bytes, and the longest UTF-8 sequence is four - so it decodes in place and
// returns the new length. Nothing is allocated.
//
// It lives in lib/ because it names no layer: it is byte arithmetic over a
// buffer, and it knows nothing about SVG, about ThorVG or about who is asking.
// That is also what makes it testable on a PC, and it is (test/host/test_pure.cpp).
// ──────────────────────────────────────────────────────────────

namespace xml
{

/// Write `cp` as UTF-8 at `dst` (up to 4 bytes). Returns the count.
inline size_t EncodeUtf8(uint32_t cp, char* dst)
{
    if (cp < 0x80u)
    {
        dst[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp < 0x800u)
    {
        dst[0] = static_cast<char>(0xC0u | (cp >> 6));
        dst[1] = static_cast<char>(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u)
    {
        dst[0] = static_cast<char>(0xE0u | (cp >> 12));
        dst[1] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
        dst[2] = static_cast<char>(0x80u | (cp & 0x3Fu));
        return 3;
    }
    dst[0] = static_cast<char>(0xF0u | (cp >> 18));
    dst[1] = static_cast<char>(0x80u | ((cp >> 12) & 0x3Fu));
    dst[2] = static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu));
    dst[3] = static_cast<char>(0x80u | (cp & 0x3Fu));
    return 4;
}

/// Read one character reference at `s`. On success sets `cp` to the codepoint
/// and `consumed` to the whole `&...;`, and returns true.
///
/// Named references are the five XML predefines and only those: any other name
/// has to be declared by the document, and ThorVG would not have honoured the
/// declaration either, so inventing an answer here would be inventing one the
/// renderer never agreed to.
inline bool ParseReference(const char* s, const char* end, uint32_t& cp, size_t& consumed)
{
    struct Named { const char* text; size_t len; uint32_t cp; };
    static const Named named[] = {
        { "&amp;",  5, 0x26u },
        { "&lt;",   4, 0x3Cu },
        { "&gt;",   4, 0x3Eu },
        { "&quot;", 6, 0x22u },
        { "&apos;", 6, 0x27u },
    };

    const size_t avail = static_cast<size_t>(end - s);

    for (const Named& n : named)
    {
        if (avail >= n.len && memcmp(s, n.text, n.len) == 0)
        {
            cp       = n.cp;
            consumed = n.len;
            return true;
        }
    }

    // Numeric: &#1234; or &#x4D2;. Shortest possible is four bytes.
    if (avail < 4 || s[1] != '#') return false;

    const bool  hex = (s[2] == 'x' || s[2] == 'X');
    const char* p   = s + (hex ? 3 : 2);

    uint32_t value  = 0;
    int      digits = 0;
    for (; p < end && *p != ';'; ++p)
    {
        uint32_t d;
        if (*p >= '0' && *p <= '9')             d = static_cast<uint32_t>(*p - '0');
        else if (hex && *p >= 'a' && *p <= 'f') d = static_cast<uint32_t>(*p - 'a' + 10);
        else if (hex && *p >= 'A' && *p <= 'F') d = static_cast<uint32_t>(*p - 'A' + 10);
        else return false;

        value = value * (hex ? 16u : 10u) + d;
        // Bounded before the next multiply, so `value` cannot wrap and a run of
        // digits cannot be walked forever looking for a ';' that is not coming.
        if (++digits > 8 || value > 0x10FFFFu) return false;
    }

    if (digits == 0 || p >= end)               return false;  // no digits, or no ';'
    if (value == 0)                            return false;  // NUL is not a character
    if (value >= 0xD800u && value <= 0xDFFFu)  return false;  // surrogate half

    cp       = value;
    consumed = static_cast<size_t>(p - s) + 1;                // including the ';'
    return true;
}

/// Resolve character references in the CHARACTER DATA of the XML in `buf`
/// (`len` bytes), in place. Returns the new length.
///
/// Markup is copied through untouched - tags, comments, CDATA sections, the
/// doctype - so a reference inside an ATTRIBUTE value is left for the parser
/// exactly as it was. That is deliberate and not an omission: an attribute's
/// value is trimmed by ThorVG's own entity-skipping pass, so rewriting one here
/// would be arguing with it, and the visible bug is text.
inline size_t DecodeCharData(char* buf, size_t len)
{
    if (!buf || len == 0) return 0;

    const char*       src = buf;
    const char* const end = buf + len;
    char*             dst = buf;

    while (src < end)
    {
        if (*src == '<')
        {
            // Markup, copied through whole. What ends it is not always '>': a
            // comment ends at "-->" and a CDATA section at "]]>", and both may
            // legitimately contain a bare '>'. A CDATA section matters twice
            // over, because its content is literal by definition - a `&amp;`
            // in there IS the five characters, and decoding it would be wrong.
            const char* closing    = ">";
            size_t      closingLen = 1;
            size_t      openingLen = 1;

            const size_t avail = static_cast<size_t>(end - src);
            if (avail >= 4 && memcmp(src, "<!--", 4) == 0)
            {
                closing = "-->"; closingLen = 3; openingLen = 4;
            }
            else if (avail >= 9 && memcmp(src, "<![CDATA[", 9) == 0)
            {
                closing = "]]>"; closingLen = 3; openingLen = 9;
            }

            const char* next = end;
            for (const char* p = src + openingLen; p + closingLen <= end; ++p)
            {
                if (memcmp(p, closing, closingLen) == 0) { next = p + closingLen; break; }
            }

            // memmove, not memcpy: dst trails src in the same buffer once
            // anything earlier has been shortened, so the ranges overlap.
            const size_t n = static_cast<size_t>(next - src);
            memmove(dst, src, n);
            dst += n;
            src = next;
            continue;
        }

        if (*src != '&') { *dst++ = *src++; continue; }

        uint32_t cp       = 0;
        size_t   consumed = 0;
        if (ParseReference(src, end, cp, consumed) && cp != 0x3Cu)
        {
            // Safe in place: the reference has been read in full before
            // anything is written, and UTF-8 is never longer than it was.
            dst += EncodeUtf8(cp, dst);
            src += consumed;
        }
        else
        {
            // Not a reference this understands, or one that would become a '<'
            // and turn the rest of the document into markup. Left as written.
            *dst++ = *src++;
        }
    }

    return static_cast<size_t>(dst - buf);
}

}  // namespace xml
