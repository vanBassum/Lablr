# Lablr — Implementation Plan

Build plan for the label printing system. Optimized for **fast workflow**, **config-as-code**, and the hard requirement that **preview = print**.

> See [CLAUDE.md](CLAUDE.md) for the project philosophy. This document is the *how*.

---

## 0. Current State

| Component | Status |
|-----------|--------|
| `lablr-api` | .NET 9 minimal API scaffold. Empty `Program.cs`. Dockerfile + docker-compose + Traefik labels ready. OIDC stubbed (commented). |
| `lablr-ui` | React 19 + Vite + Tailwind v4 + shadcn (radix-nova) + lucide. Base scaffold only. |
| `label-config` | **Does not exist yet.** Needs to be created. |

> Note: CLAUDE.md says "Vue/React" — the actual scaffold is **React**. Plan assumes React.

---

## 1. The Central Architectural Decision: Where Rendering Happens

The hard requirement **"the preview is the exact bitmap sent to the printer"** drives the whole architecture.

**Decision: the server renders. The UI only displays the server-produced bitmap.**

If the browser rendered HTML/CSS for preview while the server rendered separately for print, the two would drift (font hinting, sub-pixel AA, DPI). That violates the non-negotiable. So:

```
┌─────────┐   draft data    ┌──────────────────────────────────────┐
│  UI     │ ───────────────▶│  API                                   │
│ (React) │                 │                                        │
│         │                 │  1. Merge draft + template (HTML/CSS)  │
│         │                 │  2. Headless Chromium renders at       │
│         │                 │     exact printer pixel dimensions     │
│         │                 │  3. Threshold → 1-bit monochrome       │
│         │◀────────────────│  4. Return PNG of that 1-bit bitmap    │
│ shows   │   bitmap (PNG)  │                                        │
│ bitmap  │                 │  /print → same bitmap → printer adapter│
└─────────┘                 └──────────────────────────────────────┘
```

The **same** bitmap-generating function feeds both `/preview` and `/print`. There is exactly one rendering path.

### HTML/CSS → bitmap engine

**Recommendation: PuppeteerSharp (headless Chromium).**

- Most faithful HTML/CSS rendering — matches what we author in templates.
- Set viewport to exact pixel dimensions (see DPI math below), `deviceScaleFactor: 1`, screenshot to PNG, then threshold to 1-bit.
- Trade-off: Chromium must ship in the Docker image (~adds size, ~300ms cold per render, fast when browser is kept warm).

Alternatives considered: SkiaSharp (fast, but no HTML/CSS — defeats the template approach); wkhtmltoimage (dead project). **Keep the renderer behind an `IBitmapRenderer` interface** so it can be swapped without touching callers.

### DPI / pixel math (the bridge between mm and dots)

Thermal label printers (incl. Niimbot) are typically **203 DPI = 8 dots/mm**.

```
px = mm / 25.4 * dpi
50mm × 20mm @ 203dpi  →  400 × 160 px
```

Media defines mm + DPI → API computes exact pixel viewport → Chromium renders to that → threshold → 1-bit bitmap of exactly those dots. No scaling anywhere downstream.

---

## 2. Configuration Schema (`label-config/`)

Source of truth. Versioned in Git. Loaded by the API at startup (with dev hot-reload via a file watcher).

```
label-config/
  media/        *.yaml   physical roll definitions
  templates/    */       HTML/CSS layouts (one folder per template)
  presets/      *.yaml   user-facing workflows
  printers/     *.yaml   printer profiles + adapter binding
```

### Media

```yaml
# media/lbl123.yaml
id: lbl123
name: "Aidetek Small"
widthMm: 50
heightMm: 20
material: PET
dpi: 203          # drives mm→px conversion
sku: "AIDETEK-50x20-PET"   # optional, for reordering
```

### Template

```
templates/smd-small/
  template.html     # uses {{placeholders}}
  template.css
  meta.yaml         # declared fields + types
```

```yaml
# templates/smd-small/meta.yaml
id: smd-small
name: "SMD Small"
fields:
  - { key: partNumber, label: "Part #", type: text, required: true }
  - { key: value,      label: "Value",  type: text }
  - { key: package,    label: "Package", type: text }
  - { key: qty,        label: "Qty",    type: number }
```

The HTML references fields with a templating syntax (start simple — `{{partNumber}}` via Scriban or Handlebars.NET).

### Preset (what the user actually picks)

```yaml
# presets/aidetek-small.yaml
id: aidetek-small
name: "Aidetek Small (SMD)"
media: lbl123
template: smd-small
printer: niimbot-b1      # optional; can be chosen/defaulted at print time
```

### Printer

```yaml
# printers/niimbot-b1.yaml
id: niimbot-b1
name: "Niimbot B1"
adapter: niimbot          # selects IPrinterAdapter implementation
dpi: 203
connection:
  type: bluetooth         # or usb / network
  address: "XX:XX:XX:XX"
```

---

## 3. Backend Architecture (`lablr-api`)

Minimal API, organized by concern. Suggested namespaces:

```
Config/        Load + watch YAML, expose typed Media/Template/Preset/Printer
Rendering/     IBitmapRenderer (PuppeteerSharp impl), mm→px, 1-bit threshold
Templating/    Merge draft data into template HTML (Scriban)
Drafts/        In-memory draft store (Dictionary), optional JSON file persist
Printing/      IPrinterAdapter + per-vendor adapters (Niimbot first)
Endpoints/     Minimal API route definitions
```

### Core abstractions

```csharp
interface IBitmapRenderer {
    Task<MonoBitmap> RenderAsync(string html, string css, int widthPx, int heightPx);
}

interface IPrinterAdapter {
    string Id { get; }                       // "niimbot"
    Task PrintAsync(MonoBitmap bitmap, PrinterProfile profile);
}

record MonoBitmap(int Width, int Height, byte[] Bits);  // 1-bit packed
```

`MonoBitmap` is the contract between rendering and printing. Adapters consume it, ignorant of HTML/CSS.

### Endpoints (MVP)

| Method | Route | Purpose |
|--------|-------|---------|
| GET | `/api/presets` | List presets (UI populates picker) |
| GET | `/api/presets/{id}` | Preset + its template's field schema |
| POST | `/api/drafts` | Create a draft (preset + field values) → id |
| GET | `/api/drafts/{id}/preview.png` | **The exact bitmap**, as PNG |
| POST | `/api/drafts/{id}/print` | Render same bitmap → printer adapter |
| GET | `/api/printers` | Available printers |

`preview.png` and `print` call the **same** internal `RenderDraftAsync(id)`. This is the guardrail for preview = print.

---

## 4. Frontend Architecture (`lablr-ui`)

React 19 + shadcn. The UI is deliberately thin — it does **not** render labels itself.

```
src/
  api/          typed fetch client for the endpoints above
  features/
    print/      PresetPicker, FieldForm (from template schema), PreviewPane
  components/ui/ (shadcn)
```

**Core screen (one screen, fast path):**

1. `PresetPicker` — pick "Aidetek Small (SMD)".
2. `FieldForm` — auto-generated from the preset's template field schema.
3. `PreviewPane` — shows `<img src="/api/drafts/{id}/preview.png">`. Debounced: on every field change, re-POST/PATCH draft → refresh image.
4. **Print** button → `POST /print`.

Because the preview is literally an `<img>` of the server bitmap, preview = print is guaranteed at the UI layer for free.

---

## 5. Phased Rollout

Each phase is independently demoable. Critical path runs top-to-bottom.

### Phase 1 — Rendering Pipeline (the riskiest part, do first)
**Goal:** prove HTML/CSS → exact 1-bit bitmap works headless in .NET.
- Add PuppeteerSharp; `IBitmapRenderer` + Chromium impl.
- mm→px conversion + 1-bit threshold → `MonoBitmap` → PNG.
- Throwaway endpoint: `POST /api/_render` (raw html+css+dimensions → PNG). Eyeball it.
- **Exit:** a hardcoded HTML snippet renders to a crisp 400×160 1-bit PNG.

### Phase 2 — Configuration Layer
**Goal:** YAML config becomes the source of truth.
- Create `label-config/` with one real example per type (media, template, preset, printer).
- Loader + typed models; validate references (preset → media/template/printer exist).
- Dev file watcher for hot-reload.
- **Exit:** `GET /api/presets` returns config-driven data.

### Phase 3 — Drafts + Templating + Preview
**Goal:** a draft renders through the real config.
- Scriban templating: merge draft field values into template HTML.
- In-memory draft store; `POST /api/drafts`, `GET /preview.png`.
- Wire `RenderDraftAsync` = the single shared path.
- **Exit:** create a draft via curl, GET its preview.png, see real data on the label.

### Phase 4 — UI Fast Path
**Goal:** the effortless workflow in a browser.
- API client, PresetPicker, schema-driven FieldForm, live PreviewPane.
- CORS for dev (Vite :5173 → API). Vite proxy for `/api`.
- **Exit:** pick preset → type fields → watch preview update live.

### Phase 5 — Printer Output (Niimbot first)
**Goal:** a real label comes out.
- `IPrinterAdapter` + Niimbot adapter (the only physical target initially).
- `POST /print` → adapter consumes the `MonoBitmap`.
- **Exit:** click Print → physical label in hand. **Core success criterion met.**

### Phase 6 — Deployment
**Goal:** ship to vanbassum.com.
- Bake Chromium into the API Docker image (PuppeteerSharp deps).
- Mount/clone `label-config` into the container (volume or build step).
- Build UI static bundle; serve via its own Traefik route.
- Set `LABLR_API_DOMAIN`, `LABLR_UI_DOMAIN` env; verify Traefik routing + TLS.
- **Exit:** print a label from the deployed site.

### Phase 7+ — After MVP (only if they serve the core workflow)
- More printer adapters (Brother, Zebra, Dymo).
- More templates (chemical-small, storage-bin).
- Optional JSON draft persistence / recent history.
- ChatGPT/MCP "create draft from natural language" → review in PWA.
- OIDC auth (already stubbed in docker-compose) — only if the deployment needs it.

---

## 6. Critical Path & Dependencies

```
Phase 1 (Rendering) ─┬─▶ Phase 3 (Drafts+Preview) ─▶ Phase 4 (UI) ─┐
Phase 2 (Config) ────┘                                              ├─▶ Phase 6 (Deploy)
                       Phase 5 (Printer) ◀── needs MonoBitmap ──────┘
```

- **Phase 1 blocks everything** — if headless rendering can't produce a faithful bitmap, the whole "preview = print" approach needs rethinking. Hence it's first and de-risked with a throwaway endpoint.
- Phases 1 and 2 are independent and can be built in parallel.
- Phase 5 only needs the `MonoBitmap` contract (from Phase 1), so it can start once Phase 1 lands.

---

## 7. Key Decisions to Confirm

1. **Renderer:** PuppeteerSharp (headless Chromium) — accept the Docker image size cost for HTML/CSS fidelity? *(Recommended.)*
2. **Templating engine:** Scriban (clean `{{ }}`, .NET-native, sandboxed) vs Handlebars.NET. *(Recommended: Scriban.)*
3. **First printer target:** Niimbot confirmed as the physical device to validate against?
4. **Niimbot transport:** Bluetooth vs USB vs network — which connection does the target printer use? (Affects adapter complexity significantly.)
5. **Config location:** separate `label-config` Git repo, or a folder in this repo? CLAUDE.md implies separate-repo-cloneable; simplest start is a folder here.

---

## 8. Immediate Next Step

Start **Phase 1**: add PuppeteerSharp to `lablr-api`, implement `IBitmapRenderer`, and stand up the throwaway `/api/_render` endpoint to prove a crisp 1-bit bitmap. Everything else depends on this working.
