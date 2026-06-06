# Lablr Design

Lablr is a label creation and printing system designed to minimize friction between needing a label and having a printed label.

**Not an inventory system.** Does not manage components, chemicals, stock, locations, suppliers, or products.

---

## Core Philosophy

Optimize for:

- Simplicity
- Predictability
- Full control over label appearance
- Fast creation of new label designs
- Easy YAML configuration
- Easy AI generation of templates

Do NOT optimize for:

- Automatic responsive layouts
- Generic templates that work on every label
- Dynamic scaling between label sizes
- Building a CSS/flexbox/grid replacement

**The goal is to print labels, not to build a layout engine.**

---

## Core Model

Four concepts:

```text
Draft (data only)
    ↓
Template (design for specific label + orientation)
    ├─ Label (physical product)
    ├─ Printer (output device)
    └─ Orientation (portrait / landscape)
```

**Template Matching:** A draft is compatible with a template if the draft contains all fields the template requires.

---

## Draft

**What it is:** User data for a label.

**Contains:**

- Key-value pairs (field names and values)

**Example:**
```yaml
fields:
  name: BC547
  type: NPN
  package: TO-92
```

**Constraints:**

- Must NOT contain template references
- Must NOT contain label references
- Must NOT contain printer references
- Must NOT contain layout information
- Describes information only

---

## Template

**What it is:** A handcrafted design for a specific label and orientation.

**Template names reflect their purpose:**

```text
transistor-aidetek-small-landscape
chemical-vial-small-portrait
storage-box-dymo-54x70-landscape
```

**Example:**

```yaml
id: transistor-aidetek-small-landscape

label: aidetek-small

orientation: landscape

requiredFields:
  - name
  - type
  - package

elements:

  - type: text
    field: name
    rect:
      x: 2
      y: 2
      width: 46
      height: 8
    align: center
    valign: center
    font:
      maxSizeMm: 4
      minSizeMm: 2
      weight: bold
    wrap: false
    fit: shrink

  - type: text
    field: type
    rect:
      x: 2
      y: 12
      width: 46
      height: 6
    align: center
    valign: center
    font:
      maxSizeMm: 3
      minSizeMm: 1.5
    wrap: false
    fit: shrink
```

**Responsibilities:**

- Define required fields
- Define label reference
- Define orientation
- Define element positions and styling
- Define text fitting rules

**Key principles:**

- One template = one label + orientation combination
- Templates are NOT responsive
- Templates are NOT intended to work on arbitrary label sizes
- Templates are handcrafted designs
- A template references exactly one label
- Coordinates are absolute millimeters, not relative percentages

**Matching rule:** A draft can be printed with this template if the draft contains all required fields.

---

## Label

**What it is:** A physical label product.

**Represents:**

- A roll, sheet, or specific label product
- A set of physical properties
- Compatibility constraints

**Example:**

```yaml
id: aidetek-small

widthMm: 50
heightMm: 20

material: PET
color: white

manufacturer: Aidetek
sku: LBL-AIDA-50X20

marginsMm:
  top: 1.5
  left: 1.5
  right: 1.5
  bottom: 1.5

offsetCorrectionMm:
  x: 0
  y: 0

compatiblePrinters:
  - niimbot-b21
  - niimbot-m2
```

**Responsibilities:**

- Define physical dimensions
- Define material properties
- Define order information
- Define usable margins (safe area for printing)
- Define printer offset correction (for calibration)
- List compatible printers

**Key principle:** Physical label behavior is centralized. If margins or offsets change, all templates using that label automatically benefit. Do NOT duplicate these values in templates.

**Constraints:**

- Must NOT contain DPI (that's the printer's job)

---

## Printer

**What it is:** The output device and its resolution.

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
- Define printer protocol
- Define transport type

**Constraint:** The printer adapter receives a bitmap. It should not know about drafts, templates, or labels.

---

## Template Elements

Elements are the building blocks of a template.

**Fundamental layout primitive:** Each element owns a rectangle (absolute millimeter coordinates).

### Text Element

```yaml
type: text

field: name

rect:
  x: 2        # mm from left edge of label
  y: 2        # mm from top edge of label
  width: 46   # mm
  height: 8   # mm

align: left|center|right

valign: top|center|bottom

font:
  maxSizeMm: 4      # max font size to try
  minSizeMm: 2      # min font size to use
  weight: normal|bold

wrap: true|false

fit: shrink|none
```

**Text Fitting Algorithm:**

The renderer must guarantee text stays inside its rectangle.

```text
Start at maxSizeMm.
Measure text.
If text does not fit:
    Reduce size by step.
Repeat until it fits or reaches minSizeMm.
Render.
```

**Parameters:**

- `align` — horizontal alignment (left, center, right)
- `valign` — vertical alignment (top, center, bottom)
- `wrap` — allow text to wrap to multiple lines
- `fit` — behavior when text is too large
  - `shrink` — reduce font size until it fits
  - `none` — render at specified size (may overflow)
- `font.weight` — bold or normal rendering

### Future Element Types

Design for future support of:

```text
rectangle
line
image
qr
barcode
```

Only text needs implementation now.

---

## Render Pipeline

**Input:**

- Draft
- Template
- Label (resolved from template)
- Printer (user selected or default compatible printer)

**Output:**

- Monochrome bitmap

**Steps:**

1. Load template, label, and printer
2. Get label dimensions from label (widthMm, heightMm)
3. Apply template orientation (if landscape, swap dimensions)
4. Apply label margins (reduce usable area)
5. Get DPI from printer
6. Convert all millimeter values to pixels: `pixels = mm * (dpi / 25.4)`
7. Apply printer offset correction to all coordinates
8. For each element in the template:
   - Get the element's rectangle and properties
   - Get the field value from the draft
   - If text element:
     - Fit text to rectangle using fit algorithm
     - Apply alignment (horizontal and vertical)
     - Render text at final size
9. Rasterize all elements into a monochrome bitmap
10. Return the bitmap (for both preview and printing)

**Key principle:** The renderer is deterministic. The same draft + template + label + printer produces the same bitmap every time.

---

## Template Matching

**Automatic discovery, not selection.**

A template is compatible with a draft when:

```text
All required fields are present in the draft.
```

**Example:**

Draft:

```yaml
fields:
  name: BC547
  type: NPN
  package: TO-92
```

Template:

```yaml
requiredFields:
  - name
  - type
  - package
```

Result: **Compatible** ✅

The UI should only show templates that are compatible with the current draft.

---

## Preview and Print

**Rule:** Preview and print must use the same bitmap.

**Correct:**

```text
Draft → Template → Renderer → Bitmap → Preview
                                ↓
                              Print
```

**Incorrect:**

```text
Draft → Browser preview renderer
Draft → Separate print renderer
```

**Key principle:** There is exactly one renderer. The bitmap shown on screen is the identical bitmap sent to the printer. No separate preview path.

The renderer runs on the **backend** (C#/SkiaSharp). The frontend fetches the rendered image to preview and the rendered bytes to print — it never builds a second bitmap. This is also what lets the AI print headlessly (no browser in the loop). See CLAUDE.md + memory `backend-canonical-render`.

---

## Configuration

**Where:** YAML files, versioned in Git.

**Structure:**

```text
label-config/
  templates/
    transistor-aidetek-small-landscape.yaml
    chemical-vial-small-portrait.yaml
    storage-box-dymo-54x70-landscape.yaml
  labels/
    aidetek-small.yaml
    dymo-54x70.yaml
    chemical-vial-small.yaml
  printers/
    niimbot-b21.yaml
    dymo-450.yaml
```

**ConfigService responsibilities:**

- Seed an empty embedded **SQLite** store from the YAML on first boot; the DB is the source of truth thereafter (edited at runtime via REST/MCP)
- Validate that templates reference existing labels and printers
- Validate that templates declare required fields
- Provide template, label, printer lookups
- Find compatible templates for a draft (field matching)

---

## Application Responsibilities

### UI Layer

- Create drafts with key-value pairs
- Edit draft fields
- Show available templates (auto-filtered by field matching)
- Show the rendered bitmap preview
- Allow printer selection (if multiple compatible)
- Trigger print
- Show recent drafts / history (optional)

### Renderer

- Resolve all dimensions (mm → pixels)
- Apply margins and offset corrections
- Fit text to rectangles
- Render elements to bitmap
- Produce deterministic output

### Printer Adapter

- Accept a bitmap
- Encode it for a specific printer
- Send commands to device
- Handle transport (WebUSB, Bluetooth, etc.)

### Configuration Service

- Load and validate YAML
- Resolve references
- Provide configuration data to the app

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
- Load labels from YAML
- Load printers from YAML
- Validate references
- Return compatible templates for a draft (field matching)
- Return label by ID
- Return printer by ID

### RenderService

Converts drafts to bitmaps.

**Responsibilities:**

- Accept: draft + template + label + printer
- Execute the render pipeline
- Return: monochrome bitmap

**Does not know about:** Printer adapters, network, storage, UI.

### PrinterService

Handles print output.

**Responsibilities:**

- Accept: bitmap + printer ID
- Select the correct printer adapter
- Send bitmap to adapter
- Handle transport

### PrinterAdapter (one per printer family)

Examples:

- NiimbotPrinterAdapter
- DymoPrinterAdapter
- BrotherPrinterAdapter

**Responsibility:**

```text
Bitmap → Printer-specific byte sequence
```

---

## Design Rules

✅ **Drafts contain data only.** Key-value pairs, no layout, no references.

✅ **Templates are concrete.** Each template is designed for one specific label + orientation.

✅ **Field-based matching.** Drafts automatically discover compatible templates.

✅ **Templates define layout only.** No DPI, no margins (those are on the label).

✅ **Labels define physical properties.** Margins, offset corrections, compatible printers.

✅ **Coordinates are absolute millimeters.** Not relative percentages.

✅ **Text fitting is guaranteed.** Text stays inside its rectangle or is cut off by `fit: none`.

✅ **One render pipeline.** Deterministic, single entry point.

✅ **Preview and print use the same bitmap.** Never separate renders.

✅ **Configuration lives in Git.** YAML files, versioned, human-editable.

✅ **Many simple templates > few complex ones.** Simplicity over generality.

✅ **Avoid inventory management.** It is not this system's job.

✅ **Avoid databases unless truly needed.** Drafts stay in memory; the only DB is an embedded SQLite **config** store (labels/templates/printers/pictograms), seeded from YAML — never inventory.

---

## Design Principle

**A template is a handcrafted design for a specific label.**

The system should prefer many simple templates over a small number of complicated responsive templates.

This keeps the renderer simple, keeps YAML understandable, and gives complete control over label appearance.

When you need a new label design, write a new template. Don't try to make an old template work with a different label.
