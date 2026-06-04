# Lablr — Roadmap

> **Item numbers are stable IDs.** The logbook refers to items by number (e.g. "roadmap item 1"), so never renumber. Add new items with the next free number; don't reuse numbers of removed items.

1. [x] Define the end-to-end workflow from "I need a label" to "label printed"
2. [ ] Choose the first printer target (likely Niimbot)
3. [ ] Decide how printing works (Web Bluetooth, local bridge, etc.)
4. [ ] Prove a label can be printed to the target printer
5. [ ] Define the draft model (the data required to print a label)
6. [ ] Create a minimal label editor
7. [ ] Create a minimal preview
8. [ ] Ensure preview and print use the same bitmap
9. [ ] Create the first hardcoded template
10. [ ] Create the first printable label (e.g. BC547)
11. [ ] Introduce reusable templates
12. [ ] Introduce physical media definitions (size, material, manufacturer SKU)
13. [ ] Introduce presets (Aidetek Small, Chemical Bottle Small, Storage Bin Large)
14. [ ] Automatically resolve preset → template + media + printer
15. [ ] Add label history
16. [ ] Add duplicate/reprint functionality
17. [ ] Add search in history
18. [ ] Move templates into HTML/CSS
19. [ ] Add live template reload during development
20. [ ] Add Git-based template/media/preset configuration
21. [ ] Add Claude-assisted template editing workflow
22. [ ] Add support for multiple templates (SMD, chemical, storage)
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
