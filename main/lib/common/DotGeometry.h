#pragma once

#include <cstdint>

// --------------------------------------------------------------
// Micrometres to printer dots, and the one subtraction that decides how much of
// a label can actually be printed.
//
// It lives here, naming no layer, because three things need it and none of them
// owns it: the PRINTER (its dead zone in dots), the MEDIUM (its size and its
// own extra margin), and the PRINT path (where the artwork lands). Before this
// the conversion was a static on MediaManager and the printable rule was a
// method beside it, which put the machine's arithmetic inside the paper's
// class.
//
// The rule the whole calibration model rests on:
//
//     printable = size - printerDead - mediumMargin
//
// and ALIGNMENT IS NOT IN IT. Where the artwork is placed is a separate
// question from how much of the label can carry ink, and the old model's single
// signed offset answered both at once - so nudging artwork down reported a
// shorter printable label, as though the paper had changed.
// --------------------------------------------------------------

namespace dots
{

/// Micrometres to dots at `dpi`, rounded to nearest, symmetric about zero.
/// 25400 micrometres to the inch.
inline int32_t FromUm(int32_t um, uint32_t dpi)
{
    const int64_t n = static_cast<int64_t>(um) * dpi;
    return static_cast<int32_t>((n >= 0 ? n + 12700 : n - 12700) / 25400);
}

/// Dots to micrometres, the same rounding in reverse. For reporting a measured
/// dot count back as the unit a medium is stored in.
inline int32_t ToUm(int32_t dotCount, uint32_t dpi)
{
    if (dpi == 0) return 0;
    const int64_t n = static_cast<int64_t>(dotCount) * 25400;
    const int64_t d = static_cast<int64_t>(dpi);
    return static_cast<int32_t>((n >= 0 ? n + d / 2 : n - d / 2) / d);
}

/// How much of one axis can actually receive ink, in dots.
///
/// `deadUm` is the machine's, `marginUm` is this roll's own extra. Both are
/// amounts of paper: a negative one is treated as zero rather than allowed to
/// grow the label, because nothing can make a printer reach past the edge.
/// Never returns less than zero - a dead zone larger than the label means
/// nothing prints, not that a negative amount does.
inline int32_t Printable(int32_t sizeUm, int32_t deadUm, int32_t marginUm, uint32_t dpi)
{
    const int32_t size   = FromUm(sizeUm, dpi);
    const int32_t dead   = deadUm   > 0 ? FromUm(deadUm,   dpi) : 0;
    const int32_t margin = marginUm > 0 ? FromUm(marginUm, dpi) : 0;
    const int32_t lost   = dead + margin;
    return size > lost ? size - lost : 0;
}


/// Everything a label contributes to its own geometry, in micrometres.
///
/// `legacy` is the compatibility hinge. A medium written before the printer and
/// the paper were told apart carries ONE pair of signed offsets that meant both
/// "put the artwork here" and "this much is unreachable". Those files are still
/// on devices, so their offsets are taken as the finished placement, verbatim,
/// and their printable area comes from the printer's dead zone instead - which
/// for the roll those numbers were measured on is the same answer to the dot.
/// Rewriting such a medium through `media set` moves it to alignXUm/alignYUm
/// and the flag goes away.
struct LabelSpec
{
    int32_t widthUm       = 0;
    int32_t heightUm      = 0;
    int32_t marginLeftUm  = 0;   ///< this roll's OWN extra dead strip
    int32_t marginTopUm   = 0;
    int32_t alignXUm      = 0;   ///< placement only; never changes printable
    int32_t alignYUm      = 0;
    bool    legacy        = false;
    int32_t legacyOffsetXUm = 0;
    int32_t legacyOffsetYUm = 0;
};

/// The same numbers in dots, which is what a job and a preview both need.
struct LabelGeometry
{
    int32_t widthDots  = 0;
    int32_t heightDots = 0;
    int32_t printableWidthDots  = 0;
    int32_t printableHeightDots = 0;
    /// Head column and raster line the artwork's top-left corner goes to.
    /// Negative means that much of it falls where the machine cannot print,
    /// which is the normal state of affairs and not an error.
    int32_t placeXDots = 0;
    int32_t placeYDots = 0;
    int32_t deadLeftDots   = 0;  ///< the machine's
    int32_t deadTopDots    = 0;
    int32_t marginLeftDots = 0;  ///< this roll's
    int32_t marginTopDots  = 0;
};

/// The whole calibration model in one function, so a preview and a print cannot
/// disagree about it.
///
///     printable = size - printerDead - mediumMargin
///     placement = align - printerDead - mediumMargin
///
/// Alignment appears in the second and NOT the first: moving artwork must never
/// change how much label there is to print on. With alignment at zero the
/// artwork's top-left lands exactly at the first dot the machine can reach,
/// which is what an uncalibrated medium should do.
inline void Resolve(const LabelSpec& spec, int32_t deadLeftUm, int32_t deadTopUm,
                    uint32_t dpi, LabelGeometry& out)
{
    out = LabelGeometry{};

    out.widthDots  = FromUm(spec.widthUm,  dpi);
    out.heightDots = FromUm(spec.heightUm, dpi);

    out.deadLeftDots   = deadLeftUm       > 0 ? FromUm(deadLeftUm,       dpi) : 0;
    out.deadTopDots    = deadTopUm        > 0 ? FromUm(deadTopUm,        dpi) : 0;
    out.marginLeftDots = spec.marginLeftUm > 0 ? FromUm(spec.marginLeftUm, dpi) : 0;
    out.marginTopDots  = spec.marginTopUm  > 0 ? FromUm(spec.marginTopUm,  dpi) : 0;

    out.printableWidthDots  = Printable(spec.widthUm,  deadLeftUm, spec.marginLeftUm, dpi);
    out.printableHeightDots = Printable(spec.heightUm, deadTopUm,  spec.marginTopUm,  dpi);

    if (spec.legacy)
    {
        // Taken as-is: these ARE the placement this medium has always produced,
        // and reproducing it exactly is the whole point of keeping them.
        out.placeXDots = FromUm(spec.legacyOffsetXUm, dpi);
        out.placeYDots = FromUm(spec.legacyOffsetYUm, dpi);
    }
    else
    {
        out.placeXDots = FromUm(spec.alignXUm, dpi) - out.deadLeftDots - out.marginLeftDots;
        out.placeYDots = FromUm(spec.alignYUm, dpi) - out.deadTopDots  - out.marginTopDots;
    }
}

}  // namespace dots
