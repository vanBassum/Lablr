# Lablr — Roadmap

> Grouped by **phase, in execution order**. The bold number is a **stable ID**
> the logbook references — it never changes or gets reused, so IDs appear out of
> sequence here (and as literal text, not an auto-numbered list). New work takes
> the next free number (currently 48+).

---

## Phase 1 — Pipeline & hardware ✅

- [x] **1.** Define the end-to-end workflow from "I need a label" to "label printed"
- [x] **2.** Choose the first printer target — prototype: Dymo LabelWriter 450 (USB); production: Niimbot (Bluetooth)
- [x] **3.** Decide how printing works — one printer-transport interface; first impl WebUSB from desktop Chrome, Web Bluetooth later
- [x] **4.** Prove a label can be printed — Dymo 450 over WebUSB, render→preview→raster→print

## Phase 2 — Label model & first templates ✅

- [x] **5.** Define the draft model — data only, not bound to a layout. (Refined since: a draft is `{ preset, fields }` — see 36 & 37.)
- [x] **7.** Create a minimal preview — canvas render of a draft
- [x] **8.** Ensure preview = print — one canvas is both preview and print payload, by construction
- [x] **9.** Create the first hardcoded template — `smd-basic`
- [x] **10.** Create the first printable label (e.g. BC547)
- [x] **11.** Introduce reusable templates — declarative **YAML** (mm units), loaded by id
- [x] **12.** Introduce physical media definitions (size, material, SKU) — YAML in `config/media/`
- [x] **13.** Introduce presets — named **(template + media + orientation)** use cases ("Aida box", "Vial", "Bucket"). **The consistency layer — settled, see CLAUDE.md.**
- [x] **14.** Resolve a draft to its preset → template + media + orientation
- [x] **23.** Support multiple media sizes — 3 real rolls: 25mm square (S0929120), 54×70, chem-resistant 54×101

## Phase 3 — Print UX & calibration ✅

- [x] **32.** Landscape/portrait orientation — renderer rotates the design 90° within the physical label
- [x] **33.** Persistent printer connection — connect once (PrinterProvider); each print is just transferOut; silent reconnect on load
- [x] **34.** Offset calibration aid — "Print alignment pattern" (border + cross); nudge X/Y to dial in each media's offset

## Phase 4 — Template/render rebuild (new schema) 🔧 in progress

> Config is migrated to the new schema; the renderer code must catch up (the app
> is currently broken against it). Decided: `designSize → media` via **contain**.

- [ ] **38.** Replace `size` with `designSize` — a layout coordinate space, decoupled from physical size (media owns physical size)
- [ ] **39.** Replace stack/flexbox with rectangular `elements` — each text in an explicit `rect { x, y, width, height }` (mm)
- [ ] **40.** Auto-fit text to rectangles — shrink the font from `maxSize` until it fits
- [ ] **43.** Introduce fit mode `shrink` (future: stretch, clip, ellipsis)
- [ ] **41.** Text alignment — `align: left|center|right`, `valign: top|center|bottom`
- [ ] **42.** Text wrapping — `wrap`, optional `maxLines`; shrink further when it overflows
- [ ] **44.** Update the template schema to the new structure — `designSize`, `fields: { key: { required, label } }`, `elements: [...]`
- [ ] **37.** Drafts use `fields` (renamed from `values`) — aligns with the template field model

## Phase 5 — Draft ↔ preset + print-page override ⏭ next

- [ ] **36.** Drafts reference a **preset** (`{ preset, fields }`) — config already migrated; wire the code to read `draft.preset`
- [ ] **46.** Edit template + media + orientation on the print page (settings/gear) — overrides the preset's defaults; media = the physical roll loaded

## Phase 6 — Printers

- [~] **24.** Support additional printers — printer **profiles** as YAML (`config/printers/`, first dymo-450), `media.printers` links. Multi-printer + Bluetooth (Niimbot) selection later.

## Phase 7 — AI integration 🤖

- [~] **28.** ChatGPT → open draft → print — PWA `#/draft` deep-link route opens straight into preview/print
- [~] **27.** Serverless draft creation — generated `llms.txt` + `catalog.json` on Pages; any browsing chat builds a `#/draft` link (no MCP, no Custom GPT)
- [ ] **47.** **MCP server on the homelab** — `create_label(template, fields)` validates against config and returns a `#/draft` link. A two-way contract (vs the one-way `llms.txt`) so the AI doesn't guess. Stateless (no draft DB); never renders.

## Phase 8 — Deployment

- [~] **31.** Deployment packaging — GitHub Pages workflow builds `lablr-ui` → vanbassum.github.io/Lablr (Vite `base: /Lablr/`)

## Phase 9 — Later / maybe

- [ ] **22.** Add the remaining real templates (SMD, chemical, storage families)
- [ ] **25.** Add a simple server/API if needed
- [ ] **26.** Add draft persistence / short links if needed
- [ ] **29.** Add headless/server-side printing if it becomes useful
- [ ] **30.** Add remote printer support if it becomes useful

## Dropped / superseded

- ~~**6.** Minimal label editor~~ — **dropped**: the PWA renders/prints, it doesn't author; values come from AI-created drafts.
- ~~**45.** Remove presets~~ — **reverted, settled**: presets are KEPT. A use case ("Aida box", "Vial", "Bucket") is a named template+media; presets give consistency (same thing → same output). The print page (46) is the override. Don't relitigate — see CLAUDE.md.
- ~~**35.** Auto-fit text (initial idea)~~ — **folded into 39/40/43**.
- **15–21** — removed from scope (history/reprint/search, HTML/CSS templates, live reload, git config, Claude-assisted editing). Numbers retired, not reused.

---

> **Guiding principle:** Keep questioning every feature that does not directly reduce the time between needing a label and having a label in your hand.
