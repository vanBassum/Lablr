---
id: 2026-09-18-10h50
date: 2026-09-18
time: "10:50"
title: A device that describes itself needs no tool of its own
---

**The question** was how to let an AI agent drive Strux devices through the relay. The
shape that first suggests itself is the one every MCP tutorial shows: enumerate what a
device can do and publish each capability as its own MCP tool, so the model sees
`set_led`, `read_partition`, `scan_wifi` and picks one.

**Why that is the wrong shape here** is not ergonomics, it is ownership. The relay is
device-agnostic on purpose — it moves session chunks and has never parsed a payload —
and a tool per command would put a copy of every device's command surface inside the
relay, generated from something that changes every time somebody flashes a board. Worse,
it would make the relay's tool list a function of which devices happen to be connected,
which is a moving target for a protocol whose tool list is cached by clients.

**The delta is that the discovery mechanism already existed and was already generic.**
`help list` has re-dispatched each handler with a reader that prints declarations
instead of filling them since the command layer was written — so a command's arguments
come from the command, and there is nothing to keep in step. That is exactly what an
agent needs, and it was built for a human exploring over a WebSocket. Three MCP tools
(`devices`, `describe`, `execute`) hand that same mechanism to a model, and the relay
stays as ignorant as it was: it asks a device what it can do and then says whatever it
is told to say.

What had to be added is only what the mechanism did *not* already carry — the parts a
declaration cannot express:

- a one-line **description** per command, and per argument, because a name and a type
  say how to spell a value and never which one;
- a device-level **description** (one line, in the relay hello) and free-form
  **instructions** (`system describe`), for the things no schema holds: which commands
  belong together, in what order, in what units, and what will not work.

The instructions are deliberately prose. The temptation is to formalise them — a
workflow graph, a capability taxonomy — and the reason not to is that anything
formalised has to be understood by the relay to be useful, which is the property being
protected. Prose is read by the one consumer that is good at prose.

**A second thing settled on the way.** Registration was suspected of needing to become a
`POST` with a structured body, now that there is more to send. It does not: the hello
chunk added two days ago *is* that structured body, and it already takes a new key
without a protocol change. What the relay must not do is record it earlier — a pending
device is refused before the upgrade precisely so that nothing it asserts is written
down before somebody trusts it ([[2026-09-16-16h20-a-relay-must-not-record-what-it-has-not-yet-decided-to-trust]]).
So the description rides in the hello and the instructions do not ride anywhere: they
are pulled on demand, from the firmware that is running, which is the only copy that
cannot describe a build that is no longer there.

**Exposure is one boolean**, per device, on the relay. Read/write/destructive tiers were
considered and rejected for the same reason as the tool-per-command shape: the relay
does not know what any command means, so any tier it invented would be a guess about
somebody else's firmware. The device's own description of a command is where "this
erases flash" belongs, and the operator's switch is where "this board may be driven by a
model" belongs.

Builds on: [[2026-09-16-16h20-a-relay-must-not-record-what-it-has-not-yet-decided-to-trust]].
Found on the way: [[2026-09-18-10h40-a-string-literal-is-not-utf-8-by-the-time-it-reaches-the-wire]].
