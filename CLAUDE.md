# Lablr — Label Printing System

A tool to minimize friction between deciding a label is needed and having a printed label in hand. **A label printing tool, not inventory software.**

> This file holds the **active, decided constraints** — the stable truths that guide work right now, abstracted.
> - **What's next / open items:** [ROADMAP.md](ROADMAP.md) (items are numbered stable IDs)
> - **Why decisions were made / brainstorms:** [LOGBOOK.md](LOGBOOK.md) (append-only)
>
> If something here is no longer true, change it here *and* log the reversal in LOGBOOK.

---

## Working Agreements

- **Always commit and push** after completing a piece of work — don't wait to be asked.
- When a decision changes, update this file and append the reasoning to [LOGBOOK.md](LOGBOOK.md).

---

## Guiding Principle

**Question any feature or complexity that does not directly reduce the time between needing a label and having a label in hand.** Everything else is secondary.

Common flow: *"I have something. I need a label."*

Primary use cases: electronics workshop (SMD parts), chemical storage (bottles), general storage (bins/boxes).

---

## Active Decisions (v1)

### Backend-canonical rendering — one renderer, on the server
The **C# backend (`LabelRenderer`, SkiaSharp) is the single renderer**. It turns a draft+template into the 1-bit label bitmap and, from that same bitmap, produces **both** the preview PNG and the DYMO print job. The **PWA does not render** — it fetches the rendered image to show the preview, and fetches the rendered bytes (or asks the backend to relay them) to print. This is what lets the **AI print headlessly** ("make a label and print it" — nothing opens); the frontend is just for checking and the occasional manual tweak.

> Reversal note: an earlier "frontend-first / PWA is the canonical renderer / no backend rendering" decision was reversed on 2026-06-06 — see LOGBOOK + memory `backend-canonical-render`. Don't reintroduce client-side rendering.

### Preview = Print (hard requirement, invariant)
There is **exactly one renderer and one bitmap**. Because the frontend displays the backend's rendered image and printing uses the backend's render of the same draft, the preview is the identical bitmap that prints — by construction. A second (client-side) renderer must never exist.

### Rendering pipeline (all server-side, C#)
```
draft + template → SkiaSharp canvas at exact dot size → threshold to 1-bit bitmap
   → preview:  PNG of that bitmap  (GET /api/render/preview)
   → print:    that same bitmap → DYMO LW450 job  (GET /api/render/job → WebUSB, or POST /api/print/draft → bridge)
```
DPI is a property of the **printer profile**, passed into the mm→dots step — never hard-coded in the renderer. The print-head offset (stock calibration + manual nudge) is applied only when building the job, never to the preview.

### Templates — declarative layout first
v1 templates are a small **declarative layout** the canvas renderer interprets directly (stacks, text fields, font size/weight/align; barcode/QR later). Claude- and human-editable. **HTML/CSS templates are deferred** to a possible later alternative renderer — decoupled on purpose.

### The model — Draft / Template / Label / Printer (the "wireframe" model)
> This is the **shipped** model (see LOGBOOK 2026-06-05/06-06 + memory `render-model-wireframe`). Presets, "Media", and `designSize` were **dropped** — see [DESIGN.md](DESIGN.md) for the full spec. Don't reintroduce presets without logging a reversal.

Labels compose from decoupled concepts:
- **Draft** — data only: field values (`fields`), no references. The AI fills the fields. A draft matches any **template** whose `requiredFields` it supplies; the PWA auto-discovers the matches and lets the user pick when more than one fits.
- **Template** — a handcrafted layout for one **label** (+ orientation): `requiredFields`, a `label` reference, and `elements` in **absolute millimetres** (`rect { x, y, width, height }`). Orientation is either a single `orientation` + `elements`, or per-orientation `variants` (`portrait`/`landscape`). No `designSize`, no responsive scaling — many simple templates over a few clever ones.
- **Label** (`LabelStock`) — the physical roll: `widthMm`/`heightMm`, material, SKU, `marginsMm` (safe area), `offsetCorrectionMm` (head calibration), and `compatiblePrinters`. Margins/offsets live here so every template on that roll inherits them. Never carries DPI.
- **Printer** — a profile (`id`, `name`, `dpi`). A label's `compatiblePrinters` selects one; the renderer reads `dpi` for the mm→dots step. The print-placement **offset lives on the label** (a roll is calibrated against the head); applied only at print time, never baked into the preview.

### Platform & transport
The renderer and printer live on the **same device** — the production target is an **Android phone** (Chrome supports PWA install + Web Bluetooth). Transport is **not Bluetooth-only**: it lives behind a single `Printer` interface with swappable implementations. The **first implementation is WebUSB from desktop Chrome**, driving a USB Dymo LabelWriter 450 — chosen to prove the pipeline with no Bluetooth/OTG unknowns. **Web Bluetooth** (for the Niimbot, on Android) is a second implementation added later. The bitmap pipeline is identical across transports; only byte-delivery differs.

### End-to-end workflow
Two ways to get a label, both off the one backend renderer:
```
Zero-touch (AI prints):   AI create_draft → AI print_draft(agentId) → backend renders → relays to the bridge → label prints
Check-then-print (human): AI create_draft → #/d/{id} link → PWA shows the backend preview → one tap → print (WebUSB or bridge)
```
The AI prints **headlessly** via `print_draft` (no app needed); the PWA flow exists for checking/manual tweaks. The hosted backend (not the user's PC) holds drafts and renders, so both flows work with the PC off.

### Persistence & the backend (now active)

The **lablr-api** backend is live (no longer dormant). It:
- is the **single renderer** — `LabelRenderer` (SkiaSharp) turns draft+template into the 1-bit bitmap and serves `GET /api/render/preview` (PNG), `GET /api/render/template-preview` (PNG, sample values), and `GET /api/render/job` (DYMO bytes for WebUSB);
- holds **drafts in memory** (TTL eviction, never in the DB) — `POST/GET /api/drafts`;
- stores the **config** (labels, templates, printers, pictograms) in an **embedded SQLite** file (`Db:Path`, a persistent volume in prod). The YAML in `Config:Dir` **seeds the DB on first boot only**; after that **the DB is the source of truth** and is edited at runtime via REST/MCP. (Editing the mounted YAML after first boot does nothing — reseed by starting with an empty DB.) Schema changes ship as **EF Core migrations** (`lablr-api/Data/Migrations/`), applied at startup; a pre-migrations DB is auto-baselined so it isn't recreated. Serves `GET /api/config` and pictogram SVGs (`/pictograms`);
- **prints**: `POST /api/print/draft` and the MCP `print_draft` render the job and relay it to a connected bridge over a WebSocket (`/agent/ws`). The bridge is a dumb byte pipe;
- exposes an **MCP server** (Streamable HTTP at `/mcp`): read/author config (`list_templates`, `upsert_*`, …), `create_draft` (returns a `#/d/{id}` deep link), `list_bridges`, and `print_draft` (renders + prints headlessly — the AI's zero-touch path);
- **serves the built PWA same-origin** (`wwwroot`).

The backend is the renderer; the PWA fetches its output. MCP auth is deferred (generic MCP for now; ChatGPT's OAuth 2.1 + DCR is item 53).

---

## Out of Scope (now and likely forever)

- Inventory / stock / BOM tracking, component or product records
- Complex databases (the embedded SQLite **config** store is fine — it holds labels/templates/printers/pictograms, never inventory; drafts stay in RAM)
- User accounts, permissions, mandatory setup
- A **browser/Chromium** on the server — rendering is C#/SkiaSharp, not a headless browser. (Backend rendering itself is core, not out of scope.)
- A **second renderer** — the backend renderer is the only one; the frontend must never rasterize (preview = print)

Components, chemicals, and products are optional integrations — never required. Labels are independent objects created on demand.

---

## Deployment

Runs on the strato-stack homelab at **vanbassum.com**, behind Traefik (HTTPS via Let's Encrypt). The **lablr-api** container serves everything **same-origin**: the PWA (built into `wwwroot`), `/api`, `/pictograms`, and `/mcp`. The config YAML is a **read-only mounted directory** (`Config__Dir=/config`) that **seeds an empty SQLite DB on first boot**; the DB lives on a **writable persistent volume** (`Db__Path=/data/lablr.db`) so runtime edits survive restarts. Deep-link URLs derive from Traefik's forwarded headers. GitHub Pages is retired. A fresh deployment becomes functional without manual admin/config forms.

---

## Project Structure

```
/lablr-ui      → React PWA — fetches the backend-rendered PNG to preview; prints backend-built bytes (WebUSB) or asks the backend to relay to a bridge. Does NOT render.
/lablr-api     → .NET 9 minimal API — the single renderer (LabelRenderer/SkiaSharp) + SQLite config store + in-memory drafts + MCP (incl. print_draft) + bridge relay; serves the PWA
/lablr-bridge  → ESP32-S3 firmware: a BLE/cloud ⇄ USB-host "dumb pipe" so an Android phone can print to a USB-only Dymo
/label-config  → seed YAML/SVG (labels, templates, printers, pictograms, drafts) — seeds the SQLite DB on first boot (a mount in prod)
```
