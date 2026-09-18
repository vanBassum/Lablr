# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-09-18.

## Now

**Synced to Strux `050a4b6`.** This repository is still the template plus a name: the
only files that differ from upstream are `README.md`, `CLAUDE.md`, `CMakeLists.txt`
(`project(Lablr)`), `.github/workflows/release.yml`, `frontend/src/config.ts` and this
file. A sync is therefore a re-copy of every tracked file except those, then re-applying
the identity. `main/strux/` is never edited here; product code goes in `main/app/`, which
today still holds Strux's LED demo.

**The www partition is gone, which is what the flash plan was waiting for.** Upstream
packs the built frontend into one blob linked into the app image
(`main/strux/WebAssets/`), so `partitions.csv` is now nvs + otadata + phy + two OTA slots
that fill 4 MB exactly. The intent here is the opposite of upstream's: on a 16 MB ESP32-S3,
give the OTA slots what they need and make **the rest a FAT partition** for `/labels`,
`/media` and `/fonts`. Three things that removal took with it and this will need back:
`fatfs` and `wear_levelling` in `COMPONENT_REQUIRES`, and `CONFIG_FATFS_LFN_HEAP` /
`CONFIG_FATFS_MAX_LFN` in the defaults.

**Outstanding: the flash-size assertion will refuse the board that needs it.** The root
`sdkconfig.defaults` asserts `CONFIG_ESPTOOLPY_FLASHSIZE_4MB=y`, and the drift guard in
`CMakeLists.txt` walks *every* composed defaults file and fails the build naming any line
that did not take. A board overlay selecting 16 MB makes the root's line lose, so the
assertion has to move into the per-board overlays before an S3 board can exist. The same
applies to `CONFIG_PARTITION_TABLE_CUSTOM_FILENAME` if the S3 wants its own layout.

**Outstanding: no board, no product code.** Boards are `esp32_devkit` and
`esp32c3_supermini`. The product is a Strux-connected DYMO LabelWriter: ESP32-S3 driving
the printer over USB host, SVG labels on FAT rendered on-device (ThorVG is the candidate)
to a monochrome bitmap, with a render/preview command that returns the bitmap without
printing. ThorVG and the USB host component go in `main/idf_component.yml` — a board
fragment cannot add REQUIRES.

**Outstanding: not built here.** No `idf.py set-target`, no `pnpm install` in this
directory since the re-base.
