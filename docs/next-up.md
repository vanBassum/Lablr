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

**Outstanding: the LED role is a `MockLed`.** Nobody has said which pin, if any, carries
an LED on this board, and the DevKitC-1's is an addressable WS2812 that would need a
driver this repository does not have. The role is bound so the demo runs and nothing
lights up. One line in `BoardContext.h` plus two constants in `BoardConfig.h` fixes it
once the board is known.

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

**Outstanding: nothing has been on hardware.** The build is green and the flash map is
what the table says; no S3 has been flashed from this tree.
