---
id: 2026-09-18-15h35
date: 2026-09-18
time: "15:35"
title: A body the relay can carry is one the reply names, not one the caller recognizes
builds-on: 2026-09-18-10h50
---

**Before:** the MCP surface was believed complete once a device could describe itself
([[2026-09-18-10h50-a-device-that-describes-itself-needs-no-tool-of-its-own]]). An agent
could discover Lablr's workflow and compose any call. It then could not perform the
workflow, because the three commands that carry it — `fs read`, `fs write`, `render svg`
— move bytes rather than arguments, and the relay could neither send a body nor return
one. The gap read like a missing feature: "add body support to execute".

**What changed it:** looking for where to put the new mechanism and finding it already
there. `web read` has answered with a JSON header record, a newline, then the file since
the pull contract existed; `fs read`, `partition read` and `render svg` each copied that
shape. Streamed bodies were not missing from Strux at all. What was missing is that the
shape was something four handlers *happened to share* rather than something the
framework asserted — so the only way to know whether a reply had a body, and what the
body was, was to know which command you had called.

**Now:** an implicit convention is not a mechanism. It is a coincidence that works only
for a caller who already knows the command — which is precisely the knowledge the relay
is built not to have, and precisely what a model at the far end does not have either.
Making it explicit is the whole fix: `contentType` in the header record, an ordinary
media type, its *presence* being what declares a body exists. The relay then transports
bodies while reading exactly one field and no command name, and a firmware command
written next year is handled by relay code written today. Compatibility falls out rather
than being engineered: a reply declaring nothing is text, which is what every other
command already answers.

The generalization worth keeping is that the relay's device-agnosticism is not preserved
by keeping knowledge out of it, but by moving the *declaration* into the data. Both
halves of the MCP surface now work this way — the device says what it can do, and the
device says what it just sent.

**Two boundary placements the media type forced, each of which could have gone the
lazy way:**

*SVG is textual, though it is called `image/svg+xml`.* Dispatching on the `image/`
prefix would send a label design to a model as an opaque image block or as base64. An
SVG is a document that an agent reads, edits and writes back — that round trip is the
main thing Lablr is for — so the textual test runs first and is structural (`text/*`,
`+xml`, `+json`) rather than a list of blessed names. The lesson is that a media type's
top-level type states its *subject*, not its *encoding*, and a transport cares only
about the encoding.

*PNG is encoded on the device.* The tempting alternative was to let `render svg` keep
returning its ARGB8888S framebuffer and have the relay turn it into an image, since the
relay has the easier runtime for it. That would have put pixel-format knowledge —
Lablr's canvas layout, its byte order, its alpha convention — inside a component that
must not know what a label is. The media type is the boundary: the device's job is to
emit bytes it can name, and `application/octet-stream` is an honest name that buys
nothing. So the device encodes, which turned out cheap for an unrelated reason — the
S3's mask ROM exports miniz's deflate, so the encoder costs no flash and streams one
scanline at a time.

Rests on: media types being sufficient to describe everything a Strux command will ever
return. A body that is meaningful only in relation to its request — a diff, a delta
frame — would need more than a type, and nothing needs that yet.
