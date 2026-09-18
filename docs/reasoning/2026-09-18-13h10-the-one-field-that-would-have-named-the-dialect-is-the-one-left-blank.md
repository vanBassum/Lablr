---
id: 2026-09-18-13h10
date: 2026-09-18
time: "13:10"
title: The one field that would have named the dialect is the one left blank
---

**Before:** the phase brief said, correctly, "do not assume a particular LabelWriter
model or protocol implementation from memory if the device can tell us". USB printers
have a standard way of telling us: IEEE-1284 GET_DEVICE_ID, a printer-class control
request whose reply is a semicolon-separated string with a `CMD:` field naming the
command sets the printer understands — `CMD:PCL,PJL`, `CMD:ESCPL2,BDC`, and so on. So
the plan was to ask before implementing, and let the answer pick the dialect.

**What the printer said:**

```
MFG:DYMO;CMD: ;MDL:LabelWriter 450;CLASS:PRINTER;DESCRIPTION:DYMO LabelWriter 450;SERN:01010112345600;
```

Everything is there. Manufacturer, model, class, description, a serial number. And
`CMD:` is a single space. The printer answers the question fully and declines exactly
the one field that would have determined what bytes to send it.

**The delta:** self-description is not a property a device has or lacks; it is per-field,
and the fields a vendor fills in are the ones that cost them nothing. `MDL` is marketing.
`CMD` is the raster protocol, and DYMO's raster protocol is documented separately and
was, for years, not documented publicly at all. A device can be maximally forthcoming
about its identity and silent about its behaviour, and the second is what an
implementation needs.

So the enumeration probe did not tell us what to write. It told us **which** printer we
were writing for, and the ESC language came from elsewhere — a known-good implementation
on this repository's own `legacy` branch, confirmed against paper. That is a different
epistemic status from "the device told us", and it is worth being explicit about,
because the two look identical in working code.

What follows practically: **a second LabelWriter model cannot be supported by asking it
what it speaks.** The obvious-looking design — read `CMD:`, dispatch to a dialect — has
no input. Support for another model will mean another verified-on-paper implementation
selected by `MDL` or by VID/PID, which is a table of facts we assert, not a capability we
discover. Anyone reaching for `CMD:` later will find a space there and should not spend
an afternoon deciding whether their control transfer is broken.

The inverse is the part worth keeping. `usb status` logs and reports every descriptor
*before* anything is claimed, and that turned out to be where the value was: interface
class 07/01/02, EP OUT 0x02 and EP IN 0x82 both bulk at 64-byte MPS, self-powered, one
configuration. None of that is the dialect, and all of it was needed, and none of it
would have been visible if the code had claimed an interface on a model number and moved
on. Asking was right; it just answered a different question than the one asked.

Sits beside the rule that a channel only proves itself against something strict:
[[2026-09-18-10h40-a-string-literal-is-not-utf-8-by-the-time-it-reaches-the-wire]].
