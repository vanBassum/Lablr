---
id: 2026-09-20-15h10
date: 2026-09-20
time: "15:10"
title: The pre-parse seam is a dialect, not a pile of workarounds
---

**Reframing what two existing passes are.** `xml::DecodeCharData` and
`svg::PushDownFontAttrs` were each written as a repair: ThorVG resolves no character
reference, ThorVG inherits no font property, so rewrite the buffer before it parses and
the label comes out right. Two repairs at the same seam looked like a coincidence of two
ThorVG bugs, and the natural expectation was that both would eventually be deleted -
upstream fixes it, the pass goes away, the seam closes.

**Adding QR support is what showed the seam is the wrong thing to call temporary.** A QR
code cannot come from ThorVG and never will: ThorVG is a vector renderer and a QR is an
error-correcting code, so there is no upstream fix to wait for. It cannot come from the
label author either, which is the part that took the longest to see clearly. The author
is increasingly a model composing an SVG through the relay, and a QR matrix is not
something that can be written by anything except a real encoder - Reed-Solomon parity
over GF(256), interleaved codewords, a fixed serpentine layout, a mask chosen by penalty
score. A model can produce a grid of squares that looks exactly like a QR code and is
not one.

**And the failure mode is what settles where the work belongs.** A wrong matrix is not
usually an unreadable one. The dangerous case is the code that scans perfectly, to a
subtly different address, on a label that is already stuck to a bottle - a failure with
no moment at which anybody looks at it and thinks something is wrong. Compare that with
the two older passes: a missing `&`, text at a quarter size. Those are visible. This one
is not, so the encoding has to happen somewhere it cannot be got wrong, which means on
the device, from the payload, every render.

**So the seam is not a repair site, it is the device's own SVG dialect** - a small set of
things a label may say that plain SVG has no way to express, resolved into plain SVG
before anything downstream is involved. `<rect data-qr="...">` is a first-class feature
that will still be there after every ThorVG bug is fixed. What the three passes share is
not "ThorVG is wrong"; it is that each turns something an author can reasonably write
into something the parser already understands, upstream of the parser, where no handler,
no printer path and no part of ThorVG needs to know it happened.

**Two consequences follow, and both are already true of the older passes.** The dialect
has to be *discoverable*, because its whole audience is a caller that has never seen this
repository - which is why the QR rules went into `DeviceDoc` and into the `render svg`
description, not only into a header comment. And the dialect has to *refuse* rather than
approximate: a box too small for a code is an error naming the size that would work, not
a smaller module, because the one thing worse than a label that will not print is a label
that prints and does not scan.

Builds on the two passes that made the seam without naming it, neither of which has a
note of its own - the reasoning is in their commits: `2fb565e` (ThorVG resolves no
character reference, so a label printed the entity) and `b33b0eb` (a group's font-size is
a property ThorVG inherits from nothing). That both were written as repairs, and this one
could not be, is the whole of the delta here.
