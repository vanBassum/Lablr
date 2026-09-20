# Calibration and example labels

These are the exact SVGs the media offsets were measured with. They are checked in
so a calibration can be repeated rather than remembered, and they go on a device
with `fs write` to `/labels/`.

An SVG here is in **printer dots**, not millimetres, and its size is chosen
against a medium: `print svg -media <id>` renders the design into the medium's dot
size preserving aspect, so a design that is *not* the medium's exact dot size gets
scaled, and any ruler drawn on it stops reading true. That is the difference
between the two calibration files below.

| File | Size | For |
|---|---|---|
| `cal54x70-overrun.svg` | 672 x 900 | Finding an unknown offset on 54 x 70 mm stock |
| `cal54x70-fit.svg` | 638 x 827 | Confirming one, at scale 1 |
| `bc547-square25.svg` | 295 x 295 | An ordinary label for the 25 x 25 mm stock |
| `thorvg-text-check.svg` | 638 x 827 | Checking the two text fixes after a ThorVG change |
| `qr-square25.svg` | 295 x 295 | A QR label for the 25 x 25 mm stock, and the placeholder's worked example |

## How the calibration designs work

Both carry a millimetre ruler along the top (across the head) and down the left
(along the feed), numbered every 5 mm, plus the design's own boundary rectangle
and a centre cross.

**The overrun design is deliberately bigger than the label** - 672 dots wide is
the full head, and 900 dots is 76 mm against a 70 mm label. That is the whole
technique: the paper's own edge cuts the ruler at a number you can *read*, instead
of leaving you to measure a small margin with a ruler and a steady hand. Two
readings come off one label:

- the number the **right** edge cuts, giving `offsetX`
- the number the **trailing** edge cuts, giving `offsetY`

It costs an extra label per print, because the overrun lands on the next one.

**The fit design is exactly the medium's dot size**, so `print svg -media` renders
it at scale 1 and its ruler reads true in label millimetres. Calibrated, its
bottom-right corner marks land on the paper's bottom-right corner and the rulers
read the label's real size at the far edges. Its top and left strokes are
*expected* to be missing: that is the unprintable margin, and seeing it absent is
a confirmation rather than a defect.

## The text check

`thorvg-text-check.svg` exists because ThorVG's SVG loader gets two things wrong
that this firmware corrects *before* the parser sees the buffer - inherited font
properties ([SvgFontAttrs.h](../../main/lib/common/SvgFontAttrs.h)) and character
references ([XmlEntities.h](../../main/lib/common/XmlEntities.h)) - and both fail
**silently**: the label simply comes out with the wrong size or the entity spelled
out, and nothing anywhere says so. Render this sheet after changing either pass, or
after moving ThorVG's version, and read it.

Rendering is enough; it does not have to be printed. `render svg` at 638 x 827 shows
everything the paper would.

**Section 1 is five pairs, and a pair that does not match is the bug.** The left
column asks for its font through inheritance, the right column spells the same font
out on the element itself, and both draw `Hxp 34`. If the inheritance pass regressed,
the left column comes out at ThorVG's default 10 - a quarter of the height, unmissable
next to its own reference. Row e also inherits `DejaVuSans-Bold`, so a family that
failed to inherit resolves to nothing and that line is *blank* rather than small.

**Section 2 is five results, each under the source text that produced it.** A caption
is escaped one level, so it prints what the file literally contains:

| Row | The test line contains | Must print |
|---|---|---|
| f | `AT&amp;T` | `AT&T` |
| g | `&#65;&#66;&#67;` | `ABC` |
| h | `25&#176;C` and `10&#x3A9;` | `25°C` and `10Ω` |
| i | `&amp;lt;` | `&lt;` - four characters, **not** `<` |
| j | `&lt;` | `&lt;` - four characters |

Rows i and j are the two that look wrong and are not. Row i is the decoder's one-pass
property: it resolves left to right and never rescans what it wrote, so `&amp;lt;`
becomes the four characters `&lt;` and is not then read as `<`. Row j is the deliberate
residue - `&lt;` is left alone, because putting a `<` into character data would make
ThorVG parse the rest of the file as markup, which destroys the label instead of
printing one wrong character. So i and j print identically, and that is the pass
working.

Every caption and heading on the sheet carries its own `font-family` and `font-size`,
so the page stays readable even when the thing it is testing is broken.

## The QR label

`qr-square25.svg` is what a QR label looks like in this system, and the shortest
possible answer to "how do I put a QR code on a label": you do not draw one. The
file contains a payload and a box,

```xml
<rect x="47" y="24" width="200" height="200"
      data-qr="https://parts.local/bc547" data-qr-ecc="M"/>
```

and the device encodes it while rendering
([SvgQrCode.h](../../main/lib/common/SvgQrCode.h)). There is no module matrix in
the file, which is why the URL is still readable in it - and re-readable by
anything that opens the label later.

What to check when you print it: that a phone scans it, and that the code has
white space all round it. The quiet zone lives *inside* the 200-dot box, so the
declared rectangle is the footprint of the whole code and nothing else belongs
in it.

Sizing, for a label of your own: the shorter side of the box must be at least
`(modules + 8) * 3` dots. The payload above is 25 modules, so 33 with its quiet
zone, at 6 dots each - 198 of the 200 declared, leaving one dot of margin on each
side. Ask for a box too small and the render fails and names the size that would
have worked; it never shrinks the modules below 3 dots, because a code that
prints and does not scan is worse than one that does not print.
