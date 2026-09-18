---
id: 2026-09-09-15h40
date: 2026-09-09
time: "15:40"
title: The loss was Strux, not the link — a control was the only way to know
builds-on: 2026-09-09-15h10
supersedes: 2026-09-09-15h10
---

**Before** (note `2026-09-09-15h10`): the command path was measured clean — 23 frames
received, 23 dispatched, 23 returned, every drop counter zero — while ICMP to the same
device lost 109 of 139 packets and the same wired PC lost none to its gateway or to
8.8.8.8. That was read as proof the fault was environmental: a congested 2.4 GHz band
and the C3 SuperMini's weak antenna.

**The counters were right. The conclusion drawn from them was wrong.**

The inference was: *the loss is below every layer I instrumented, therefore it is below
the firmware.* ICMP is answered by lwIP, so a lost ICMP echo cannot be a Strux handler's
fault — true — and from there to "the radio" is one step that felt like no step at all.
It skipped everything between lwIP and the antenna that Strux also affects: heap, the
TCP/IP task's share of the CPU, and the Wi-Fi driver's RX buffers.

**What changed it:** a control. `C:\Workspace\wifiprobe` is a separate ESP-IDF project
on the same ESP32-C3 — STA with hardcoded credentials, `WIFI_PS_NONE`, a UDP echo
server and counters. No board layer, no manager, no provider, no settings store, no
command dispatcher, no HTTP, no TLS, no relay, no mDNS, no telemetry, no FAT image, and
`sdkconfig.defaults` touching nothing that concerns the radio.

Same board, same AP, same position, same channel 1, same RSSI (−66/−67 dBm), same wired
PC. One variable: the firmware.

| | Strux | wifiprobe |
|---|---|---|
| ICMP | 109/139 lost (78 %) | **0/40 lost** |
| UDP echo, 200 B | — | **0/2500 lost** across two runs |
| RTT | 24 ms avg when it answered | p50 2.7 ms, p99 14 ms, max 24 ms |
| Free heap | not measured | ~222 KB |

**So the packet loss is caused by the firmware, and the RF link is fine.** Everything
attributed to the environment in the superseded note — the crowded band, the antenna,
the `WIFI_REASON_ASSOC_EXPIRE` episode — is back to being unexplained rather than
explained.

**One mechanism accounts for both numbers, which is what makes it credible.** A high
per-packet RX drop rate inside the device explains 78 % ICMP loss *and* 23 of 30
WebSocket frames arriving: TCP retransmits, so a request frame gets several attempts
where an ICMP echo gets one. The two figures looked like different phenomena and are the
same one seen through transports with different persistence. The slow successes — 1.9 s,
3.3 s, 7.3 s — were that retransmission, not a congested air interface.

**Where to look next, in order of what the numbers already favour:** free heap, because
lwIP drops silently when a pbuf allocation fails and Strux carries mbedTLS, an HTTP
server, mDNS and a mounted FAT partition that wifiprobe does not; then the Wi-Fi
driver's RX buffer counts; then whether any Strux task starves the TCP/IP task. The
control makes each of those a bisection rather than a guess — add one back to wifiprobe,
or take one away from Strux, and re-run the same UDP echo.

**The transferable lesson is about the shape of the argument, not the bug.** "The fault
is below everything I instrumented" bounds the fault from above and says nothing about
where it is. Instrumentation can only exonerate the layers it measures; a *control* —
the same hardware with the suspect removed — is the only thing that can exonerate the
hardware. This investigation produced four wrong diagnoses before one, and the control
took twenty minutes to build. Related: the two measurement faults in the superseded
note, which are still accurate and still worth reading.
