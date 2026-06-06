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

### Frontend-first — the PWA does everything
The **React PWA is the canonical renderer**. It loads config, edits a draft, renders the label to a canvas bitmap, shows the preview, and sends that bitmap to the printer. No backend rendering.

### Preview = Print (hard requirement, invariant)
There is **exactly one renderer and one bitmap**. The bitmap shown on screen is the identical bitmap sent to the printer. A second rendering path must never exist — this holds by construction, not as a feature to add later.

### Rendering pipeline (all client-side)
```
draft data + template → canvas at exact dot size → threshold to 1-bit bitmap
                      → that same bitmap = preview AND print payload
```
DPI is a property of the **media/printer profile**, passed into the mm→dots step — never hard-coded in the renderer.

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
```
AI calls the MCP create_draft tool → backend stores the draft in RAM → returns a #/d/{id} link
   → tap link → PWA opens directly on the draft → preview → one tap → print
```
The AI never renders or prints — it only produces draft data via MCP and gets back a URL. The hosted backend (not the user's PC) holds drafts, so the flow works with the PC off.

### Persistence & the backend (now active)

The **lablr-api** backend is live (no longer dormant). It:
- holds **drafts in memory** (TTL eviction, never in the DB) — `POST/GET /api/drafts`;
- stores the **config** (labels, templates, printers, pictograms) in an **embedded SQLite** file (`Db:Path`, a persistent volume in prod). The YAML in `Config:Dir` **seeds the DB on first boot only**; after that **the DB is the source of truth** and is edited at runtime via REST/MCP. (Editing the mounted YAML after first boot does nothing — reseed by starting with an empty DB.) Serves `GET /api/config` and pictogram SVGs (`/pictograms`);
- relays print jobs to a connected bridge as a **dumb byte forwarder** (`POST /api/agents/{id}/print` → WebSocket) — it forwards the PWA-rendered bytes verbatim and **never renders**;
- exposes an **MCP server** (Streamable HTTP at `/mcp`): read/author config (`list_templates`, `upsert_*`, …) and `create_draft`, which returns a `#/d/{id}` deep link. There is **no `print_draft`** — the AI never renders or prints;
- **serves the built PWA same-origin** (`wwwroot`).

It **never renders** — the PWA is the single renderer. MCP auth is deferred (generic MCP for now; ChatGPT's OAuth 2.1 + DCR is item 53).

---

## Out of Scope (now and likely forever)

- Inventory / stock / BOM tracking, component or product records
- Complex databases (the embedded SQLite **config** store is fine — it holds labels/templates/printers/pictograms, never inventory; drafts stay in RAM)
- User accounts, permissions, mandatory setup
- Backend/headless rendering, Chromium (the backend relays bytes, it never rasterizes — preview = print)

Components, chemicals, and products are optional integrations — never required. Labels are independent objects created on demand.

---

## Deployment

Runs on the strato-stack homelab at **vanbassum.com**, behind Traefik (HTTPS via Let's Encrypt). The **lablr-api** container serves everything **same-origin**: the PWA (built into `wwwroot`), `/api`, `/pictograms`, and `/mcp`. The config YAML is a **read-only mounted directory** (`Config__Dir=/config`) that **seeds an empty SQLite DB on first boot**; the DB lives on a **writable persistent volume** (`Db__Path=/data/lablr.db`) so runtime edits survive restarts. Deep-link URLs derive from Traefik's forwarded headers. GitHub Pages is retired. A fresh deployment becomes functional without manual admin/config forms.

---

## Project Structure

```
/lablr-ui      → React PWA — the single renderer; renders + previews + prints (WebUSB or via the bridge relay)
/lablr-api     → .NET 9 minimal API — SQLite config store + in-memory drafts + MCP + bridge byte-relay; serves the PWA. Never renders.
/lablr-bridge  → ESP32-S3 firmware: a BLE/cloud ⇄ USB-host "dumb pipe" so an Android phone can print to a USB-only Dymo
/label-config  → seed YAML/SVG (labels, templates, printers, pictograms, drafts) — seeds the SQLite DB on first boot (a mount in prod)
```
