---
id: 2026-09-10-00h10
date: 2026-09-10
time: "00:10"
title: The socket budget was sized for a two-file page
builds-on: 2026-09-09-23h10
---

**Before:** nothing about lwIP sockets was configured, so the device ran on IDF's
default `CONFIG_LWIP_MAX_SOCKETS=10` and `esp_http_server`'s default
`max_open_sockets=7`. That had been fine for as long as the device existed.

**What changed it:** UI modules put an import map in `index.html`, and because the two
host facades are rollup entries, Vite added a `<link rel="modulepreload">` for each. A
page load went from asking the device for **two** files to **four**, concurrently, while
the shell also opens its command WebSocket.

**Why the default could not absorb that.** `httpd_start` refuses to start unless
`LWIP_MAX_SOCKETS >= max_open_sockets + 3` — three are reserved for the server's own
working. At the defaults that inequality is *exactly tight*: `7 + 3 = 10`. The web
server is entitled to the entire socket table, and everything else on the device — the
relay's outbound WebSocket, mDNS, SNTP, the DHCP client — competes for sockets httpd has
already claimed. `accept()` then returns errno 23, ENFILE, and the server **resets**
whichever connections lost the race.

Measured against the bench C3: four parallel requests for assets that each return 200
on their own, and two of the four came back as connection resets. With a WebSocket also
open, all four failed. The device's own log was the only place it said so:

```
E httpd: httpd_accept_conn: error in accept (23)
W httpd: httpd_server: error accepting new connection
```

**The failure mode is what makes this worth a note.** The browser does not report a
reset stylesheet as an error you can see — it renders the page without it. So a device
whose HTTP server was out of sockets looked like *a shell with no sidebar*, which is
also what [the cascade bug](2026-09-09-23h55-dropping-preflight-isolates-a-modules-elements-not-its-utilities.md)
looked like. Two unrelated faults with one symptom, found in one sitting, and the second
would have gone on hiding behind the first.

**Both halves were wrong, so both are fixed:**

- `CONFIG_LWIP_MAX_SOCKETS=16`, the option's maximum. httpd's `7 + 3` now leaves six for
  everything else — headroom rather than a fit.
- `modulePreload: false` in the frontend build. The hints bought nothing: `react` and
  `react/jsx-runtime` are needed only once a module is imported, which cannot happen
  until the manifest has come back over the WebSocket, a round trip later. The import
  map still resolves them on demand. Four concurrent requests became two.

**The general lesson, which is not about sockets.** A frontend build decides how many
connections a page opens at once, and on this project the server is a microcontroller
with a socket table of ten. Those two facts live in different repositories' worth of
config and nothing connects them. Any change that adds a file to the critical path —
a font, a chunk, a preload hint — is a change to the device's connection budget, and the
symptom will be a missing asset rather than an error.
