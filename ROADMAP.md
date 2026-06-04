# Lablr — Roadmap

> **Item numbers are stable IDs.** The logbook refers to items by number (e.g. "roadmap item 1"), so never renumber. Add new items with the next free number; don't reuse numbers of removed items.

1. [x] Define the end-to-end workflow from "I need a label" to "label printed"
2. [x] Choose the first printer target — prototype: Dymo LabelWriter 450 (USB); production: Niimbot (Bluetooth)
3. [x] Decide how printing works — one printer-transport interface, first impl WebUSB from desktop Chrome; Web Bluetooth added later
4. [x] Prove a label can be printed to the target printer — Dymo 450 over WebUSB, render→preview→raster→print. (Alignment offset is a media/printer-profile concern, deferred to items 12–14.)
5. [x] Define the draft model — a draft is **data only** (`{ label?, values }`), NOT bound to a template. The same draft renders on any **compatible** template (template's fields ⊆ draft's values), so one chemical → big bucket label or small vial label. Templates own the field schema.
6. [ ] ~~Create a minimal label editor~~ — **dropped**. The PWA renders/prints; it does not author. Field values come from AI-created drafts (items 27–28), not manual entry. (Number kept per stable-ID rule.)
7. [x] Create a minimal preview — canvas render of a draft via the declarative layout
8. [x] Ensure preview and print use the same bitmap — one canvas is both preview and print payload, by construction
9. [x] Create the first hardcoded template — `smd-basic` (name/subtitle/package) + sample drafts
10. [x] Create the first printable label (e.g. BC547) — BC547 sample draft prints
11. [x] Introduce reusable templates — declarative **YAML** format (mm units, center-origin), loaded by id; adding a template = adding a YAML file. Layout is renderer-interpreted (not HTML/CSS — that stays deferred, item 18).
12. [x] Introduce physical media definitions (size, material, manufacturer SKU) — YAML media profiles in `public/config/media/` (first: S0929120). Render bounds come from the media; media also homes the calibrated head **offset** (the deferred item-4 value). DPI stays in the printer module.
13. [x] Introduce presets — reusable named **(template + media)** output formats in `presets.json`. A draft auto-offers every preset whose template fits its data, so one draft → multiple outputs (e.g. chemical "Vial" + "Bucket"). (Briefly dropped for single-output; the multi-output need brought them back, scoped to template+media, not printer.)
14. [x] Resolve draft → presentation — draft's suggested template (overridable) + loaded media + connected printer. No preset indirection.
15. [ ] Add label history
16. [ ] Add duplicate/reprint functionality
17. [ ] Add search in history
18. [ ] Move templates into HTML/CSS
19. [ ] Add live template reload during development
20. [ ] Add Git-based template/media/preset configuration
21. [ ] Add Claude-assisted template editing workflow
22. [ ] Add support for multiple templates (SMD, chemical, storage) — mechanism + UI template selector done (smd-basic, storage-box); add the remaining real templates
23. [x] Add support for multiple media sizes — 3 real rolls: 25mm square (S0929120), 54×70, chemical-resistant 54×101 (LD 014 02.MMXIII)
24. [~] Add support for additional printers — printer **profiles** introduced as YAML (`config/printers/`, first: dymo-450), identity only for now; media link to their printer via `media.printers` (and carry the per-roll print offset). (Still single printer + WebUSB; DPI/head width remain in the driver, multi-printer + Bluetooth selection later.)
25. [ ] Add a simple server/API if needed
26. [ ] Add draft persistence if needed
27. [~] Add ChatGPT/MCP draft creation — **serverless, no MCP server, no Custom GPT**: a generated `llms.txt` (+ `catalog.json`) is published on Pages with the protocol + live templates + an example; `index.html` `<noscript>` points to it. Any browsing chat reads it and builds a `#/draft?t=…&field=…` URL — paste the site link, ask for a part, get a label link. (Needs real-world testing in ChatGPT/Claude.)
28. [~] Add ChatGPT → open draft → print workflow — PWA `#/draft` deep-link route done: a link opens straight into the detail view, ready to print. AI-emitted link pending the GPT setup; printing PC-off still needs the Bluetooth printer (USB Dymo needs the PC).
29. [ ] Add headless/server-side printing if it becomes useful
30. [ ] Add remote printer support if it becomes useful
31. [~] Add deployment and configuration packaging — GitHub Pages deploy workflow (Actions) builds `lablr-ui` → https://vanbassum.github.io/Lablr/ (Vite `base: /Lablr/`). Config + `catalog.json` served statically alongside.
32. [x] Support landscape/portrait orientation — per-print toggle; renderer rotates the design 90° within the physical label (head width is fixed, so the bitmap stays the media's physical size)
33. [x] Persistent printer connection — connect once (open + claim) held in a PrinterProvider; each print is just transferOut. Silent reconnect on load via `navigator.usb.getDevices()`; unplug detected via the `disconnect` event. Header shows connect/disconnect.
34. [x] Offset calibration aid — "Print alignment pattern" (border + corner-to-corner cross + center crosshair, sized to the media) in the gear, printed with the current offset; nudge X/Y and reprint to dial in each media's offset.
35. [ ] Auto-fit text — give template text fields a bounding box and scale the text down to fit, so long values don't overflow/clip
36. [ ] **Drafts reference Presets, not Templates** — change draft schema from `template: smd-basic` to `preset: smd`. Presets are user-facing concepts (Vial, Bucket, SMD label, Storage box); templates are renderer implementation. Reason: users think in terms of output formats, not layout mechanics.
37. [ ] **Rename `values` to `fields` in drafts** — align naming with template data model. A template defines which fields are needed; a draft provides field values. Schema: `draft.fields: { key: value }` instead of `draft.values`.
38. [ ] **Replace `size` with `designSize` in templates** — move physical sizing out of templates. `designSize` is the coordinate system for layout (e.g. 50mm × 20mm canvas). Physical output size is determined by the preset's media, not the template. Reason: a template should be reusable across different media sizes.
39. [ ] **Replace stack/flexbox layouts with rectangular elements** — move from layout trees (`type: stack`, `direction`, `gap`) to explicit rectangles. Each text element lives in a `rect { x, y, width, height }` (mm). Reason: labels are small; explicit bounds make it trivial for the renderer (and AI) to guarantee text fits.
40. [ ] **Auto-fit text to rectangles** — implement shrink-on-overflow: start at max font size, measure, reduce size if text doesn't fit, repeat until it fits. Add `font { maxSize, minSize, weight }` to text elements. Reason: labels need tight layouts; auto-fit eliminates manual tweaking and overflow risk.
41. [ ] **Add text alignment in rectangles** — support `align: left | center | right` and `valign: top | center | bottom`. Reason: fine-grained control needed for small label rectangles.
42. [ ] **Add text wrapping support** — add `wrap: true | false` and optional `maxLines: N`. When text wraps and exceeds maxLines, reduce font size further. Reason: short chemical names don't always fit; wrapping + auto-fit handles long names gracefully.
43. [ ] **Introduce fit mode `shrink`** — add `fit: shrink` directive (future modes: stretch, clip, ellipsis). Shrink mode reduces font size until content fits within the rectangle. Reason: provides a clear, composable directive for text overflow handling.
44. [ ] **Update template schema to new structure** — target: `designSize`, `fields: { key: { required, type, label } }`, `elements: [ { type, field/text, rect, align, valign, font, wrap, fit } ]`. Deferred: HTML/CSS templates (item 18), barcode/QR (item 9), future elements. Reason: new structure is declarative, AI-friendly, and guarantees fit by construction.
45. [ ] **Preserve core architecture** — maintain the hierarchy `Draft -> Preset -> (Template + Media) -> Printer`. The update refines positioning (drafts reference presets, not templates; physical size in media, not template), but the decoupling holds. Reason: decoupling keeps the system composable and easy to understand.

> **Guiding principle:** Keep questioning every feature that does not directly reduce the time between needing a label and having a label in your hand.
