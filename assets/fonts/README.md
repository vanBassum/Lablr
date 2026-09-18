# Fonts

Fonts are **not** part of the firmware image. `RenderManager` reads `/fonts` off the
FAT storage partition at boot and registers each file with ThorVG under **its filename
without the extension** — so `DejaVuSans.ttf` is `font-family="DejaVuSans"` and
`DejaVuSans-Bold.ttf` is `font-family="DejaVuSans-Bold"`. There is no fallback and no
family/weight matching: a `font-family` ThorVG does not know renders *nothing*, silently,
while the shapes around the text still draw. `render fonts` is what tells you which names
the device actually has.

These two are checked in so a fresh device can be brought to a known state. Push them
with `fs write`:

    fs write -path /fonts/DejaVuSans.ttf        (file as the request body)
    fs write -path /fonts/DejaVuSans-Bold.ttf

## What is here, and why these

DejaVu Sans, regular and bold (2.37). Chosen over the Verdana copied off a Windows
machine during bring-up, which proved the path and could never ship. DejaVu is freely
redistributable (see `LICENSE`, a Bitstream Vera / Public-domain-addition pair), and its
glyph coverage is the reason to pay the ~1.4 MB: degree signs, micro, arrows and the rest
of what a lab label asks for are present rather than missing-and-invisible.

Bold is here as well as regular because "the title didn't print" is the failure mode
otherwise, and it is a silent one.

The other twenty-odd DejaVu faces are deliberately not checked in. Anything else a
product wants goes on the device the same way, without a firmware change.
