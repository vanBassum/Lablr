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
