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

## Phase 4 — Template/render rebuild (new schema) ✅

> Config was migrated to the new schema; renderer code rewritten to support it.
> Implementation uses contain scaling for designSize → media mapping.

- [x] **38.** Replace `size` with `designSize` — a layout coordinate space, decoupled from physical size (media owns physical size)
- [x] **39.** Replace stack/flexbox with rectangular `elements` — each text in an explicit `rect { x, y, width, height }` (mm)
- [x] **40.** Auto-fit text to rectangles — shrink the font from `maxSize` until it fits
- [x] **43.** Introduce fit mode `shrink` (future: stretch, clip, ellipsis)
- [x] **41.** Text alignment — `align: left|center|right`, `valign: top|center|bottom`
- [x] **42.** Text wrapping — `wrap`, optional `maxLines`; shrink further when it overflows
- [x] **44.** Update the template schema to the new structure — `designSize`, `fields: { key: { required, label } }`, `elements: [...]`
- [x] **37.** Drafts use `fields` (renamed from `values`) — aligns with the template field model

## Phase 5 — Draft ↔ preset + print-page override 🔧 in progress

- [x] **36.** Drafts reference a **preset** (`{ preset, fields }`) — config already migrated; wire the code to read `draft.preset`
- [ ] **46.** Edit template + media + orientation on the print page (settings/gear) — overrides the preset's defaults; media = the physical roll loaded

## Phase 6 — Printers

- [~] **24.** Support additional printers — printer **profiles** as YAML (`config/printers/`, first dymo-450), `media.printers` links. Multi-printer + Bluetooth (Niimbot) selection later.

## Phase 7 — AI integration 🤖

- [x] **28.** ChatGPT → open draft → print — PWA `#/d/{id}` deep-link route opens straight into preview/print (item 52)
- [~] **27.** Serverless draft creation — generated `llms.txt` + `catalog.json` on Pages; any browsing chat builds a `#/draft` link (no MCP, no Custom GPT) — **superseded by the MCP `create_draft` tool (item 51); see LOGBOOK 2026-06-05**
- [x] **47.** MCP server on the homelab — **done as item 51: STATEFUL (creates + stores drafts in RAM, returns a deep link), generic MCP. See Phase 10 / LOGBOOK 2026-06-05.**

## Phase 8 — Deployment

- [~] **31.** Deployment packaging — GitHub Pages workflow builds `lablr-ui` → vanbassum.github.io/Lablr (Vite `base: /Lablr/`) — **superseded by same-origin homelab hosting (item 50); see LOGBOOK 2026-06-05**

## Phase 10 — Backend, config hosting & MCP (planned 2026-06-05) 🔧

> Activates `lablr-api`. Decisions (LOGBOOK 2026-06-05): generic MCP first
> (ChatGPT OAuth later), UI served **same-origin** from the homelab (retires
> Pages, removes the cross-origin canvas-taint risk), config baked into the API
> image from git.

- [x] **48.** In-memory draft store in `lablr-api` — `POST /api/drafts` + `GET /api/drafts/{id}`, TTL eviction, no DB (drafts in RAM)
- [x] **49.** Config (`/label-config` + pictogram SVGs) read from a configurable disk dir (mount); serve `GET /api/config` + static `/pictograms`; UI fetches at runtime behind a ready-gate
- [x] **50.** Serve the PWA same-origin from the backend (PWA built into the image `wwwroot`) — drops CORS + canvas-taint; retires the GitHub Pages deploy (supersedes 31)
- [x] **51.** MCP server (`ModelContextProtocol.AspNetCore`, `MapMcp("/mcp")` Streamable HTTP) — `list_templates` + `create_draft` (stores draft → deep link); generic MCP, no auth yet (reshapes 47). Config resources: not added (tools suffice).
- [x] **52.** PWA `#/d/{id}` deep-link route fetches the stored draft → straight into preview/print
- [ ] **53.** ChatGPT connector — OAuth 2.1 + Dynamic Client Registration (likely via Authentik); later

## Phase 9 — Later / maybe

- [ ] **22.** Add the remaining real templates (SMD, chemical, storage families)
- [ ] **25.** Add a simple server/API if needed
- [ ] **26.** Add draft persistence / short links if needed
- [x] **29.** Headless/server-side printing — **done**: backend renders (SkiaSharp) + MCP `print_draft` relays to a bridge; the AI prints with no app open. See the "Backend-canonical rendering" note below.
- [ ] **30.** Add remote printer support if it becomes useful

## Dropped / superseded

- **Backend-canonical rendering (2026-06-06, final).** Settled after a same-day round-trip: briefly removed *all* backend rendering (kept the PWA as renderer), then reversed — the **C# backend (`LabelRenderer`/SkiaSharp) is the single renderer**. It serves the preview PNG and the print job; the **PWA fetches both and never rasterizes**. The Node `lablr-render` sidecar is **dropped** (it only existed to share the PWA's TS render code — moot once the PWA stops rendering). This makes **item 29 (headless/server-side printing) DONE**: MCP `print_draft` renders + relays to a bridge, so the AI prints with no app open. See LOGBOOK 2026-06-06 + memory `backend-canonical-render`.
- ~~**6.** Minimal label editor~~ — **dropped**: the PWA renders/prints, it doesn't author; values come from AI-created drafts.
- ~~**45.** Remove presets~~ — **reverted, settled**: presets are KEPT. A use case ("Aida box", "Vial", "Bucket") is a named template+media; presets give consistency (same thing → same output). The print page (46) is the override. Don't relitigate — see CLAUDE.md.
- ~~**35.** Auto-fit text (initial idea)~~ — **folded into 39/40/43**.
- **15–21** — removed from scope (history/reprint/search, HTML/CSS templates, live reload, git config, Claude-assisted editing). Numbers retired, not reused.

---

> **Guiding principle:** Keep questioning every feature that does not directly reduce the time between needing a label and having a label in your hand.
