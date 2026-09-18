---
id: 2026-09-09-20h40
date: 2026-09-09
time: "20:40"
title: A console that waits is a network that drops
builds-on: 2026-09-09-15h40
supersedes:
---

**Before:** a control had shown the RF link was fine and the fault was in Strux
(`2026-09-09-15h40`), and the next moves were listed as "free heap, then the driver's
RX buffer counts, then whether a Strux task starves the TCP/IP task". All three were
wrong, and the bisection found something no amount of reading would have suggested.

**What it actually was.** The ESP32-C3 board overlay set
`CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`, which makes USB Serial/JTAG the **primary**
console — `SECONDARY_NONE`, `UART_NUM=-1`. The primary console is a full VFS driver
whose write **blocks waiting for FIFO space**. The secondary console is a
best-effort ROM path that drops instead of waiting.

So `ConsoleManager::LogOutput` begins with `vprintf(fmt, args)`, and that call could
block for as long as the USB host took to drain — in whatever task was logging.
`esp_log_set_vprintf` means *every* task's log line goes through it, the Wi-Fi and
lwIP tasks included. Stall those and inbound frames are dropped indiscriminately,
which is why the damage showed up as ICMP loss with the command path's own counters
all reading clean.

The measurement that named the resource was `txerr=23 txeno=12` — 23 of 36 `sendto`
calls failing with `ENOMEM` — beside `heap=148456 minheap=82128`. A fixed pool was
empty while 148 KB of heap sat free, which is what a stalled driver looks like and
not what a leak looks like.

**Why the control never suffered it.** wifiprobe used the UART primary with USB as
*secondary*, so its log path could not block; and it logged one line per five
seconds where Strux logs on every manager's init and captures stdout besides. Same
board, same AP, same channel, same RSSI — the console was the only difference that
mattered, and the sdkconfig for `ESP_WIFI*` and `LWIP*` turned out to be
byte-identical between the two projects, 120 keys with no diff at all.

**The evidence, A→B→A in the minimal control, one variable:**

| | loss | p50 RTT | late |
|---|---|---|---|
| UART primary | 0/2400 | 2.7 ms | 3–6 |
| USB **primary** | up to 5.5 % | 55–81 ms | 659–758 |
| UART primary again | 0/2400 | 2.6 ms | 3–6 |

And on Strux, under a reboot churn that had failed 10 boots out of 10 at 14–97 %
loss: 8/8 boots clean minimal, 9/10 clean with every manager on, p50 2.7 ms.

**The fix is to stop claiming the primary slot.** `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`
plus `CONFIG_ESP_CONSOLE_SECONDARY_USB_SERIAL_JTAG=y` keeps every byte of output on
the USB-C port — `ESP_LOGx` and `esp_rom_printf` both verified arriving — and costs
nothing. The overlay's own comment had justified the setting with "without this the
console goes out of GPIO21 and `idf.py monitor` shows nothing", which is true of the
primary slot and false of the console as a whole. A comment that states a real
consequence of the wrong mechanism is worse than no comment: it is what stopped
anyone looking again.

**The generalisation, which is the part worth keeping.** *Anything that can block
must not sit on a path that every task takes.* Logging is exactly such a path,
because `esp_log_set_vprintf` puts one function between every task in the system and
the console. A log call has to be cheap and lossy, never slow and lossless — on a
network device, dropping a line is nothing and stalling the Wi-Fi task is
everything.

Looking for that shape rather than for the symptom immediately found a second
instance: `ConsoleManager::WriteHistory` held `mutex_` across its writes to `resp`,
and a `ReplyWriter` goes to the transport on every value. So `log list` — which the
frontend's Console page issues on open — held the log mutex for as long as a 40 KB
reply took to send, while `StoreLine` takes that same mutex from whatever task just
logged. Opening a page could stall the network stack for the length of its own
reply. Now the ring bounds are snapshotted and each line copied under its own short
lock, written outside it.

**What was ruled out, each by measurement rather than argument:** the relay and its
cache warmer; the entire upper half of the framework (webserver and its FAT mount,
telemetry, UI, update, SNTP, the app, mDNS) — removing which made things *worse*, not
better; `CONFIG_PM_ENABLE` and DFS, both directions; Wi-Fi power save, read back as
`ps=0` from inside the failing firmware; the second AP netif;
`WIFI_ALL_CHANNEL_SCAN` with `WIFI_CONNECT_AP_BY_SIGNAL`; PMF; an IP conflict; and
the whole Wi-Fi/lwIP configuration.

**And the residual is not ours.** After both fixes, roughly one boot in twelve still
loses a single contiguous 1–3 second run of packets with perfect latency for
everything around it and no disconnect logged. The control does the same thing at
the same rate (2 boots in 12 against Strux's 1 in 12), and steady state shows none at
all — 18 000 consecutive packets, zero loss. It is the AP settling a returning
client, and Strux is at parity with a minimal ESP-IDF station. Knowing that required
running the churn against the control, which is a step worth remembering: a residual
is only yours if the control does not share it.

**On the observation that long-connected devices seemed stable.** It was correct, and
it was a clue rather than a coincidence: once boot logging stops, so does the
stalling. It was deliberately not used to steer the bisection, and it fell out of the
answer instead — which is the right order.
