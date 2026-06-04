# Lablr — Logbook

Append-only record of brainstorms and decisions. Newest entries go at the bottom. Don't rewrite history — if a decision is reversed, add a new entry explaining why.

---

## 2026-06-04 — v1 direction: frontend-first

**Decision:** v1 is a frontend-first React PWA. The PWA is the *canonical renderer*. No backend rendering, no Chromium/Puppeteer, no database, no MCP in v1.

Pipeline (all client-side):
```
draft + template → canvas at exact dot size → threshold to 1-bit bitmap
                 → that same bitmap is BOTH the preview AND the print payload
```

**Why:** The product's whole point is minimizing friction ("I have something. I need a label."). An earlier plan drifted into a heavy stack (.NET API + headless Chromium renderer + Docker image + print adapters + config service). That's too much for v1 and contradicts the low-friction goal.

---

## 2026-06-04 — Preview = print is an invariant, not a feature

**Decision:** "Preview and print use the same bitmap" is treated as a design constraint, true by construction — not a task to bolt on later.

**Why:** It holds only if there is ONE bitmap that the preview displays and the printer receives. The moment two code paths produce pixels, it's false. So the editor → canvas → bitmap → print path must obey this from the start.

---

## 2026-06-04 — Templates: declarative YAML layout first, HTML/CSS later

**Decision:** v1 templates are a small declarative YAML layout the canvas renderer interprets directly (stacks, text fields, font size/weight/align; barcode/QR later). HTML/CSS templates are deferred as a *later alternative renderer*.

**Why:** Faithful HTML/CSS rasterization needs a real browser engine — that's what pulled the plan toward Chromium. A declarative layout is far lighter, still Claude-editable, still Git-friendly, and decoupled from the renderer so HTML/CSS can return later.

---

## 2026-06-04 — Roadmap reordered to lead with hardware risk

**Decision:** Adopted a sequential roadmap (see ROADMAP.md) that proves printing to the real printer *early*, before building the editor/templates/config.

Notes captured during review:
- The first print proof should use a **fake/hardcoded bitmap** (e.g. a black rectangle), to separate "can I talk to the printer?" (transport + protocol) from "can I render a label?" (canvas pipeline). Prove the transport in isolation — it's the scariest unknown.
- Consider pulling "reprint last label" earlier; let "search history" drift later.

---

## 2026-06-04 — Open questions (gating early roadmap items)

- **First printer target?** Leaning Niimbot (BLE, well-documented community protocols). A Dymo is also on hand, but many Dymo LabelWriters are USB-only, which would rule out Web Bluetooth.
- **Print device platform — Android or iPhone?** Hard fork: Web Bluetooth works on Android/desktop Chrome but **not** iOS Safari. iPhone would force a different transport (local bridge / native wrapper).
- **Config delivery — bundled at build time, or fetched at runtime?**
