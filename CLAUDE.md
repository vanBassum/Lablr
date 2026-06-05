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

### The model — Draft / Template / Media / Preset / Printer
> ⚠ **Stale — pending reconciliation.** The shipped code (the "wireframe" render model, see LOGBOOK 2026-06-05 + memory `render-model-wireframe`) dropped **presets** and `designSize`; it uses `LabelStock` (not "Media"), **absolute mm**, and per-template **orientation variants**. The section below describes the older design and is kept until this is reconciled.

Labels compose from decoupled concepts:
- **Draft** — the data: a **preset** + field values (`fields`). Created by the AI — it picks the preset (use case) and fills the fields.
- **Template** — declarative layout + field schema + `designSize` (a coordinate space, not physical size). Owns which fields exist; renders any draft that supplies them.
- **Media** — the physical roll (size, material, SKU) + calibrated head offset. *What's loaded* determines the media at print time — physical runtime state, never baked into a draft.
- **Preset** — a named **(template + media + orientation)** **use case**: "Aida box", "Chemical vial", "Chemical bucket". **Presets are the decided entry point and the consistency layer — SETTLED, do not relitigate** (we oscillated ~3×). Why: the same thing always prints the same way — a transistor for the Aida box always uses that template + that roll — and it's how you/the AI express intent ("a label for my Aida box"), not "template X on media Y". If presets ever feel doubtful again, re-read this and the LOGBOOK entry rather than re-debating.
- **Printer** — a YAML profile (`config/printers/`), currently just **identity** (id, name) so media can link to it via `media.printers`. (DPI + head width still live in the driver while there's a single printer; they move here with item 24.) The print-placement **offset lives on the media** — a roll is always loaded on a specific printer, so the (printer + roll) calibration is uniquely the media's. Applied only at print time, never to the preview.

Note: a draft references **one** preset (its use case); the **print page can override** template/media/orientation — the escape hatch for "print this on whatever roll is actually loaded."

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
- holds **drafts in memory** (TTL eviction, no database) — `POST/GET /api/drafts`;
- serves the **config** (`GET /api/config`) and pictogram SVGs (`/pictograms`), read from a directory on disk (`Config:Dir`) that is a **mount point** in prod (edit config without rebuilding);
- exposes an **MCP server** (Streamable HTTP at `/mcp`) with `list_templates` + `create_draft` so an AI creates a draft and gets a `#/d/{id}` deep link;
- **serves the built PWA same-origin** (`wwwroot`).

It **never renders** — the PWA remains the canonical renderer. MCP auth is deferred (generic MCP for now; ChatGPT's OAuth 2.1 + DCR is item 53).

---

## Out of Scope (now and likely forever)

- Inventory / stock / BOM tracking, component or product records
- Complex databases
- User accounts, permissions, mandatory setup
- Backend/headless rendering, Chromium

Components, chemicals, and products are optional integrations — never required. Labels are independent objects created on demand.

---

## Deployment

Runs on the strato-stack homelab at **vanbassum.com**, behind Traefik (HTTPS via Let's Encrypt). The **lablr-api** container serves everything **same-origin**: the PWA (built into `wwwroot`), `/api`, `/pictograms`, and `/mcp`. Config is a **read-only mounted directory** (`Config__Dir=/config`); deep-link URLs derive from Traefik's forwarded headers. GitHub Pages is retired. A fresh deployment should become functional without manual admin/config forms.

---

## Project Structure

```
/lablr-ui      → React PWA — the canonical renderer; fetches config + drafts from the API
/lablr-api     → .NET 9 minimal API (active) — config + in-memory drafts + MCP; serves the PWA
/label-config  → configuration (labels, templates, printers, pictograms, drafts) — read by the API from disk (a mount in prod)
```
