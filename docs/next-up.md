# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-09-20.

## Now

**The device describes itself well enough for an agent that has never seen this
repository.** `system describe` carries the conceptual model — labels are complete SVGs,
media are physical stock, render is free and print is not — and `help describe` carries
every command and argument. The two were tested by driving the whole workflow from the
device's own words: `media list` → `fs read` a reference → `fs write` a new SVG →
`render svg` at the medium's dot geometry, and the label came out right. What was wrong
was **stale**, not missing: the filesystem section still called `/media` "empty for now",
contradicting the media section below it, and `render svg -width` still promised that
media definitions would supply the size "later".

**The UI is the product's own now.** Print is the home page; Strux's LED example is gone
from `main/app/`, from the nav and from `backend.ts`. `MockLed` stays bound in
`BoardContext` because `Led` is still a role every Strux board owes — that is a template
decision, not this product's.

**Outstanding: two fixes built but never flashed.** The S3 was not on the bench when
they were written, so both are compile-verified only. (1) `xml::DecodeCharData` resolves
character references before ThorVG parses, because ThorVG resolves none - a label
written `AT&amp;T` printed the entity. Host tests cover the function; what is unchecked
is that a real label draws `&`. (2) The Print page's preview asks for `format: "png"`
instead of the device's raw ARGB, which was three quarters of a megabyte down a
512-byte session window. `check_amp.py` (written to the session scratchpad, not kept)
drives both over the wire; the ampersand check is decisive on its own, because a
`&amp;` render and a CDATA `&` render must come back byte-identical.

**Outstanding: ThorVG moved 1.1.0 -> 1.1.2 and no label has been printed since.** The
forked component is gone - the one-token `-D__linux__` fix landed upstream and shipped as
registry 1.1.2 - but that release bumps the renderer itself, and the last three commits
here were all ThorVG text quirks. The build is clean; what is unchecked is that text still
draws at all (the fix's whole purpose) and that the entity and font-size fixes still hold.
The same flash settles this and the two fixes above.

**Outstanding: the browser Print button is still the one path never physically
exercised.** Everything it calls has been driven from a script and works, and the page
around it is new. It is one click on the Print page.

**Outstanding: `roll54x70`'s offsets look invented.** −1100 and −5000 µm against
`square25`'s measured −1016 and −3133. Round numbers are what a guess looks like, and the
device's own instructions now say not to do this. Either `print calibrate` on that roll
and measure them, or set them to zero and say so.

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

**Settled, and not revisited without a measurement:** the S3-only board
(`esp32s3_n16r8`, 16 MB flash, 8 MB octal PSRAM, MAC 80:b5:4e:db:47:18), the 3 MB + 3 MB
OTA plus 9.875 MB `storage` flash map, no LED, the printer on the S3's native USB pins
with **5 V fed to VBUS from outside**, 300 DPI and a 672-dot head, the raster narrowed
to `ceil((offsetX + width) / 8)` — the far end only, because `ESC D` is a count from head
column 0 and cannot be given an origin — and `square25`'s measured offsets
(−1016, −3133 µm; printable 283 × 258 of 295 × 295 dots).
