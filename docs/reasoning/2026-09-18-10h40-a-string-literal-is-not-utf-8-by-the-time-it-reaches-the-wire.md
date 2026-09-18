---
id: 2026-09-18-10h40
date: 2026-09-18
time: "10:40"
title: A string literal is not UTF-8 by the time it reaches the wire
---

**Before:** source files are UTF-8, so a literal's bytes are the file's bytes. The repo
has written em dashes inside log strings for months — `"Disconnected — will retry"` —
on the assumption that what is in the file is what leaves the device.

**What falsified it** was giving the device something a machine parses. The new
`system describe` returns the product's instructions as a JSON string, and the relay
parses that reply. On hardware the reply came back with byte `0x97` where the source
had `0xE2 0x80 0x94`: the em dash had been re-encoded to **cp1252**, the system
codepage of the machine that ran the compiler. GCC's execution charset defaults to the
build host's locale, not to the source charset, so the conversion happens silently at
compile time and the file on disk keeps looking correct.

`0x97` is not valid UTF-8. The device's JSON was therefore not valid JSON to anything
strict, and only its own frontend — a browser, which repairs what it is given — had
ever been on the receiving end.

**The delta:** the encoding of a string literal is a property of the machine that built
the firmware, not of the file it was written in. So the same source produces different
bytes on a colleague's machine, in CI, and in a container — which makes this the worst
class of bug to chase later: it is invisible in review, invisible in the build, and
reproduces only where nobody is looking.

The rule that follows is small and mechanical: **string literals are ASCII; comments are
free.** Comments never leave the compiler, so the repo's prose style is untouched. The
37 literals that already carried an em dash were converted at once, which also means
those log lines were already reaching the log broadcast as invalid UTF-8 — a latent bug
nobody had noticed, because the only consumer was forgiving.

What makes this worth a note rather than a fix in passing is that it was found by
*adding a second consumer*. A frontend that repairs bad bytes had been hiding it; a
parser that refuses them exposed it the first time it was asked to read something the
firmware had written. The general form: a channel only proves itself when something
strict is on the other end — which is also the lesson the telemetry formatter learned
about Influx.

Relates to the MCP work: [[2026-09-18-10h50-a-device-that-describes-itself-needs-no-tool-of-its-own]].
