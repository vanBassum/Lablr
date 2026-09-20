#pragma once

#include <cstddef>
#include <cstring>

#include "SvgFontAttrs.h"
#include "XmlEntities.h"

// --------------------------------------------------------------
// Expanding a QR placeholder into ordinary SVG, for a renderer that has never
// heard of QR codes.
//
// A label author - increasingly a model at the other end of the relay - writes
// the PAYLOAD and nothing else:
//
//     <rect x="20" y="20" width="120" height="120"
//           data-qr="https://parts.local/bc547" data-qr-ecc="M"/>
//
// and this pass replaces that element, before ThorVG parses the buffer, with a
// white field and one <path> of black modules. What ThorVG is handed contains
// no trace of QR: it is a rect and a path, which it has always understood.
//
// -- Why a placeholder, and not a matrix in the file --
//
// A QR code is not a drawing, it is an error-correcting code: the payload
// becomes codewords, Reed-Solomon parity is computed over GF(256), the result
// is interleaved and laid out in a fixed serpentine around the finder and
// timing patterns, and one of eight masks is chosen by penalty score. None of
// that is guessable, and a matrix that is WRONG is the dangerous case - not the
// one that fails to scan, but the one that scans to something subtly different,
// on a label already stuck to a bottle. So the device encodes, every time, from
// the payload. The file then carries the URL rather than a picture of it, which
// also means a label can be read and re-rendered instead of only reprinted.
//
// -- The geometry, and why it is snapped --
//
// A module must land on a whole number of printer dots. At 300 DPI a dot is
// 0.085 mm, and a module 3.5 dots wide cannot be printed as 3.5 dots - it comes
// out as 3 or 4, chosen per module by the rasteriser, and a code whose modules
// alternate between two widths is one a scanner may refuse. So the module size
// is floor(box / modules), the code is centred in the box with what does not
// divide left as margin, and the origin is floored to a whole unit too. One
// user unit is one dot when the label is rendered at its medium's dot geometry,
// which is how labels here are authored.
//
// QR_MIN_MODULE_DOTS is the floor below which this refuses rather than emits.
// 3 dots is 0.254 mm at 300 DPI, about the smallest module a thermal printer
// reproduces reliably once ink spread is allowed for. A box too small for that
// is a clear error naming the size that would work - the alternative is a label
// that looks right, prints, and does not scan.
//
// The quiet zone is INSIDE the declared box, four modules on every side, as the
// spec requires. It is painted white rather than merely left empty, so a code
// dropped onto a dark part of a label still has the border it needs.
// --------------------------------------------------------------

namespace svg
{

/// Error correction level, as `data-qr-ecc` spells it: L, M, Q, H.
enum class QrEcc { Low, Medium, Quartile, High };

/// The modules this pass draws come from here. The device binds the real
/// encoder (espressif/qrcode); the host tests bind a stub, which is what lets
/// the geometry be tested without a device or that dependency.
struct QrEncoder
{
    virtual ~QrEncoder() = default;

    /// Encode `text` (NUL-terminated, `len` bytes before the NUL) at `ecc`.
    /// Returns the side length in modules, or 0 when the payload will not fit
    /// any version the encoder is willing to produce.
    virtual int Encode(const char* text, size_t len, QrEcc ecc) = 0;

    /// Dark (true) or light, for the code built by the last Encode.
    virtual bool Module(int x, int y) const = 0;
};

/// The spec's quiet zone: four modules of clear margin on every side.
inline constexpr int QR_QUIET_MODULES = 4;

/// The smallest module this pass will emit, in user units (= dots at scale 1).
/// Below this a printed code stops being reliable, so it is refused instead.
inline constexpr int QR_MIN_MODULE_DOTS = 3;

/// The longest `data-qr` payload accepted, after character references are
/// resolved. A label's QR is a URL or a short structured string; this bounds
/// the scratch buffer so the pass needs no allocation of its own.
inline constexpr size_t QR_MAX_PAYLOAD = 512;

enum class QrStatus
{
    Ok,
    MissingGeometry,   ///< no width/height, or not a positive number
    BadEcc,            ///< data-qr-ecc was not one of L, M, Q, H
    PayloadTooLong,    ///< longer than QR_MAX_PAYLOAD
    EncodeFailed,      ///< the encoder refused the payload
    BoxTooSmall,       ///< box cannot hold modules + quiet zone at the floor
};

/// What went wrong, and the numbers a caller needs in order to say so usefully.
struct QrDiagnostic
{
    QrStatus status = QrStatus::Ok;
    Slice    payload;          ///< the data-qr value, as written
    Slice    ecc;              ///< the data-qr-ecc value, when that is the problem
    int      modules  = 0;     ///< side in modules, quiet zone excluded
    int      boxDots  = 0;     ///< the smaller of the declared width and height
    int      needDots = 0;     ///< the smallest box that would have worked
};

namespace detail
{

/// Parse an SVG coordinate: optional sign, digits, optional fraction. A unit
/// suffix is ignored - `px` is the only one that means anything at this scale,
/// and it means user units, which is what these already are.
inline bool ParseCoord(Slice s, double& out)
{
    if (s.Empty()) return false;

    const char*       p   = s.p;
    const char* const end = s.p + s.n;

    while (p < end && IsSpace(*p)) ++p;

    double sign = 1.0;
    if (p < end && (*p == '+' || *p == '-')) { if (*p == '-') sign = -1.0; ++p; }

    bool   any = false;
    double val = 0.0;
    while (p < end && *p >= '0' && *p <= '9') { val = val * 10.0 + (*p - '0'); ++p; any = true; }

    if (p < end && *p == '.')
    {
        ++p;
        double scale = 0.1;
        while (p < end && *p >= '0' && *p <= '9')
        {
            val += (*p - '0') * scale;
            scale *= 0.1;
            ++p;
            any = true;
        }
    }

    if (!any) return false;
    out = sign * val;
    return true;
}

/// Append a decimal integer. Every coordinate this pass emits is whole by
/// construction, so there is no number formatting here beyond this.
inline void PutInt(Sink& sink, long v)
{
    char  buf[24];
    char* p = buf + sizeof(buf);

    const bool         neg = v < 0;
    unsigned long long u   = neg ? 0ull - static_cast<unsigned long long>(v)
                                 : static_cast<unsigned long long>(v);
    do { *--p = static_cast<char>('0' + (u % 10)); u /= 10; } while (u);
    if (neg) *--p = '-';

    sink.Put(p, static_cast<size_t>(buf + sizeof(buf) - p));
}

/// floor() for a value already known to be finite and small. The pass works in
/// whole units, so this is the only rounding it does.
inline long FloorL(double v)
{
    const long t = static_cast<long>(v);
    return (v < 0.0 && static_cast<double>(t) != v) ? t - 1 : t;
}

/// Resolve `data-qr-ecc`. Absent is Medium; anything that is not L/M/Q/H is an
/// error rather than a silent fallback, because a label that quietly dropped to
/// a weaker level than it asked for is one nobody would notice.
inline bool ParseEcc(Slice s, QrEcc& out)
{
    if (s.p == nullptr) { out = QrEcc::Medium; return true; }   // not declared

    const char* p   = s.p;
    const char* end = s.p + s.n;
    while (p < end && IsSpace(*p)) ++p;
    while (end > p && IsSpace(end[-1])) --end;
    if (end - p != 1) return false;

    switch (*p)
    {
        case 'L': case 'l': out = QrEcc::Low;      return true;
        case 'M': case 'm': out = QrEcc::Medium;   return true;
        case 'Q': case 'q': out = QrEcc::Quartile; return true;
        case 'H': case 'h': out = QrEcc::High;     return true;
        default:            return false;
    }
}

}  // namespace detail

/// Copy the XML in [in, in+len) to `out`, replacing every <rect> that carries a
/// `data-qr` attribute with the SVG of that payload's QR code.
///
/// Returns the length the output needs. Call with `out` null (and `cap` 0) to
/// measure, then again with a buffer that size; the encoder is deterministic,
/// so both passes produce the same bytes. A result equal to `len` means there
/// was no placeholder and the input can be parsed as it stands.
///
/// On failure the return is 0 and `diag` says which placeholder failed and why.
/// Nothing is written past `cap`.
inline size_t ExpandQrCodes(const char* in, size_t len, char* out, size_t cap,
                            QrEncoder& enc, QrDiagnostic& diag)
{
    using namespace detail;

    diag = QrDiagnostic{};

    if (!in || len == 0) return 0;

    Sink sink{ out, cap };

    const char*       src = in;
    const char* const end = in + len;

    while (src < end)
    {
        if (*src != '<') { sink.Put(src, 1); ++src; continue; }

        const size_t avail = static_cast<size_t>(end - src);

        // Markup with no attributes and its own terminator, copied through
        // whole - so a placeholder written inside a comment stays a comment.
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
        const char* const afterTag   = tagEnd + 1;
        const bool        selfClosed = (tagEnd > src + 1 && tagEnd[-1] == '/');

        const char* nameStart = src + 1;
        const char* nameEnd   = nameStart;
        while (nameEnd < tagEnd && !IsSpace(*nameEnd) && *nameEnd != '/') ++nameEnd;
        const size_t nameLen = static_cast<size_t>(nameEnd - nameStart);

        const Slice payload = (src[1] != '/' && NameIs(nameStart, nameLen, "rect"))
                                  ? FindAttr(nameEnd, tagEnd, "data-qr")
                                  : Slice{};

        if (payload.p == nullptr)          // an ordinary element: copy it
        {
            sink.Put(src, static_cast<size_t>(afterTag - src));
            src = afterTag;
            continue;
        }

        // ---- a placeholder ----
        diag.payload = payload;

        const Slice eccSlice = FindAttr(nameEnd, tagEnd, "data-qr-ecc");
        QrEcc       ecc      = QrEcc::Medium;
        if (!ParseEcc(eccSlice, ecc))
        {
            diag.status = QrStatus::BadEcc;
            diag.ecc    = eccSlice;
            return 0;
        }

        double x = 0.0, y = 0.0, w = 0.0, h = 0.0;
        ParseCoord(FindAttr(nameEnd, tagEnd, "x"), x);     // absent means 0, as in SVG
        ParseCoord(FindAttr(nameEnd, tagEnd, "y"), y);
        if (!ParseCoord(FindAttr(nameEnd, tagEnd, "width"),  w) || w <= 0.0 ||
            !ParseCoord(FindAttr(nameEnd, tagEnd, "height"), h) || h <= 0.0)
        {
            diag.status = QrStatus::MissingGeometry;
            return 0;
        }

        // The payload as the file spells it is still escaped: DecodeCharData
        // runs over character data and copies markup through whole, so an
        // `&amp;` inside an attribute is still five characters here. Resolve it,
        // or every query string with two parameters encodes the wrong address.
        if (payload.n > QR_MAX_PAYLOAD)
        {
            diag.status = QrStatus::PayloadTooLong;
            return 0;
        }
        char text[QR_MAX_PAYLOAD + 1];
        memcpy(text, payload.p, payload.n);
        const size_t textLen = xml::DecodeCharData(text, payload.n);
        text[textLen] = '\0';

        const int modules = enc.Encode(text, textLen, ecc);
        if (modules <= 0)
        {
            diag.status = QrStatus::EncodeFailed;
            return 0;
        }
        diag.modules = modules;

        const int  side       = modules + 2 * QR_QUIET_MODULES;
        const long boxDots    = FloorL(w < h ? w : h);
        const long moduleDots = boxDots / side;

        if (moduleDots < QR_MIN_MODULE_DOTS)
        {
            diag.status   = QrStatus::BoxTooSmall;
            diag.boxDots  = static_cast<int>(boxDots);
            diag.needDots = side * QR_MIN_MODULE_DOTS;
            return 0;
        }

        const long extent = side * moduleDots;
        const long ox     = FloorL(x + (w - static_cast<double>(extent)) / 2.0);
        const long oy     = FloorL(y + (h - static_cast<double>(extent)) / 2.0);
        const long mx     = ox + QR_QUIET_MODULES * moduleDots;
        const long my     = oy + QR_QUIET_MODULES * moduleDots;

        // The quiet zone is painted, not merely left empty, so the code keeps
        // its margin over whatever it was placed on.
        sink.Put("<g><rect x=\"");
        PutInt(sink, ox);
        sink.Put("\" y=\"");
        PutInt(sink, oy);
        sink.Put("\" width=\"");
        PutInt(sink, extent);
        sink.Put("\" height=\"");
        PutInt(sink, extent);
        sink.Put("\" fill=\"#ffffff\"/><path fill=\"#000000\" d=\"");

        // One subpath per horizontal RUN of dark modules, not per module: a QR
        // is mostly horizontal pairs and triples, and this buffer is the one
        // ThorVG then has to parse.
        for (int row = 0; row < modules; ++row)
        {
            int col = 0;
            while (col < modules)
            {
                if (!enc.Module(col, row)) { ++col; continue; }

                int run = 1;
                while (col + run < modules && enc.Module(col + run, row)) ++run;

                sink.Put("M");
                PutInt(sink, mx + col * moduleDots);
                sink.Put(" ");
                PutInt(sink, my + row * moduleDots);
                sink.Put("h");
                PutInt(sink, run * moduleDots);
                sink.Put("v");
                PutInt(sink, moduleDots);
                sink.Put("h-");
                PutInt(sink, run * moduleDots);
                sink.Put("z");

                col += run;
            }
        }

        sink.Put("\"/></g>");

        src = afterTag;

        // `<rect ...>` without the slash is not a valid shape element, but it
        // costs one branch to swallow the `</rect>` that must then follow,
        // rather than leave an orphan closing tag in what ThorVG parses.
        if (!selfClosed)
        {
            const char* p = src;
            while (p + 7 <= end && memcmp(p, "</rect>", 7) != 0) ++p;
            if (p + 7 <= end) src = p + 7;
        }
    }

    return sink.n;
}

}  // namespace svg
