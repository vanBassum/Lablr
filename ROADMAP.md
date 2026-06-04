# Lablr — Roadmap

- [x] Define the end-to-end workflow from "I need a label" to "label printed"
- [ ] Choose the first printer target (likely Niimbot)
- [ ] Decide how printing works (Web Bluetooth, local bridge, etc.)
- [ ] Prove a label can be printed to the target printer
- [ ] Define the draft model (the data required to print a label)
- [ ] Create a minimal label editor
- [ ] Create a minimal preview
- [ ] Ensure preview and print use the same bitmap
- [ ] Create the first hardcoded template
- [ ] Create the first printable label (e.g. BC547)
- [ ] Introduce reusable templates
- [ ] Introduce physical media definitions (size, material, manufacturer SKU)
- [ ] Introduce presets (Aidetek Small, Chemical Bottle Small, Storage Bin Large)
- [ ] Automatically resolve preset → template + media + printer
- [ ] Add label history
- [ ] Add duplicate/reprint functionality
- [ ] Add search in history
- [ ] Move templates into HTML/CSS
- [ ] Add live template reload during development
- [ ] Add Git-based template/media/preset configuration
- [ ] Add Claude-assisted template editing workflow
- [ ] Add support for multiple templates (SMD, chemical, storage)
- [ ] Add support for multiple media sizes
- [ ] Add support for additional printers
- [ ] Add a simple server/API if needed
- [ ] Add draft persistence if needed
- [ ] Add ChatGPT/MCP draft creation
- [ ] Add ChatGPT → open draft → print workflow
- [ ] Add headless/server-side printing if it becomes useful
- [ ] Add remote printer support if it becomes useful
- [ ] Add deployment and configuration packaging

> **Guiding principle:** Keep questioning every feature that does not directly reduce the time between needing a label and having a label in your hand.
