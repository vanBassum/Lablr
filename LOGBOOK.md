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

---

## 2026-06-04 — Media definitions; offset gets a home (item 12)

Introduced **Media** — the physical label profile — as runtime YAML config (`public/config/media/` + manifest, same pattern as templates). First entry: `s0929120.yaml` (25×25mm, SKU S0929120, direct-thermal).

**Design choices:**

- **Media owns:** physical `size` (mm), `sku`, `material`, and the calibrated **`offset`** (mm) — where the label sits on the print head. This is the **home for the deferred item-4 alignment offset.** Currently `{0,0}` (uncalibrated); editing the YAML + reloading re-calibrates. Noted that with multiple printers it becomes a printer×media calibration.
- **DPI is NOT on the media** — it's the print head's resolution, so it stays in the printer module (`dymo.ts`, `mmToDots`). The renderer receives mm and converts; it never hardcodes DPI itself (honors CLAUDE.md's invariant). Printer profiles are item 24.
- **Render bounds = media size**, not template size. The template's `size` is now a *design hint*; layout (mm, center-origin) is centered within the actual media. A template "fits" a media when `template.size ≤ media.size` (`templateFitsMedia`) — otherwise the UI warns it will clip.

**UI:** added a Media dropdown (Draft · Template · Media). `chemical-large` (50×30) on the 25mm media now shows a clip warning — concrete proof that media constrains what prints. The offset sliders are reframed as a *nudge* on top of the media's calibrated offset (a calibration aid; the real value lives in the media YAML).

**This is the second of the three compatibility relations presets need:** draft↔template (fields) and template↔media (size). Presets (items 13–14) will resolve draft → (template + media + printer) using both.

---

## 2026-06-04 — Preview matches label size; offset moved to printer commands

The preview canvas was the full 672-dot head width (a wide strip) with the offset shifting content inside it. Changed so **the bitmap is the label itself** — canvas is sized to the media (e.g. 296×296 dots for 25mm) and shown on screen at the label's real aspect ratio.

**Consequence — cleaner offset model:** the head-positioning offset is no longer baked into the bitmap. It's now applied as **printer commands** (`HeadOffset` in `dymo.ts`): X via **ESC B** dot-tab (whole bytes, 8-dot granularity), Y via **leading blank raster lines**. So the SYN bitmap sent to the printer is byte-identical to the previewed canvas — this *strengthens* preview = print (the label's pixels are now exactly equal; positioning is separate). The media's `offset` (mm) + the UI nudge are converted to dot-tab bytes + blank lines at print time. Note: ESC B byte-granularity makes X calibration coarse (~0.68mm steps); fine centering is handled by the center-origin layout, so this is fine for landing the label.

`renderLabel` no longer takes offsets; bytes-per-line now follows the label width (37 for 25mm) instead of always 84.

---

## 2026-06-04 — Presets dropped; the draft suggests a template (items 13–14)

**Question raised:** do we want presets at all, or should the draft carry template + media (overridable)?

**Decision: drop presets; the draft carries an optional, overridable *suggested template* — not media.** Reasoning:

- Presets exist mainly so a *human* picks one thing instead of three. But we already dropped the manual editor (item 6) — **the AI creates drafts**, and the AI can pick a template directly. So presets' main value evaporates in an AI-driven flow.
- **Media is physical runtime state** (which roll is loaded), which ChatGPT cannot know when it mints a draft + link. So media must NOT be baked into a draft. The template (a presentation choice) *can* be suggested.
- So: `Draft` gains optional `template?` (a hint). Media is whatever's loaded (auto-picked when one fits); printer is whatever's connected. This gives one-tap printing (open link → suggested template + loaded media → preview → print) while preserving data ⟂ presentation — the suggestion is freely overridable (sodium chloride still prints big or small).

This **reverses CLAUDE.md's "users pick a preset" model**; CLAUDE.md "The model" section updated to Draft / Template / Media / Printer. Items 13 (presets) dropped, 14 reframed to draft→presentation resolution (done). Sample drafts now carry a `template` suggestion; the UI defaults to it on draft change.

---

## 2026-06-04 — Dev server exposed on LAN for phone testing

`dev` script now runs `vite --host` so the PWA is reachable from the phone (the production target) at `http://<pc-lan-ip>:5173` for UI/preview/ergonomics testing. **Caveat:** WebUSB needs a secure context (localhost or HTTPS), so the phone over plain-HTTP LAN can view/preview but **cannot print** — printing to the USB Dymo stays on the PC. (Phone printing is the later Bluetooth-Niimbot / HTTPS path.)

---

## 2026-06-04 — Phone-first UX with shadcn

Replaced the dev test page with a real **phone-first** UX (`App.tsx` shell + `components/PrintScreen.tsx`), built on shadcn (`radix-nova` style): Card, Select, Badge, Label, Separator added via the CLI.

Layout: sticky header (title + light/dark toggle) → centered label **preview** (white framed card, scaled to the media's real aspect, longest edge ≤280px) with a Fits/Clips badge → touch-friendly **Select**s for *What to print* (draft) · *Template* · *Label (loaded media)* → a big **sticky bottom Print bar** (thumb-reach, safe-area padding) with inline status. Column is `max-w-md` so it reads as a phone screen on desktop too.

Offset-nudge inputs and the WebUSB diagnostics probe moved into a collapsible **Advanced** section — they're calibration/dev concerns, off the main path.

**Note:** the draft→template defaulting was reworked from a setState-in-effect into a render-phase reset (track `seenDraft`, clear the override when the draft changes) — the lint rule `react-hooks/set-state-in-effect` flagged the effect version; the render-phase pattern is React's recommended alternative and avoids cascading renders.

---

## 2026-06-04 — Two-screen flow: browse grid → detail/print + gear

Reworked the UX into the flow the user described:

- **Browse** (`LabelApp` + `DraftCard`): a 2-column grid of draft cards, each rendering a live mini label preview (its suggested template + a fitting media). Scroll/swipe through; tap a card to open it.
- **Detail/print** (`DraftDetail`): back button + large preview + a sticky bottom bar with a big **Print** button and a **gear** (Settings2) next to it. The gear opens a **Drawer** (vaul bottom sheet) holding Template + Media selects, with offset-nudge + WebUSB diagnostics under an Advanced disclosure.

`LabelCanvas` is a shared preview component (white frame, scaled to longest edge `maxEdgePx`); it takes an optional `canvasRef` so the detail view can read the canvas to print it. Shared `draftName` + `pickTemplate`/`pickMedia` resolvers in `label/templates.ts` keep cards and detail consistent. Replaced the single `PrintScreen` with `LabelApp`/`DraftDetail`/`LabelCanvas`. Added shadcn `drawer` (vaul).

---

## 2026-06-04 — Presets reintroduced (refined) for multi-output drafts (item 13)

**This re-reverses the earlier "presets dropped" decision** — but for a reason the original analysis didn't cover. We dropped presets assuming each draft has **one** output (AI picks the template). The user surfaced a real **multi-output** need: acetone wants a *big bucket* label **and** a *small vial* label — one piece of data, several deliberate presentations. That's precisely what presets are for.

**Refined definition:** a **preset = a named, reusable (template + media) output format** (`presets.json`): `Vial` = chemical-small + s0929120; `Bucket` = chemical-large + dymo-99014. Key properties:

- **Reusable, not per-draft.** Defined once; a draft **auto-offers every preset whose template fits its data** (`compatiblePresets`, same `fields ⊆ values` check). So every chemical draft gets Vial + Bucket with zero duplication. (Chosen over the alternative of listing template/media pairs inside each draft, which repeats them.)
- **Scoped to (template + media), not printer.** Printer is still whatever's connected.
- **Media physicality stands:** a preset names a media, but you can only physically print the one whose roll is loaded. Preview works for any; a "roll not loaded" warning is a later nicety.

**UI:** the detail screen shows compatible presets as **chips** (the big/small choice) above Print — elevated from the gear to the main screen. The gear now holds **calibration & diagnostics** (offset nudge + WebUSB probe) rather than template/media selects, since presets are the curated template+media choice. Added media `dymo-99014` (54×101mm) and resized `chemical-large` to fill it so "Bucket" is a real output. CLAUDE.md model section updated to Draft / Template / Media / Preset / Printer.

---

## 2026-06-04 — Real media inventory + landscape/portrait (items 23, 32)

**Media inventory** — replaced the invented placeholder with the user's actual rolls (`public/config/media/`): `s0929120` (25mm square), `dymo-54x70` (54×70, no SKU), `chem-resistant` (54×101, marking "LD 014 02.MMXIII"). Chemical presets now offer **Vial** (25mm) · **Bucket** (54×70) · **Chem-resistant** (54×101); `chemical-large` resized to 54×70 so it fits. Item 23 done.

**Orientation (item 32)** — added a per-print **portrait/landscape toggle** on the detail screen. Implementation: the print head width is fixed, so the bitmap is always the media's physical `w×h` dots; landscape lays the design into a *swapped* area (label height × width) and rotates it 90° onto the bitmap (`renderLabel(…, orientation)`). `templateFitsMedia` is orientation-aware (swaps the label's dims). Kept as a per-print toggle (default portrait) rather than a preset/template field — simplest, most "mode"-like; can be promoted to a preset default later if wanted. Rotation direction is a guess (clockwise) — verify on the device; trivial to flip.

**Update:** orientation promoted to a **preset** field (`orientation?`, default portrait), per the user. NOT the template — a template's `size` aspect already implies its orientation, and putting it there would duplicate near-identical templates and conflate design with placement. A preset is the complete output spec = template + media + orientation, so it drives the right thumbnail/default; the per-print toggle remains as an override (reset to the preset's value when the preset changes).

---

## 2026-06-04 — Persistent printer connection (item 33)

Printing used to `requestDevice → open → claim → print → release → close` on **every** Print press (re-prompting / re-opening each time). Now the connection is established **once** and held:

- New `PrinterProvider` (`src/printer.tsx`) keeps the open+claimed `UsbDevice` in a ref; `print()` is just a `transferOut`. Connects once on first print (or via the header), reuses thereafter.
- **Silent reconnect on load** via `navigator.usb.getDevices()` — after the first grant, the printer reconnects with no prompt on subsequent loads.
- **Unplug handling** via the WebUSB `disconnect` event → status flips to disconnected.
- Header shows a **Connect/Printer** chip (USB icon, green when connected) to connect/disconnect manually.
- `dymo.ts` gained `getGrantedDymo()` + `onUsbDisconnect()`; `openDymo()` refactored to share a `claim()` helper. `DraftDetail` now prints through `usePrinter().print()` instead of opening/closing per press.

(Same caveat as before: WebUSB needs localhost/HTTPS, so this is the dev/desktop path; phone-over-LAN-HTTP still can't print.)

---

## 2026-06-04 — Offset fix (compositing) + 1-bit threshold (darker thin lines)

**Offset wasn't moving the print.** It was applied via printer commands — `ESC B` dot-tab (X) and leading blank lines (Y) — which the LabelWriter 450 ignores in practice. Replaced with **pixel compositing**: `printCanvas` draws the label canvas onto a full-head-width (672-dot) bitmap at the offset and sends that. Pure pixels always move, and X is now **per-dot** (was byte-coarse). `HeadOffset` changed from `{dotTabBytes, topBlankLines}` to `{x, y}` in dots; `buildJob` no longer does positioning. (`preview = print` holds for the label content — the print just composites that identical bitmap onto the head.)

**Thin lines printed faint** — anti-aliased thin strokes render as gray pixels above the black cutoff. Added `thresholdTo1Bit` in the renderer (cutoff `INK_THRESHOLD = 176`) applied at the end of `renderLabel` and `renderCalibration`, so the canvas becomes pure black/white: thin lines round to solid black, and the **preview now shows the true 1-bit result** (tightening preview = print, which previously showed smooth gray the printer wouldn't reproduce). One knob (`INK_THRESHOLD`) tunes overall darkness.

The calibration panel now also shows the current offset in **mm** (nudge is in dots; media YAML `offset` is mm) so the value is copy-pasteable.

---

## 2026-06-04 — Printer profiles; dead zone is a printer trait (item 24, partial)

The leading-edge dead zone is a **printer** property (gap-sensor → head distance), identical for every roll — so storing it per-media was wrong. Introduced **printer profiles** as YAML config (`config/printers/`, first `dymo-450.yaml`) holding `topMarginMm`. And, per the user, **media link to compatible printers** via `media.printers: [ids]` (a DYMO roll fits the LabelWriter, not the Niimbot).

The print-placement offset is now split by ownership:

- **Y (dead zone)** → `printer.topMarginMm`, shared by all rolls (`printerForMedia` resolves the media's printer; first compatible, else first).
- **X (position across head)** → `media.offset.x`, per roll.
- Manual nudge in the gear still finds the values; the calibration hint says which knob each axis feeds. Preview stays untouched (offset is print-only).

Wiring: `loadPrinters()` + `printerForMedia()` in `templates.ts`, `parsePrinter()` in `load.ts`, `Printer` type, `LabelApp` loads + passes printers to `DraftDetail`. DPI + head width still live in `dymo.ts` (single printer); they migrate to the profile when multi-printer / Niimbot lands. Item 24 marked partial (`[~]`).

**Correction (same day), two things from calibration:**

1. The Y correction is **negative** (~-20 dots on the 25mm, -22 on the 54×70 — the LW450 prints ~1.7mm *low*), so it's a signed *placement offset*, not a "dead zone / top margin" (which would be positive). So `topMarginMm` was the wrong shape.
2. **Offsets live per physical media, not per printer.** A roll is always loaded on a *specific* printer, so the (printer + roll) calibration is uniquely the media's — and the per-roll values differ anyway (-20 vs -22), so per-media is *more accurate*, not just simpler. So the brief "dead zone on the printer" split above is **reverted**: the printer profile drops back to **identity** (id, name) and each media carries its own `offset: {x, y}` (mm). Final placement = `media.offset` + calibration nudge; printer link (`media.printers`) is just for identity/targeting (the target printer name now shows under the preview). Measured: `s0929120 {-0.5, -1.7}`, `dymo-54x70 {0, -1.86}` (X still to calibrate).
