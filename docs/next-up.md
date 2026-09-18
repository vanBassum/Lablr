# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-09-18.

## Now

**A label prints.** SVG on FAT → ThorVG → threshold → LabelWriter raster → USB bulk OUT →
paper, driven by `print svg` and by the Render page's Print button, which calls that same
command. A 25 × 25 mm label at 295 × 295 dots costs 164 ms to render, 31 ms to convert and
785 ms to send; the job is 25,186 bytes with 14,296 dots of ink. The rendering path is
unchanged and shared — `render svg` and `print svg` call one `RenderManager::Render`, so
the preview predicts the print by construction rather than by agreement.

**What the printer is, from the printer.** DYMO LabelWriter 450, `0922:0020` rev 0112,
serial 16031114352460. One interface, class 07/01/02 (printer, bidirectional), EP OUT
0x02 and EP IN 0x82, both bulk, both 64-byte MPS. `usb status` reports all of it plus
GET_PORT_STATUS, so paper-out comes from the printer rather than from a guess.

**Its IEEE-1284 `CMD:` field is empty**, which is the one fact that would have named the
raster dialect: `MFG:DYMO;CMD: ;MDL:LabelWriter 450;CLASS:PRINTER;...`. So the ESC
language is not discoverable from the device, and a second model cannot be supported by
asking it what it speaks.

**Outstanding, and the whole point of the next phase: the printable area is not known.**
The first label came out almost right — frame, both fonts, all four text runs — but its
right and bottom border sit on or past the label's edge. 295 dots ≈ 25.0 mm at 300 DPI
landed close enough to confirm 300 DPI on both axes, so what is missing is not the
resolution but the *margins*: where dot 0 sits relative to the label's leading edge and
its left edge, and how many dots of the 295 are actually reachable. The old C# config
recorded `offsetCorrectionMm: {x: 0, y: -5}` for both rolls it knew, which says the answer
is non-zero and was found by measurement before. `print svg` has `offsetX` but **no
vertical offset at all** — that is a known gap, deliberately not filled by guessing.

**Outstanding: `/media` is still empty, and now has facts to be built from.** Geometry is
supplied by the caller in printer dots. `headWidth` defaults to 672 (the 300 DPI head) and
is an argument because the head prints from its own left edge — a 295-dot label placed at
offset 0 leaves 377 dots of head hanging off the paper, which is why the full-width test
pattern ran off the label.

**Outstanding: the head is addressed at full width for every job.** `bytesPerLine` is 84
whatever the label's width, so a 25 mm label ships 84 bytes per line to use 37 of them.
Narrowing `ESC D` to the label is an obvious saving and is unproven on this printer.

**Settled, and not revisited without a measurement:** the S3-only board
(`esp32s3_n16r8`, 16 MB flash, 8 MB octal PSRAM, verified at boot on MAC
80:b5:4e:db:47:18), the 3 MB + 3 MB OTA plus 9.875 MB `storage` flash map, no LED, and
the printer on the S3's native USB pins with **5 V fed to VBUS from outside** — the board
cannot source it, and with no VBUS the printer never attaches its pull-up and the host
enumerates nothing at all.
