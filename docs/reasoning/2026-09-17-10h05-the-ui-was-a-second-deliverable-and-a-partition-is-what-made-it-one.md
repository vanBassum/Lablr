---
id: 2026-09-17-10h05
date: 2026-09-17
time: "10:05"
title: The UI was a second deliverable, and a partition is what made it one
---

**Before:** the built frontend was gzipped into `www/`, turned into a FAT image, and
flashed to a `www` data partition of its own. `WebServerManager` mounted it at `/www`
with wear levelling, `StaticFileHandler` resolved a URI to a path and `fopen`'d it, and
`UpdateManager` marked that partition uploadable so the UI could be replaced without
touching the app. That independence was the stated benefit, and it is real: a 143 KB
page change did not have to move a 1.1 MB image.

**What it cost was never priced.** A partition makes the UI a *second deliverable*, and
two deliverables can disagree. A device's page and the commands that page calls are
versioned separately, with nothing anywhere that says which pairs — and with the module
mechanism gone (`2026-09-15-12h30`) a page now knows device commands **by name**, so a
`www` image from a different firmware is not a cosmetic mismatch but a page calling
commands that may not exist. Nothing reports that; the page simply misbehaves. The
Cache-Control comment in `StaticFileHandler.cpp` is a fossil of the same seam from the
other side: a stale `/index.html` after a www-only OTA, silent, with nothing broken and
nothing saying so.

**The delta:** independent UI update is not a feature of this product, it is a
consequence of where the bytes were put. Nothing ever shipped a UI-only update — the
`www` slot existed because a FAT image is how one embeds a directory, and the
independence came along with it uninvited. Naming it as a cost rather than a benefit is
what makes the answer obvious: link the bundle into the app image, and a firmware image
becomes the whole product again. Version skew between page and commands stops being
possible rather than being detected.

The gateway firmware (`KC1245 Gateway workspace/esp_gateway`) had already solved the
mechanics, and the one non-obvious part is why it is a *blob* rather than one
`EMBED_FILES` entry per asset: vite content-hashes its output, so the file list is not
knowable when CMake configures, which is exactly when `EMBED_FILES` needs it. A single
fixed name (`web_assets.bin`) makes the CMake input a constant, and the blob's directory
is the manifest the reader needs — so the frontend may split chunks, add fonts and
rename everything on every build without touching C++ or CMake.

What came with the move, none of it planned:

- **The filesystem is gone, so the hazards of one are too.** No mount, no wear-levelling
  layer, no `fatfs`/`wear_levelling` components, no `CONFIG_FATFS_*`. `Resolve` dropped
  its path-traversal check and nothing is missing: the lookup is an exact name match
  against the blob's directory, so `../` matches nothing. It was a filesystem hazard and
  there is no filesystem.
- **No read buffer anywhere.** The bytes are flash-mapped rodata, so the HTTP route is
  one `httpd_resp_send` (with a real Content-Length instead of a chunked response) and
  `web read` is one `ctx.out.write(file.data, file.size)` that `Session::write` splits
  across the session window itself. The 512-byte `fread` loops on both sides were
  copying flash into RAM and back out.
- **896 KB returned to the OTA slots**, which now fill the 4 MB flash exactly at
  0x1F0000 each. The app grew by 109 KB — the blob, less the `fatfs` code it no longer
  links — to 0x1312F0, leaving 38% of a slot free.

The price is paid honestly: a UI change is an app OTA, and changing the partition table
means an existing device needs a full flash rather than an update. Both are one-time,
and the second is the ordinary cost of any partition change.

Related: `2026-09-15-12h30-a-mechanism-with-no-second-consumer-is-a-seam-the-template-pays-for`
(builds-on — that note removed the seam between shell and page; this one removes the
seam between page and firmware).
