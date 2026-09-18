# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-09-18.

## Now

**This firmware is ESP32-S3 only, and the flash map is settled.** `esp32s3_n16r8` is the
one board: 16 MB flash, 8 MB octal PSRAM at 80 MHz. The template's `esp32_devkit` and
`esp32c3_supermini` are deleted — 4 MB parts with no PSRAM, which nothing this product
does will fit on. Flash size and partition table are now stated by the board's
`sdkconfig.defaults` rather than asserted at the root, which is what made a 16 MB board
possible at all; the drift guard is untouched and now names the board overlay when a line
does not take. Builds green at 1,273,696 bytes (1.21 MiB), 60% of a slot free.

**Settled: there is no LED, and that is the answer.** The `Led` role binds `MockLed`
permanently. This product drives a label printer; the only thing an indicator would
report is a link state the printer's own commands already answer, and writing a WS2812
driver to light up a demo that gets deleted with `LedManager` would be work spent on the
copy rather than the product.

**Settled: the flash map is the baseline.** 3 MB + 3 MB OTA and a 9.875 MB `storage`
area, agreed 2026-09-18. Not revisited without a measurement.

**Outstanding: `storage` is reserved, not live.** 9.875 MB at 0x620000, declared FAT in
the table so the OTA slots cannot grow into it, but nothing mounts it, formats it or
knows it exists — `fatfs` and `wear_levelling` are deliberately still out of
`COMPONENT_REQUIRES`. Mounting it is the next piece, and with it the `/labels`, `/media`
and `/fonts` layout.

**Outstanding: no product code.** `main/app/` still holds Strux's LED demo. The product is
a Strux-connected DYMO LabelWriter: the S3 drives the printer over USB host, SVG labels on
the FAT partition are rendered on-device (ThorVG is the candidate) to a monochrome bitmap,
and a render/preview command returns that bitmap without printing. ThorVG and the USB host
component go in `main/idf_component.yml` — a board fragment cannot add REQUIRES.

**Outstanding: octal PSRAM is configured but UNVERIFIED.** `CONFIG_SPIRAM_MODE_OCT` is
the one setting here that a green build says nothing about: quad instead of octal, or a
module that is not really an R8, boots fine and just finds less PSRAM, or none. It is
confirmed when a real S3 boots this image and the log reports the full 8 MB - and not
before.
