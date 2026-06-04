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
23. [ ] Add support for multiple media sizes
24. [ ] Add support for additional printers
25. [ ] Add a simple server/API if needed
26. [ ] Add draft persistence if needed
27. [ ] Add ChatGPT/MCP draft creation
28. [ ] Add ChatGPT → open draft → print workflow
29. [ ] Add headless/server-side printing if it becomes useful
30. [ ] Add remote printer support if it becomes useful
31. [ ] Add deployment and configuration packaging

> **Guiding principle:** Keep questioning every feature that does not directly reduce the time between needing a label and having a label in your hand.
