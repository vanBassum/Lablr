---
id: 2026-09-16-16h20
date: 2026-09-16
time: "16:20"
title: A relay must not record what it has not yet decided to trust
---

**Before:** a device registered entirely through its connect URL —
`?id=…&fw=…&name=…&project=…` plus an `X-Strux-Token` header. The stated trade was
that the server knows who connected before the first chunk and the session protocol
gains no relay-specific verb, and for a while it held.

**What broke it** was wanting one more field. The firmware's git commit: a tag says
`0.1.0` and two boards reporting `0.1.0` can be running different code, which is
exactly what a device list exists to tell apart. Under the old scheme that is one more
query parameter — one more percent-encoder call, one more slice of a fixed `uri_`
buffer, one more change on both sides — and it was visibly not going to be the last.
Moving the display fields to a **hello message** on the socket (issue #22) makes each
new fact a key rather than a protocol change, and the fix seemed to be purely about
extensibility.

**The delta is what fell out of it.** A pending device is refused *before* the WebSocket
upgrade — that is what makes `/device` safe to leave on the public internet — so there
is no socket for it to say hello on. The obvious reading is that this is a regression:
today a pending row shows `Thermy / 1.0.0` and afterwards it shows an id. So the design
seemed to owe a way to get that metadata back, and three were considered (accept the
upgrade and read one chunk before closing; a separate `POST /device/hello`; keep a
reduced set in the URL).

All three are wrong, and seeing why reframed the problem. **Those strings are
unauthenticated assertions from a device nobody has vouched for yet, rendered beside an
Approve button.** That is the one place on the dashboard where a chosen name can
mislead the person making a security decision — "Thermy 1.0.0" next to Approve, sent by
whoever reached the endpoint. Recording them was never a feature that the new design
costs us; it was a hole that the new design closes for free.

So the ordering is the point, not an obstacle: **nothing a device asserts is written
down until the relay has decided to trust it.** What a pending row shows — the id, the
source address, the attempt count — is exactly what the relay can stand behind, and it
is what the approve decision should have been made on all along. The id is the only
field the token proves, which is also the argument for it being the only field left in
the URL; the two conclusions turn out to be the same one.

**What this does not say.** It is not an argument that pre-upgrade refusal is the only
safe design, nor that a device may never be heard before approval — a relay that wanted
richer pending rows could have a device sign its hello, and then it would be evidence
rather than assertion. It says that *unauthenticated display strings* and *an operator
about to grant access* do not belong on the same row, and that the cheapest way to
guarantee that is to have nowhere to put them.

The hello rides its own reserved session (`0xFFFE`, below telemetry's `0xFFFF`) rather
than becoming a command, for the reason telemetry did: nothing replies to it, the device
is not waiting, and the relay already dispatches on the header without reading a
payload. A command would have inverted who knows the answer — the relay asking the
device to describe itself — and cost a round trip on every reconnect.
