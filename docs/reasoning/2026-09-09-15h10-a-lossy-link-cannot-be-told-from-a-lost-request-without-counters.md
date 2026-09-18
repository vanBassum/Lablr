---
id: 2026-09-09-15h10
date: 2026-09-09
time: "15:10"
title: A lossy link cannot be told from a lost request without counters
builds-on:
supersedes:
---

**Before:** commands over both transports were intermittently "disappearing" — no reply,
nothing on serial, no error anywhere. Every hypothesis was about the firmware, and each
one was pursued and then had to be withdrawn: a socket-table leak in
`ConnectionRegistry` (never demonstrated — no `table full` line exists in any capture),
`Cmd_Modules` not being entered (the capture had missed the boot window), the
single-tasked `esp_http_server` blocking (the trace showed no stall anywhere). The
investigation kept looking inside the device because the device was the only thing being
measured.

**Two measurement faults were manufacturing the evidence.**

The first was the test harness. `cmd.mjs` printed its result with `console.log` and then
called `process.exit()`. On Windows, `console.log` to a pipe is asynchronous, so
`process.exit()` truncates it: a *successful* request whose reply had arrived, been
reassembled and decoded printed nothing at all, and was recorded as "empty reply". A
whole failure mode in the notes was an artefact of the observer. Output goes through
`fs.writeSync(1, …)` now, and the process is allowed to exit on its own.

The second was `esp_rom_printf` itself. Tracing with it *suppressed* the failure — 6/6
requests succeeded with the prints in, ~1/3 failed with them out — which is what a
timing-sensitive race looks like, and is why the fourth hypothesis was a race in the
reply path. It is not. The prints simply slowed the request rate. Separately, a single
20-argument `esp_rom_printf` produced numbers that did not reconcile with the requests
actually made; the ROM printf is a minimal implementation and short lines of five or six
arguments are the only ones worth trusting.

**What changed it:** counting instead of printing, at every stage that can end a request,
plus one control that touches none of the firmware.

Over 30 sequential `ui modules` requests on one LAN socket — 16 answered, 11 not — the
device's counters read: 23 frames received, 23 dispatched, 23 returned, 23 FINAL chunks
accepted by the socket. `noSlot`, `refused`, `recvFail`, `routeBad`, `authReject`,
`replyEmpty`, `sendFail`, `finalFail` and `readFail` were **all zero**. Seven request
frames never reached the device at all, and several replies that *were* sent arrived
after the client's 8 s timeout — the successful ones took 76 ms, 84 ms, 1.9 s, 3.3 s,
7.3 s, which is TCP retransmission backoff, not a handler.

The control settles it. ICMP to the device — handled by lwIP, below every layer under
suspicion — lost **109 of 139** packets. From the same wired PC, the gateway lost 0/20 at
0 ms and 8.8.8.8 lost 0/20 at 4.4 ms. The loss is size-independent (65–90 % at 8, 32, 200
and 1000 byte payloads), so it is not a rate or fragmentation effect, and the association
never dropped once (`drops=0`, RSSI a stable −66 to −73 dBm), so it is not roaming or
auth. 2.4 GHz has ~18 BSSIDs in range, five each on channels 1, 5 and 11.

**So:** the command path was never at fault, and the earlier `WIFI_REASON_ASSOC_EXPIRE`
episode was a worse instance of the same environmental loss rather than a separate bug.

**The trap worth naming:** *"time sync works, so the network is fine"* is not evidence.
`TimeManager: Time synchronized` appears in the middle of the 78 %-loss window. SNTP is
one small UDP request with retries and no deadline anyone notices, so it gets through a
link that fails three commands in ten. A mechanism that is known to work only proves the
link passes *occasional* packets — which is exactly the condition being ruled out. The
control has to be something that fails when the link is bad: a rate of small packets,
counted.

**What it implies for the design.** Nothing structural, and that is the point — the
counters exonerated the parts a redesign would have touched. Two bounded observations
survive:

- The client's idle timeout is the only thing converting a slow link into a failure.
  Replies at 8 s are real replies. That is a tuning question, not a fault.
- On the relay interface the same capture shows 16 failed sends and one dispatch that
  entered and had not returned. `RelaySessionLink::SendRaw` blocks up to 5 s per failed
  send, so under loss a handler writing several chunks holds the relay task long enough
  for the server to replace the pipe — which is the `chunk for unknown session`
  pattern seen from the relay side. It only appears on a link this bad, and the bounded
  fix belongs where it already is: the relay failing a device's in-flight browser
  sessions when it replaces that device's pipe, instead of letting them time out.

**The instrumentation is scaffolding, not a feature.** `lib/protocol/SessionStats.h`
costs one relaxed atomic add per stage and dumps to the UART on a 5 s timer — never
through `ESP_LOGx`, because `ConsoleManager` hooks the log vprintf and broadcasts every
line down the very transports being measured. It went in as one commit so it can leave as
one revert. A template that forks copy should not carry a diagnostic labelled temporary.
