---
id: 2026-09-09-21h30
date: 2026-09-09
time: "21:30"
title: "Logbook: the Wi-Fi packet-loss bisection, run by run"
builds-on: 2026-09-09-20h40
supersedes:
---

The evidence behind
[`2026-09-09-20h40-a-console-that-waits-is-a-network-that-drops`](2026-09-09-20h40-a-console-that-waits-is-a-network-that-drops.md),
kept because that conclusion is cheap to state and expensive to re-derive. This is
every configuration that was measured, in order, including the four wrong diagnoses
and the two faults in my own instruments.

It lives here rather than at the repo root because this folder's contract is exactly
what a logbook needs: dated, append-only, immutable, never rewritten. An entry that
turned out wrong got a later entry saying so; none were edited. If the question is
ever reopened, read from the bottom.

## The test environment

Fixed for every experiment below, so results are comparable:

- **Device** ESP32-C3 SuperMini, MAC `e8:3d:c1:9e:a8:50`, DHCP `192.168.11.22`,
  serial on COM6. Same physical position throughout.
- **AP** SSID `vanBassum`, 2.4 GHz channel 1, RSSI −66 to −77 dBm. ~18 BSSIDs in
  range, five each on channels 1, 5 and 11.
- **PC** wired Ethernet `192.168.11.10`, 1 Gbps. Gateway and 8.8.8.8 both 0 % loss —
  the PC's own path is never the variable.
- **Relay** local dev API on `ws://192.168.11.10:8080/device`, device id
  `esp32-e83dc19ea850`. Plain `ws://`, **no TLS**.
- **Instrument** `wifiprobe/pc/udpecho.py` — sequenced UDP echo, 200 B payload,
  10 ms gap. UDP not TCP: TCP retransmission hid the loss from us for a day. The
  device echoes on port 7777 (`wifiprobe` natively, Strux via `main/diag_udpecho.c`),
  so both firmwares are measured by the same thing.
- **Control** `C:\Workspace\wifiprobe` — minimal ESP-IDF STA + UDP echo, no Strux
  code at all.
- **Branch** `diag/packet-loss`, pushed. `main/DiagConfig.h` holds the per-manager
  kill switches and `main/diag_udpecho.c` the in-Strux echo server. Neither exists on
  `main` — they were scaffolding and were reverted once they had done their job — so
  every path below naming a `DIAG_*` switch is read against that branch, not the
  tree you are in. Only three things survived the cleanup: the console config in the
  C3 board overlay, the `#error` guard in `ConsoleManager`, and the `WriteHistory`
  lock fix.

---

### 2026-09-09 15:02 — First measurement: Strux loses 78 % of ICMP

109 of 139 pings lost, size-independent (65–90 % at 8/32/200/1000 B). Association
never dropped, RSSI −66…−73. Strux's own command counters were all clean: 23 frames
in, 23 dispatched, 23 returned, every drop counter zero.

### 2026-09-09 15:10 — Wrong conclusion: blamed the RF environment

Reasoned "the loss is below every layer I instrumented, therefore below the
firmware". That bounds the fault from above and says nothing about where it is.

### 2026-09-09 15:20 — Two measurement faults found, both mine

`cmd.mjs` used `console.log` + `process.exit()`; on Windows that truncates piped
stdout, so successful replies were recorded as "empty reply". That failure mode
never existed. Also: one 20-argument `esp_rom_printf` produced numbers that did not
reconcile — split into short lines.

### 2026-09-09 15:35 — Control built: wifiprobe is clean

Same board, position, AP, channel, RSSI. **0/40 ICMP, 0/2500 UDP**, p50 2.7 ms, zero
disconnects, 222 KB free heap. So the RF link is fine and the fault is in Strux.
Supersedes the 15:10 conclusion.

### 2026-09-09 16:05 — Strux baseline came up clean, so the fault is not static

Full Strux + the same instrument: **0/2500 lost**. Then 0/1500 under concurrent
WebSocket load (60/60 commands OK), and 0/1500 during repeated 131 KB HTTP transfers.
Load alone degrades latency (p99 62 ms) but loses nothing. Reproduction became the
blocker.

### 2026-09-09 16:20 — Ruled out: power save reset by stop/start

Theory: Strux's reconnect calls `esp_wifi_stop()`/`start()` and never re-asserts
`WIFI_PS_NONE`. Tested in wifiprobe by doing exactly that cycle:
`ps before stop = 0`, `ps after start = 0`, p50 RTT unchanged at 2.6 ms. Power save
is **not** reset by a stop/start cycle. Disproven.

### 2026-09-09 16:35 — Ruled out: CONFIG_PM_ENABLE / DFS

Strux sets `CONFIG_PM_ENABLE=y` + `CONFIG_PM_DFS_INIT_AUTO=y`; wifiprobe did not.
Added both to wifiprobe as the single variable: **0/2500 lost**. Reason it is inert —
the reported CPU clock never left 160 MHz, because Wi-Fi holds an `APB_FREQ_MAX`
lock. Disproven, with the mechanism.

### 2026-09-09 16:45 — Ruled out: IP conflict / second board

ARP shows one host on `.22` with the expected MAC. No duplicate.

### 2026-09-09 17:00 — REPRODUCED: reboot churn

Rebooting the device repeatedly (what I was actually doing when the failure first
appeared) makes it come back. 8 boots: one at **90.8 %** loss, four more at 2.7–6.8 %,
tail RTT up to 1571 ms.

### 2026-09-09 17:20 — Reproduction is now reliable: 10/10 boots bad

Added `DIAG_AUTO_REBOOT_S=75` so the device reboots itself and the harness needs
nothing from the firmware. Full Strux, 10 self-reboots: **every boot bad**, 14 % to
97 % loss, p50 RTT 156–2387 ms.

Note the shape: p50 of 350–430 ms on a 2 ms link is *queueing or sleeping*, not RF
loss. Next step is to read `esp_wifi_get_ps()` in Strux itself — the 16:20 experiment
only proved a stop/start cycle does not reset it, never that Strux's call (made
*before* `esp_wifi_start()`) takes effect at all.

### 2026-09-09 17:35 — Ruled out: power save, measured inside Strux

`esp_wifi_get_ps()` reported from Strux itself: **ps=0** on every report line, across
boots. Strux does set `WIFI_PS_NONE` before `esp_wifi_start()` and it does take
effect. Power save is out for good.

Also withdrawing the CPU-clock reading from the 16:35 entry: it used
`ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED`, which returns a cached value, not the live
frequency. That measurement said nothing.

### 2026-09-09 17:45 — Ruled out: the relay (bisection step 1)

`DIAG_ENABLE_RELAY=0`, everything else on, 8 self-reboots: still bad, 1.8 %–94.3 %.
So the cache warmer pulling 131 KB after every reconnect is not the cause.

### 2026-09-09 17:55 — Ruled out: the whole upper half (bisection step 2)

Off: webserver (and its FAT mount), telemetry, UI, update, SNTP, the LED app, mDNS.
Left: console, settings, system, network, command. 8 boots: **42 %–100 % loss** —
worse, not better. The fault is in the core, not in any application or service.

### 2026-09-09 18:05 — Ruled out: CONFIG_PM_ENABLE, properly this time

Minimal Strux with PM compiled out entirely (deleted `sdkconfig.c3` so the defaults
actually took): 8 boots, **33.8 %–99.5 % loss**. Latency dropped a lot (p50 17–82 ms
vs 156–2387 ms) but loss rose. PM was adding delay, not causing loss.

### 2026-09-09 18:15 — The measurement that names the resource

`rx=160 tx=137 seqhigh=600 rxerr=0 txerr=23 txeno=12 heap=148456 minheap=82128`

- 600 sent by the PC, **160 arrived** — 73 % lost *inbound*, before any Strux code.
- Of those 160, 23 replies failed to send with **errno 12 = ENOMEM**.
- Free heap 148 KB, minimum 82 KB — so this is a **fixed pool**, not the heap.
- `STA disconnected (reason 4)` = ASSOC_EXPIRE mid-run.

Dominant failure is inbound drop with a simultaneous TX allocation failure. With
webserver and relay off, Strux holds no sockets at all — so nothing of Strux's is
holding receive buffers. Remaining differences from wifiprobe are all inside
`WiFiInterface`: the second (AP) netif, `WIFI_ALL_CHANNEL_SCAN` +
`WIFI_CONNECT_AP_BY_SIGNAL`, and `pmf_cfg.capable = true`.

### 2026-09-09 18:20 — Environment caveat: VPN active on the PC

Bas started a VPN on the measuring PC around this time. It can re-route even
LAN-bound traffic, so results in this window are suspect. The decisive comparison
gets re-run with the VPN off before anything is concluded from it.

### 2026-09-09 18:30 — Ruled out: sdkconfig for Wi-Fi and lwIP is identical

Diffed every `CONFIG_ESP_WIFI*` / `CONFIG_LWIP*` / `CONFIG_ESP_COEX*` key between the
two projects: **120 keys each, zero differences.** Driver buffers, pbuf pools,
lwIP task settings and coexistence are all off the table.

### 2026-09-09 18:40 — Ruled out: the AP netif, all-channel scan, PMF

Guarded each behind a switch and turned all three off, making Strux's Wi-Fi setup
match wifiprobe's: 8 boots, **83.7 %–98.8 % loss**. None of them.

### 2026-09-09 18:55 — ROOT CAUSE: USB Serial/JTAG as the PRIMARY console

Full sdkconfig diff turned up the real difference. Strux's C3 board overlay set
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, which makes USB the **primary** console
(`SECONDARY_NONE`, `UART_NUM=-1`). wifiprobe used the UART primary with USB as
**secondary**.

The primary console is a full VFS driver whose write **blocks waiting for FIFO
space**; the secondary is a best-effort ROM path that drops instead of waiting. So
every log line stalls whichever task emitted it — including the Wi-Fi and lwIP
tasks, which is why the damage is indiscriminate inbound frame loss plus `ENOMEM`
on transmit.

A→B→A in the minimal control, one variable, nothing else touched:

| Config | Loss | p50 RTT | late |
|---|---|---|---|
| A UART primary | 0/2400 (0 %) | 2.7 ms | 3–6 |
| B USB **primary** | up to 5.5 % | 55–81 ms | 659–758 |
| C back to UART primary | 0/2400 (0 %) | 2.6 ms | 3–6 |

Draining the port from the host helps but does not fix it (p50 12–37 ms), so it is
the blocking write path, not merely an absent reader.

### 2026-09-09 19:05 — Fix verified in Strux

Board overlay changed to `CONFIG_ESP_CONSOLE_UART_DEFAULT=y` +
`CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` — USB output is kept, just as
secondary. The overlay's old comment was wrong: the secondary console reaches USB
too, so nothing is lost by not claiming the primary slot.

- Minimal Strux + fix: **8/8 boots 0.0 % loss**, p50 2.7 ms.
- Full Strux, every manager on, + fix: **9/10 boots 0.0 % loss**, p50 2.7 ms.

Why wifiprobe never suffered: it logged one line per 5 s through a non-blocking
path. Strux logs on every manager init and captures stdout, so it blocked far more —
and it explains the observation that long-connected devices seem stable: once boot
logging stops, so does the stalling.

### 2026-09-09 19:10 — Residual: one boot in ten drops a burst

Boot 5 of the full-Strux run lost 195/600 (32.5 %) but with **p50 2.8 ms, p99
14 ms** — latency perfect. That is a short outage, not the console fault, and needs
its own investigation.

### 2026-09-09 19:30 — Fix holds under full concurrent load

Steady state, no reboots, everything enabled, three loads at once (UDP measurement +
WebSocket command loop + bulk HTTP off port 80 + relay-proxied fetches):

- UDP: **20/20 batches, 0.0 % loss**, `late` 5–18 per 600.
- WebSocket: **321/321 commands OK**, 53–71 ms each. Before the fix the same test
  produced 8-second timeouts and 11 failures in 30.
- Device counters: `rx=2400 tx=2400 txerr=0 txeno=0`, `ws frm/in/out/fin` all equal,
  `relay txf=0`.

Also verified the fix costs no observability: with UART primary + USB secondary,
both `ESP_LOGx` **and** `esp_rom_printf` still arrive over the USB-C port. The
overlay comment that justified the primary slot was simply wrong.

`CONFIG_PM_ENABLE` re-enabled and now harmless — p50 2.7 ms, p90 5.0 ms. The
multi-second latency previously blamed on PM was the console all along.

### 2026-09-09 19:40 — Fixed a second instance of the same class

`ConsoleManager::WriteHistory` held the log mutex across writes to `resp`, which go
to the transport on every value — so a `log list` (the frontend's Console page) held
that mutex for the length of a 40 KB network reply, while `StoreLine` takes the same
mutex from whatever task just logged, Wi-Fi and lwIP included.

Same defect as the root cause: a network-speed operation blocking a task that only
wanted to log. Now snapshots the ring bounds, then copies one line at a time and
releases between lines. A wrap mid-reply can show a newer line in a slot; a log dump
can live with that.

### 2026-09-09 20:10 — Stress round: at control parity

Both fixes, everything enabled. Same instrument throughout.

| case | loss | rtt |
|---|---|---|
| 8 B payload, 2 ms gap, 2000 pkt | 0.0 % | p50 2.7 / p99 11.1 / max 27 ms |
| 200 B, 2 ms gap, 2000 pkt | 0.0 % | p50 2.8 / p99 10.9 ms |
| 1400 B, 2 ms gap, 1500 pkt | 0.1 % (1) | p50 4.2 / p99 19.3 ms |
| 1400 B, 10 ms gap, 1000 pkt | 0.0 % | p50 5.1 / p99 20.0 ms |
| 200 B sustained, 3000 pkt | 0.0 % | p50 4.4 / p99 14.9 ms |
| UDP during 15× `log list` | 0.0 % | p50 4.3 / p99 14.4 ms |

- ICMP, 200 packets: **198 received (1 % loss), avg 3 ms** — against 78 % loss and
  24 ms before.
- Command surface: 60/60 on one socket.

### 2026-09-09 20:25 — The residual is the environment, not Strux

20-boot churn regression showed 17/20 clean and 3 boots losing a burst *with perfect
latency* (p50 3–4 ms). Added loss-shape reporting to the instrument, which showed
each one is **a single contiguous run** — a 1–3 s blackout at a random point, no
disconnect logged, `txerr=0 txeno=0`.

Not the relay: with `DIAG_ENABLE_RELAY=0` it still happened, 1 boot in 12.
Not the state machine: the cycle timer is stopped on `Ipv4Acquired` and no
"restarting the station" or "Lost IP" line ever appears.

The missing control was reboot churn against **wifiprobe**, which had never been
run. Added self-reboot to the control and measured it the same way:

| firmware | boots | bursts | shape |
|---|---|---|---|
| full Strux + fixes | 12 | 1 | 1 run, 97 pkt from seq 503 |
| wifiprobe (control) | 12 | 2 | 1 run, 101 from 330; 1 run, 57 from 543 |

Statistically identical. **Strux is at parity with a minimal ESP-IDF STA**, so the
remaining blackout is an AP-side event and not this firmware's to answer for.

### 2026-09-09 20:50 — Guard added so it cannot come back

A `#error` in `ConsoleManager.cpp` refuses any build where USB Serial/JTAG is the
**primary** console, naming the mechanism and the one-line fix, with
`STRUX_ALLOW_BLOCKING_CONSOLE` as a deliberate escape hatch.

It lives there rather than in a board file because that is the code whose safety
depends on it — `esp_log_set_vprintf` puts `LogOutput` between every task and the
console, so a blocking console is this manager's problem.

Verified both ways: both boards still build, and reintroducing the bad line refuses
the build with the explanation. A comment is what failed the first time; the old one
justified the setting with a real consequence of the wrong mechanism.

### 2026-09-09 20:55 — Final validation, steady state under full load

No reboots, all managers, relay on, three concurrent loads.
- UDP: **42/42 batches, 25 200 packets, 0.0 % loss.** `late` 5–18 per 600.
- WebSocket commands: **830/830 OK**, worst 287 ms under full load.
- `ui modules` **over the relay browser pipe** (the original failing symptom, which
  used to succeed 1 in 3 with 8–12 s timeouts): **40/40 OK, 65–81 ms.**
- Both boards build: `esp32c3_supermini` and `esp32_devkit`.

### 2026-09-09 21:15 — Adversarial: connection-table flood, log storm under a lock

300 socket attempts, 10 at a time against a 4-slot table. Every refusal logs from
inside `ConnectionRegistry`'s `LOCK`, so this is the remaining shape of the defect —
a log flood emitted under a mutex.

`opened=204 refused=98 replied=181 rejected=0 timedOut=0`

Refusals are correct for a 4-slot table, and **nothing hung**: no command was
silently swallowed, which is what the slotless-socket refusal was for. UDP loss
during the flood: **0.0 %**.

### 2026-09-09 21:20 — Known residual, bounded and currently harmless

`ConsoleManager::LogOutput` still calls `vprintf` synchronously, so the console is
on a path every task takes. With the UART primary that is cheap — a 128-byte
hardware FIFO that always drains — but a sustained log flood could still fill it and
block ~87 µs per byte. Not observed in any test above. The structural answer is to
queue in `LogOutput` and let the broadcast task do the writing; that is a real change
and not worth making to fix a symptom nobody has.
