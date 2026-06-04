# Lablr — Logbook

Append-only record of brainstorms and decisions. Newest entries go at the bottom. Don't rewrite history — if a decision is reversed, add a new entry explaining why.

---

## 2026-06-04 — End-to-end workflow defined (roadmap item 1)

**The real happy path** (workshop scenario): PC is off. A package arrives with parts. User grabs their phone and tells ChatGPT *"I need a label for this device, I want to store it in my Aidetek Small container."* ChatGPT creates a draft. User opens the PWA, sees the preview, taps Print. Label prints.

**v1 target flow (achievable, matches frontend-first):**
```
Talk to ChatGPT → it creates a DRAFT (data only: preset + field values)
              → draft saved on the HOSTED server (vanbassum.com, not the PC)
              → phone PWA opens the draft → renders → shows preview → one tap → prints over Bluetooth
```

Key points:
- ChatGPT never renders or prints. It only fills in draft *data*. The phone stays the canonical renderer AND the printing device — consistent with frontend-first.
- "PC is off" works because the draft lives on the hosted server, not the user's machine.
- This makes a small **server + draft storage required** for the real happy path (store a draft / fetch a draft — no rendering on the server). It is no longer "someday, if needed."

**Platform:** User is on **Android** → Chrome supports Web Bluetooth + PWA install. The phone-centric flow is viable; no iOS limitation to design around.

**The "even better" dream** — ChatGPT shows the draft and you just say "print it", never opening the PWA — is deferred, blocked by two hard walls:
1. ChatGPT can't talk to a Bluetooth printer (Web Bluetooth needs a browser + a user gesture on the phone).
2. ChatGPT showing the *real* preview would require server-side rendering, which violates "preview = print" (no second renderer).

This dream only becomes possible with a **networked (WiFi) printer** + server-side rendering — a heavier architecture, noted as a possible future, not v1.

**Consequence for roadmap item 2 (choose printer):** the printer choice decides which future is open. Bluetooth (e.g. Niimbot) → phone-driven flow (v1 target above). Networked printer → server could print directly, enabling the hands-free ChatGPT dream later.

**Note:** This does not change build order — we still prove plain printing first, before any ChatGPT wiring.

---

## 2026-06-04 — Refinement: ChatGPT hands off via a deep link

ChatGPT can return a **link** after creating the draft. That's the handoff mechanism:
```
talk to ChatGPT → it POSTs a draft to the server → server returns a draft link
   → user taps link → PWA opens DIRECTLY on that draft → preview → one tap → print
```

**Why this is good:**
- Removes the last navigation friction — no opening the app and hunting for the draft; the link lands you on it.
- Sidesteps the "ChatGPT can't show the real preview" wall entirely — it doesn't need to; the link carries you into the PWA where the single real preview lives.
- Still fully frontend-first: ChatGPT only produces draft *data* + a URL; the phone renders/previews/prints.

**Implication for later:** the server must mint a draft ID and the PWA needs deep-link routing (`/draft/:id`). Both small.

---

## 2026-06-04 — First printer + transport locked (roadmap items 2 & 3)

**Decision:** prototype against a **Dymo LabelWriter 450** already on hand, driven over **WebUSB from desktop Chrome on the dev PC**. Niimbot (Bluetooth) remains the production target.

**Why the Dymo first:**
- Already owned — fastest way to prove the rendering→print pipeline (roadmap items 4–10) against real hardware.
- The **450** is the freely-usable model: no RFID label lock (that's the 550 — avoid), third-party rolls fine, open/documented raster protocol, lots of OSS prior art.
- Direct thermal (fades) and 300 DPI — fine for a prototype. DPI is already a media/printer-profile property, so this difference from the Niimbot is handled by construction.

**Why WebUSB-from-PC first (not BLE, not OTG):**
- The 450 is **USB only** — no Bluetooth at all, so the Web Bluetooth path can't apply to it.
- Printing from desktop Chrome over WebUSB needs **no USB-OTG and no BLE**, removing every transport unknown for the prototype. (Driving a Dymo over WebUSB through Android OTG is plausible but unverified — deferred, not on the critical path.)

**Consequence (reverses the earlier "Web Bluetooth only" framing):** transport is no longer Bluetooth-only. It sits behind a single `Printer` interface with swappable implementations — **first impl WebUSB (desktop), second impl Web Bluetooth (Niimbot on Android)**. This abstraction is needed anyway for multiple printers (item 24); the Dymo just forces it early. The bitmap pipeline is identical across transports — only byte-delivery differs, so "preview = print" is unaffected. CLAUDE.md "Platform & transport" updated accordingly.

---

## 2026-06-04 — WebUSB connection to Dymo 450 proven (roadmap item 4, connection half)

Built a minimal WebUSB probe page in the PWA and confirmed desktop Chrome can take **raw ownership** of the LabelWriter 450.

**Device facts (read off the live device):**
- `vendorId=0x0922` (DYMO), `productId=0x0020` (LabelWriter 450).
- Interface 0, USB printer class (class 7, sub 1, proto 2, bidirectional).
- Endpoints: **ep#2 bulk OUT** (push raster here) and **ep#2 bulk IN** (read status), 64-byte packets.

**The Windows gotcha, confirmed and solved:** out of the box, `device.open()` returned **"Access denied"** — Windows' inbox printer driver (`usbprint.sys`) owns the device, and Chrome can't open a device another kernel driver holds. Fix: **Zadig → bind WinUSB to the 450**. After that, `open()` + `claimInterface(0)` both succeed.

**Implications:**
- WinUSB rebinding means the 450 no longer prints via normal Windows/DYMO software while bound — fine, the prototype only wants raw WebUSB. Reversible via Device Manager → uninstall device.
- This friction is **desktop-Windows-specific**. It reinforces that desktop WebUSB is a *dev convenience*, not the production path (Android/Niimbot over Bluetooth). The Android-OTG-WebUSB question (does it need the same rebinding?) stays deferred.

**Next (item 4, print half):** format raster bytes per the LabelWriter command language and push to ep#2 OUT. Must verify the exact command set against a reliable source (CUPS `rastertolabel` / DYMO LW Technical Reference) before sending — don't guess at hardware.

---

## 2026-06-04 — First real label printed; item 4 complete

A physical label came off the Dymo 450: full pipeline **render canvas → on-screen preview → 1-bit raster → WebUSB → print**, with preview = print holding by construction.

**Verified LW450 raster protocol** (from CUPS `rastertolabel.c` + DYMO LW450 command manual), now in `lablr-ui/src/dymo.ts`:

- 300 DPI, 672-dot (84-byte) head. SYN (`0x16`) + N data bytes per line, MSB = leftmost dot, bit 1 = black.
- Sequence: `0x1B`×100 + `ESC @` reset → `ESC L hi lo` (length in dots) → `ESC D n` (bytes/line) → per-line SYN → `ESC E` eject.
- S0929120 media = 25×25 mm = ~296 dots = 37 bytes (we now render full-head-width for calibration).

**Known issue, deliberately deferred — alignment offset.** Print lands shifted (content too high; top border/triangle clipped). This is **not a renderer bug**: it's the physical position of the label on the 672-dot head + leading-edge start, which is **per-media and per-printer**. It belongs to the Media/Printer profile work (**roadmap items 12–14**), not to v1 plumbing. Decision: do **not** hardcode an offset into the throwaway test; the media model will own this value. A calibration tool (X/Y dot offsets, preview=print preserved) was added to the test page to *measure* the value when needed.

**Guiding-principle check:** perfecting alignment before the media model exists would be premature complexity that doesn't reduce time-to-label. Item 4's goal was "prove a label *can* be printed" — achieved. Move on to the draft model (item 5).

---

## 2026-06-04 — Draft model: the template owns the fields (items 5, 7, 8, 9)

**Caught a framing error in item 5.** "Define the draft model (the data required to print a label)" implied a fixed field schema. But the **template** decides which fields exist and can be overwritten — those fields differ per template (an SMD part label ≠ a chemical bottle label). So there is no single fixed draft schema.

**Resolved to a two-layer model:**

- **Template** owns the field schema *and* the layout. `fields: [{ key, label, type }]` + a declarative `layout` (vertical stack + text nodes for v1; a text node binds to a field by `key` or holds a literal).
- **Draft** is a generic, stable envelope: `{ templateId, values: Record<key, string> }`. It references the template **by id** and must supply a value for **every** declared field.
- The renderer resolves `template + values → canvas`, and that one canvas is both the preview and the print payload (preview = print holds by construction).

**Decisions (from the user):**

- Draft references template **by id**, not inline — keeps drafts tiny for the ChatGPT→link handoff and makes templates the single source of truth for fields.
- **No field defaults** for now; a draft must supply all values. Optional/hidden fields are a possible later refinement, not v1.
- Templates ship with **sample drafts** as design-time fixtures, so a template can be previewed against real data while being built.

**Built (`lablr-ui/src/label/`):** `types.ts` (Template/Draft/layout), `templates.ts` (`smd-basic` + 3 sample drafts: BC547, LM358, 100nF), `render.ts` (declarative-layout canvas renderer). The print test page is now data-driven — pick a draft, it re-renders, prints the same bitmap. This closes items 5, 7, 8, 9; item 6 (editing field values) and item 10 (confirm BC547 on hardware) remain.

**Note:** config-as-code in the PWA for now; templates/drafts move to the label-config repo later (item 20).

---

## 2026-06-04 — Template format: YAML, millimetres, center-origin (item 11)

Moved templates from inline TS to a declarative **YAML** format (parsed with `js-yaml`, imported via Vite `?raw`). First template `smd-basic.yaml` lives in `lablr-ui/src/label/templates/` and carries its own `samples:` fixtures.

**Design decisions (with the user):**

- **Units are millimetres, origin is the label center.** Rationale: mm is resolution- and printer-independent — the renderer maps mm→dots via the media's DPI, so a template renders at the same *physical* size on a 300 dpi Dymo or a 203 dpi Niimbot. Chosen over normalized/fraction units (which auto-fill but make text size track label size and distort aspect ratios) and over device dots (not portable). Crisp + readable + predictable wins for labels.
- **Offset does NOT live in the template.** The per-label alignment offset (the deferred item-4 issue) is *physical calibration*, a property of (media + printer), so it belongs to the media/printer profile (items 12–14), not the design. Templates stay media-agnostic.
- **Scale is deferred (YAGNI).** "One template on multiple physical labels" via an explicit scale factor is real but speculative while only one label exists. mm + center origin makes it a clean future add; don't build the binding now.
- Draft model unchanged: `{ templateId, values }`, references by id, supplies all values.

**Format shape:** `{ id, name, size:{w,h}mm, fields:[{key,label}], layout: <stack|text tree>, samples:[...] }`. A `text` node binds to a field via `field` or holds a literal via `text`; `size` = cap height in mm; stacks cascade `align` to children. v1 renderer handles vertical stacks + text; horizontal stacks and barcode/QR are later.

**Guiding-principle check:** YAML + mm + center are the no-regret foundations; offset and scale were deliberately kept *out* to avoid baking physical/speculative concerns into the design layer.

---

## 2026-06-04 — Config moved to public/, fetched at runtime (not bundled)

Templates moved from `src/label/templates/*.yaml` (imported via Vite `?raw`, **baked into the JS bundle**) to **`public/config/templates/`**, fetched at runtime with `fetch()`.

**Why:** the `?raw` import contradicted the decoupled-config direction (CLAUDE.md `/label-config`; roadmap items 19–20) — editing a label would have required a rebuild + redeploy. Runtime-fetched config means **edit/add a template → just reload, no rebuild**, and the load mechanism (fetch YAML → parse) is now identical to the future served label-config repo. `public/` is the interim host.

**Mechanism:** a static folder can't be directory-listed, so a tiny **manifest** `public/config/templates/index.json` (`["smd-basic"]`) lists template ids; the loader reads the manifest, then fetches+parses each `<id>.yaml`. Adding a template = drop the file + add its id to the manifest. Loading is async, so the UI has loading/error states. Paths use `import.meta.env.BASE_URL` to survive a sub-path deploy behind Traefik.

**Not yet:** this is runtime *loading*, not git-based config (item 20) or live HMR reload (item 19 — a manual refresh re-fetches, which is enough for now).

---

## 2026-06-04 — Manual editor dropped; second template + selector (item 6 dropped, item 22 started)

**Decision (user): no manual label editor.** The PWA is a renderer/printer, not an authoring tool — drafts are created by the AI (the ChatGPT→draft→link→print workflow, items 27–28), and the bundled sample drafts are stand-ins for those. A manual field-editor would build something the architecture says shouldn't exist. **Item 6 dropped** (number retained per stable-ID rule).

**Built instead:** a second template `storage-box.yaml` (fields `code` + `contents`, different layout from smd-basic) and a **template selector** dropdown in the test page. Switching template changes the fields *and* layout entirely — concrete proof that the template, not the draft, defines what a label is. Draft labels in the UI are now generic (`Object.values(values).join(" · ")`) since fields differ per template. This lands the multi-template mechanism for **item 22** (remaining: add the real template set).

---

## 2026-06-04 — Draft decoupled from template (reverses the earlier `templateId` binding)

**User insight:** one draft should render on multiple templates — e.g. sodium chloride goes on a *big* bucket label and a *small* vial label. Same data, different presentation.

**This reverses the 2026-06-04 "draft references template by id" decision.** That coupling (`{ templateId, values }`) was wrong: it tied data to one presentation. It also contradicted CLAUDE.md's own model ("ChatGPT creates a DRAFT (data only)"; "a preset resolves to media + template + printer") — the draft was always meant to be data-only.

**New model:**

- **Draft = `{ label?, values }`** — data only, no template reference.
- **Template** owns the field schema + layout + size (unchanged).
- **Compatibility by duck-typing:** a template can render a draft when `template.fields ⊆ draft.values` (`templateAccepts`). No new "kind" entity — field-key presence is enough. (An explicit `kind` to group drafts/templates is a possible later UX refinement.)
- The render pairs *any* draft with *any* compatible template; the UI is now draft-first (pick data → choose among fitting templates).

**Config restructure:** sample drafts moved out of templates into a shared `public/config/drafts.json` (so a draft isn't owned by a template). Added `chemical-large` (50×30mm) + `chemical-small` (25×25mm) templates; the sodium-chloride/acetone drafts fit both, proving the bucket/vial scenario. `Template.samples` removed.

**Forward link:** this compatibility check is exactly what **presets** (items 13–14) will build on — a preset picks a (template + media + printer) for a draft, and only compatible templates are valid choices.
