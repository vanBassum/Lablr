# Next up

**Active work only.** Rewritten constantly, kept tiny, and an item is *removed* when it
lands or is dropped — never ticked off in place. Everything else lives in
GitHub issues (work for later) or `docs/reasoning/` (why things are the way they are).
If a fact wants to survive, it does not belong in this file.

Last updated 2026-09-18.

## Now

**The rendering path is proved on hardware, end to end.** `fs write` → SVG on FAT →
ThorVG → ARGB bitmap → command reply, driven both from a script and from the device's
own Files and Render pages. Geometry is pixel-perfect; `<text>` renders with a TrueType
font read from `/fonts`. A 400×200 render costs 320 KB of PSRAM (the canvas itself),
~25 KB of internal heap, leaves ~11.5 KB of the worker's 16 KB stack, and takes ~780 ms
round trip including shipping 313 KB over the WebSocket.

**Outstanding: the ThorVG fork is not made yet.** Font support needs one token removed
from the component (`-D__linux__`), agreed to be carried as a fork of
`espressif/idf-extra-components`. Until that fork exists, `main/idf_component.yml` still
points at the registry and the build refuses with instructions; the bench works because
this machine's `managed_components/` copy is patched by hand, which a clean clone will
not be. The stanza to switch to is in that file, and it is worth a PR upstream.

**Outstanding: printing does not exist.** No USB host, no DYMO protocol. `DeviceDoc`
says so explicitly, because a model that assumes otherwise will tell someone a label was
printed.

**Outstanding: `/media` is empty.** Render sizes are given as explicit pixel width and
height. Media definitions — physical size, DPI — are the next design decision, and the
render command's arguments are where they will land.

**Outstanding: the bench font is Verdana.** Copied off this Windows machine to prove the
path; it is not redistributable and is not in the repo. Anything shipped wants an open
font (DejaVu, Liberation, Noto).

**Worth backporting to Strux:** the WebSocket inbound-frame fix. The local transport read
frames into a 512-byte stack buffer while declaring `INBOUND_WINDOW = 4096`, so any frame
of 512 bytes or more dropped the client — which also means `partition write` from the
browser over the LAN has been broken. Same fix, in `main/strux/WebServerManager/`.

**Settled, and not revisited without a measurement:** the S3-only board
(`esp32s3_n16r8`, 16 MB flash, 8 MB octal PSRAM, verified at boot on MAC
80:b5:4e:db:47:18), the 3 MB + 3 MB OTA plus 9.875 MB `storage` flash map, and no LED —
`MockLed` is permanent, because a label printer has nothing to indicate.
