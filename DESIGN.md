# Lablr v2 Design

Lablr is a label creation and printing system designed to minimize friction between needing a label and having a printed label.

**Not an inventory system.** Does not manage components, chemicals, stock, locations, suppliers, or products. The central unit is a **label draft**.

---

## Core Model

Four concepts, one matching rule:

```
Draft (key-value pairs)
  ↓
matches by fields
  ↓
Template (declares required fields, layout for specific media + orientation)
  ├─ Media (physical dimensions)
  ├─ Printer (DPI, transport)
  └─ Orientation (portrait / landscape)
```

---

## Draft

**What it is:** The user's current label.

**Contains:**
- User data (text fields to print)
- A recommended or selected preset

**Example:**
```yaml
preset: smd-basic
fields:
  name: BC547
  type: NPN
  package: TO-92
```

**Responsibilities:**
- Store label data
- Reference the preset to use
- Be easy to create from UI, ChatGPT, MCP, or manual input
- Be editable before printing

**Constraints:**
- Must NOT contain layout details
- Must NOT contain physical label dimensions
- Must NOT contain printer details
- Must NOT contain inventory data

---

---

## Template

**What it is:** Concrete layout designed for a specific media and orientation.

**Declares:**
- Required and optional fields (field names the draft must provide to match this template)
- Elements (text, barcode, etc.)
- Rectangles with normalized coordinates (0.0–1.0 of the media)
- Font sizes, alignment, wrapping rules

**Example:**
```yaml
id: smd-basic-50x20-landscape
media: aidetek-small-50x20
orientation: landscape

fields:
  name:
    required: true
  type:
    required: true
  package:
    required: false

elements:
  - type: text
    field: name
    rect:
      x: 0.05
      y: 0.05
      width: 0.90
      height: 0.40
    align: center
    valign: center
    font:
      size: 12
      weight: bold
    wrap: false

  - type: text
    field: type
    rect:
      x: 0.05
      y: 0.55
      width: 0.42
      height: 0.35
    align: center
    valign: center
    font:
      size: 8
    wrap: false
```

**Responsibilities:**
- Declare required and optional fields
- Define layout using normalized coordinates (0.0–1.0)
- Define elements and their positioning

**Matching rule:** A draft can be printed with this template if the draft contains all required fields.

**Key principle:** One template = one media + orientation combination. No responsive scaling. Reusable by any draft that has the required fields.

---

## Media

**What it is:** The physical label.

**Example:**
```yaml
id: aidetek-small-label
widthMm: 50
heightMm: 20
material: PET
color: white
sku: LBL123
manufacturer: Niimbot
```

**Responsibilities:**
- Define physical width and height (in millimeters)
- Define material
- Define manufacturer, SKU, order information
- Represent the actual roll or sheet of labels

**Constraints:**
- Must NOT contain DPI (that's the printer's job)

---

## Printer

**What it is:** The output device and resolution.

**Example:**
```yaml
id: niimbot-b21
name: Niimbot B21
dpi: 203
protocol: niimbot
transport: bluetooth
```

**Responsibilities:**
- Define print resolution (DPI)
- Define printer protocol (niimbot, dymo, etc.)
- Define transport type (bluetooth, webusb, etc.)
- Provide enough information for a printer adapter to convert a bitmap into device-specific commands

**Constraint:** The printer adapter receives a bitmap. It should not know about drafts, templates, or presets.

---

## Render Pipeline

**Input:**
- Draft (key-value pairs)
- Template (selected by field matching)
- Media (from template)
- Printer (from template)

**Output:**
- Monochrome bitmap

**Steps:**

1. Load the template, media, and printer
2. Get physical dimensions from media (widthMm, heightMm)
3. Apply orientation (if landscape, swap dimensions)
4. Get DPI from printer
5. Convert media size from millimeters to pixels: `pixels = mm * (dpi / 25.4)`
6. For each element in the template:
   - Scale element rect from 0.0–1.0 to pixel coordinates
   - Get the field value from the draft
   - Render text at specified font size, aligned within the rectangle
7. Rasterize all elements into a monochrome bitmap
8. Return the bitmap (for both preview and printing)

**Key principle:** The renderer is deterministic. The same draft + template produces the same bitmap every time.

---

## Preview and Print

**Rule:** Preview and print must use the same bitmap.

**Correct:**
```
Draft → Renderer → Bitmap → Preview
                      ↓
                    Print
```

**Incorrect:**
```
Draft → Browser preview renderer
Draft → Separate print renderer
```

**Key principle:** There is exactly one renderer. The bitmap shown on screen is the identical bitmap sent to the printer. No separate preview path.

---

## Configuration

**Where:** YAML files, versioned in Git.

**Structure:**
```
label-config/
  templates/
    smd-basic.yaml
    chemical-small.yaml
    storage-box.yaml
  media/
    aidetek-small-label.yaml
    dymo-54x70.yaml
  printers/
    niimbot-b21.yaml
    dymo-450.yaml
  presets.yaml
```

**ConfigService responsibilities:**
- Load YAML files
- Validate references (presets → existing templates, media, printers)
- Validate that drafts contain required fields for their template
- Support reload during development
- Provide templates, media, presets, printers to the app

---

## Application Responsibilities

### UI Layer
- Create drafts with key-value pairs
- Edit draft fields
- Show available templates (filtered by field matching)
- Show the rendered bitmap preview
- Trigger print
- Show recent drafts / history (if implemented)

### Renderer
- Resolve all dimensions
- Map coordinates from 0.0–1.0 to pixels
- Render elements
- Produce the final bitmap

### Printer Adapter
- Take the bitmap
- Encode it for a specific printer
- Send it over the chosen transport

### Configuration Loader
- Load YAML
- Validate configuration
- Provide templates, media, printers to the app

---

## Suggested Internal Services

### DraftService
Manages draft objects.

**Responsibilities:**
- Create draft with key-value pairs
- Update draft fields
- List drafts
- Delete / archive draft

Persistence can be in memory at first.

### ConfigService
Loads and validates configuration.

**Responsibilities:**
- Load templates from YAML
- Load media from YAML
- Load printers from YAML
- Validate that templates reference existing media and printers
- Provide matching: given a draft, return all templates whose required fields are satisfied

### RenderService
Converts drafts into bitmaps.

**Responsibilities:**
- Accept: draft + template + media + printer
- Execute the render pipeline
- Return: monochrome bitmap

**Does not know about:** Printer adapters, network, storage, UI.

### PrinterService
Handles print output.

**Responsibilities:**
- Select the correct printer adapter for a template's printer ID
- Pass the bitmap to the adapter
- Handle transport (send commands to device)

### PrinterAdapter (one per printer family)

Examples:
- NiimbotPrinterAdapter
- DymoPrinterAdapter
- BrotherPrinterAdapter

**Responsibility:**
```
Bitmap → Printer-specific byte sequence
```

---

## Design Rules

✅ **Drafts contain data only.** Key-value pairs, no layout.

✅ **Templates are concrete.** Each template is designed for one specific (media, orientation) pair.

✅ **Field-based matching.** A draft can print with a template if it has all required fields.

✅ **Templates define layout only.** No physical size, no DPI.

✅ **Media defines physical size only.** No DPI.

✅ **Printers define DPI and transport.** No knowledge of templates or media.

✅ **Templates use relative coordinates (0.0–1.0).** Scaled to the media dimensions at render time.

✅ **One render pipeline.** The renderer produces one bitmap.

✅ **Preview and print use the same bitmap.** Never separate renders.

✅ **Configuration lives in Git.** YAML files, versioned.

✅ **Avoid inventory management.** It is not this system's job.

✅ **Avoid databases unless truly needed.** In-memory storage at first.

---

## Implementation Order (Suggested)

1. Type definitions (Draft, Preset, Template, Media, Printer)
2. ConfigService (load YAML, validate)
3. RenderService (11-step pipeline)
4. DraftService (create, update, list)
5. PrinterService + adapters
6. UI integration

---

## Key Insight

The design separates concerns so clearly that:
- The renderer never has to ask "what's the printer?" or "what's the media?"
- The template never has to know physical millimeters or pixels
- The draft never has to know layout or printer details
- The UI never has to perform math (rendering, text fitting, coordinate mapping)

Each service knows exactly what it needs and nothing more.
