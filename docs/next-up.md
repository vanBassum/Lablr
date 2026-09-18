# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-09-18.

## Now

**Media is data, and the 25 × 25 mm stock is calibrated.** `/media` holds one JSON
object per roll, written at runtime through `media list/get/set/delete`; `print svg -media
square25 -path /labels/bc547.svg` is the whole call and a correctly positioned label comes
out. The schema is five fields — `name`, `widthUm`, `heightUm`, `offsetXUm`, `offsetYUm` —
in integer micrometres, with no `dpi` and no head width, because those belong to the
printer and `print status` reports them.

**What the paper measured, on this printer with this stock:**

| Quantity | Measured |
| --- | --- |
| `offsetXUm` | −1016 (−12 dots) — head column 0 is 1.0 mm *inside* the label's left edge |
| `offsetYUm` | −3133 (−37 dots) — raster line 0 lands 3.13 mm past the leading edge |
| Printable | 283 × 258 of the label's 295 × 295 dots |
| One label | 36 bytes/line, 258 lines, 9,657-byte job — against 25,186 uncalibrated at full head width |

Both axes lose a margin, which is why `printableWidthDots`/`printableHeightDots` are
reported — **derived from the offsets, never stored.** A design has to keep its content
clear of them; `/labels/bc547.svg` does.

**Outstanding: the browser Print button is still the one path never physically
exercised.** Everything it calls has been driven from a script and works. It is one click
on the Render page after selecting the medium.

**Outstanding: is the vertical offset actually a constant?** Every measurement of it so
far followed a job that *overran* the label — the 400-line rulers on a 295-dot label, whose
tails are visible at the top of each calibration photo. If raster line 0 depends on the
previous job's length rather than on the media, it is not a per-medium property and the
schema needs rethinking. Two consecutive same-length prints settle it. The legacy C#
carried a fixed −5 mm for every roll, which is weak evidence that it is constant — and
weak evidence is what it is, since the measured value here is −3.13 mm.

**Outstanding: the calibrated label is good, not perfect.** Deliberately parked rather
than chased; the residual is small and the next real information comes from a second roll,
not from another decimal place on this one.

**Outstanding: only one medium exists.** Adding label formats and anything
AI-facing is the next phase, on purpose.

**Settled, and not revisited without a measurement:** the S3-only board
(`esp32s3_n16r8`, 16 MB flash, 8 MB octal PSRAM, MAC 80:b5:4e:db:47:18), the 3 MB + 3 MB
OTA plus 9.875 MB `storage` flash map, no LED, the printer on the S3's native USB pins
with **5 V fed to VBUS from outside**, 300 DPI and a 672-dot head, and the raster narrowed
to `ceil((offsetX + width) / 8)` — the far end only, because `ESC D` is a count from head
column 0 and cannot be given an origin.
