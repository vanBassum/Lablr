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
Labels compose from decoupled concepts:
- **Draft** — the data (field values) + an **optional, overridable suggested template**. Created by the AI; *not* bound to a template (the same draft prints big or small).
- **Template** — declarative layout + field schema + design size. Owns which fields exist; a template can render any draft that supplies its fields.
- **Media** — the physical roll (size, material, SKU) + calibrated head offset. *What's loaded* determines the media at print time — physical runtime state, never baked into a draft.
- **Preset** — a named, reusable **(template + media + orientation)** output format (e.g. "Vial" = chemical-small + 25mm + portrait). A draft **auto-offers every preset whose template fits its data**, so one draft can produce multiple deliberate outputs (big *and* small, portrait *and* landscape). Presets are shared, defined once — not per-draft. Orientation defaults to portrait; a per-print toggle overrides it.
- **Printer** — a YAML profile (`config/printers/`), currently just **identity** (id, name) so media can link to it via `media.printers`. (DPI + head width still live in the driver while there's a single printer; they move here with item 24.) The print-placement **offset lives on the media** — a roll is always loaded on a specific printer, so the (printer + roll) calibration is uniquely the media's; per-roll values differ anyway. Offset is applied only at print time, never to the preview.

Note: presets are scoped to **(template + media)**, not printer, and exist to serve **multi-output** drafts (the human picks "Vial" vs "Bucket"). They were briefly dropped when each draft had a single output; the multi-output need brought them back in this refined form. (See LOGBOOK.)

### Platform & transport
The renderer and printer live on the **same device** — the production target is an **Android phone** (Chrome supports PWA install + Web Bluetooth). Transport is **not Bluetooth-only**: it lives behind a single `Printer` interface with swappable implementations. The **first implementation is WebUSB from desktop Chrome**, driving a USB Dymo LabelWriter 450 — chosen to prove the pipeline with no Bluetooth/OTG unknowns. **Web Bluetooth** (for the Niimbot, on Android) is a second implementation added later. The bitmap pipeline is identical across transports; only byte-delivery differs.

### End-to-end workflow
```
talk to ChatGPT → it creates a DRAFT (data only) on the hosted server → returns a link
   → tap link → PWA opens directly on the draft → preview → one tap → print over Bluetooth
```
ChatGPT never renders or prints — it only produces draft data + a URL. The hosted server (not the user's PC) holds drafts, so the flow works with the PC off.

### Persistence
Drafts are ephemeral (memory / simple storage). No database. A small server exists only to store and hand off drafts — it never renders.

---

## Out of Scope (now and likely forever)

- Inventory / stock / BOM tracking, component or product records
- Complex databases
- User accounts, permissions, mandatory setup
- Backend/headless rendering, Chromium

Components, chemicals, and products are optional integrations — never required. Labels are independent objects created on demand.

---

## Deployment

Destined for the strato-stack homelab at **vanbassum.com**, behind Traefik (HTTPS via Let's Encrypt). A fresh deployment should become functional without manual admin/config forms.

---

## Project Structure

```
/lablr-ui      → React PWA — the canonical renderer (primary)
/lablr-api     → .NET 9 minimal API — dormant in v1; later only for draft storage/handoff
/label-config  → configuration (media, templates, presets, printers) — introduced incrementally
```
