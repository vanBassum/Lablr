# Lablr — Label Printing System

A full-stack label creation and printing application designed to minimize friction between deciding a label is needed and having a printed label in hand.

**Stack:** .NET 9 minimal API (lablr-api) + Vue/React UI (lablr-ui), deployed to vanbassum.com behind Traefik.

---

## Core Philosophy

Optimize for:
- **Fast workflow** — from decision to printed label in seconds
- **Minimal maintenance** — Git-based configuration, no complex databases
- **Easy deployment** — clone repo, run, go
- **Claude Code integration** — templates and config editable by AI

Avoid:
- Inventory management, stock tracking, BOM management
- Complex databases
- Unnecessary administration

---

## What This Project Is

**This is a label printing tool, not inventory software.**

### Primary Entity: Label Draft

The system operates on label drafts — the data and context needed to render and print a label.

Drafts may be held in memory, persisted to files, or stored elsewhere. **The persistence mechanism is an implementation detail.**

Everything else exists to support creating, previewing, editing, and printing label drafts.

### What the System Does NOT Do

- Store components, chemicals, products, or inventory records
- Track inventory counts or locations
- Require user accounts or permissions
- Mandate prerequisite setup

Labels are independent objects created on-demand. Components, chemicals, and products are optional integrations, never required.

---

## Primary Use Cases

1. **Electronics Workshop** — SMD components, resistors, modules, capacitors
2. **Chemical Storage** — Bottles and containers for KCl, citric acid, etc.
3. **General Storage** — Bins and boxes of screws, cable ties, plumbing parts

Common flow: "I have something. I need a label."

---

## Key Design Decisions

### Configuration as Code
- Templates, printers, and label definitions live in Git
- Deploying to a new server = clone repo + run
- Changes to templates behave like code changes (review, commit, rollback)

### Three-Part Model: Media, Template, Preset

Labels are composed from three decoupled concepts:

**Media** — The physical roll loaded in the printer.
```yaml
media:
  lbl123:
    widthMm: 50
    heightMm: 20
    material: PET
```
Answers: How big? What material? Which SKU to buy?

**Template** — The layout and field structure.
```yaml
templates:
  smd-small:
  chemical-small:
  storage-bin:
```
Answers: How should the label look? Which fields exist? Where are they positioned?

**Preset** — A user-facing workflow combining media + template + printer choice.
```yaml
presets:
  aidetek-small:
    media: lbl123
    template: smd-small
```
Answers: Which label should the user select in the UI?

**Why three parts:** Users think in presets ("Print on Aidetek Small"), not in media or templates. Decoupling allows one media type to serve multiple templates, and one template to work with different media.

### Preview = Print (Hard Requirement)

**The preview shown to the user must always be generated from the exact bitmap that will be sent to the printer.**

No separate preview rendering pipeline may exist. This is non-negotiable.

Pipeline:
```
HTML/CSS → Render at printer DPI → 1-bit monochrome bitmap → Preview & Send to Printer
```

The bitmap displayed on screen must be the identical bitmap sent to the device.

### Printer Abstraction
- Rendering (HTML/CSS → bitmap) decoupled from printing
- Adapters for Niimbot, Brother, Zebra, Dymo, etc.
- Renderer agnostic to printer choice

### Minimal Persistence
- Drafts and recent history in memory acceptable
- Optional: simple file-based persistence (JSON)
- No database required unless clear need emerges

---

## Development Workflow

**Intended workflow:**

1. Claude Code edits HTML/CSS/YAML templates
2. Dev server hot-reloads
3. Browser shows live preview
4. User reviews and tweaks
5. Config committed to Git

**Template technology:** Current preferred direction is HTML/CSS — familiar tooling, easy to edit, Claude-friendly, supports live preview. The system should be designed so alternative renderers could be introduced later without major refactoring.

---

## Template Development

Templates should be editable by both humans and AI.

**Preferred workflow:**

1. Edit HTML/CSS template in editor
2. Dev server automatically reloads
3. Print bitmap regenerates instantly
4. Exact print preview updates in real-time
5. Iterate until satisfied

This rapid feedback loop enables effective use of Claude Code to design and modify templates.

---

## Printer Selection Philosophy

**Users generally select a preset, not a printer.**

The system should determine the appropriate media, template, and printer automatically whenever possible.

Bad UX:
```
1. Select printer
2. Select media
3. Select template
4. Specify width
5. Specify height
```

Good UX:
```
Print BC547 on Aidetek Small
```

Presets encapsulate all these choices, reducing user friction.

---

## Configuration as Product

**The label configuration repository is part of the product.**

Templates, presets, media definitions, and printer profiles are treated as source code and versioned in Git.

Benefits:
- Git history and rollback
- Code review workflow for template changes
- Easy collaboration
- No manual database migrations
- New installation → clone repo → ready to use

A fresh deployment should become fully functional without requiring manual setup, admin panels, or configuration forms.

---

## ChatGPT Integration

Future workflow:
```
ChatGPT (natural language)
  → MCP Tool (create draft)
  → PWA (review & print)
  → Printed label
```

Users may say "Create a label for BC547" → system generates draft ready to print.

---

## Success Criteria

The project succeeds when this workflow feels effortless:

1. User realizes a label is needed
2. User creates label (ChatGPT or PWA)
3. User optionally tweaks draft
4. User prints
5. User has label

**Everything else is secondary.** Question any feature or complexity that doesn't directly serve this workflow.

---

## Project Structure

```
/lablr-api          → .NET 9 minimal API
/lablr-ui           → Frontend (Vue/React)
/label-config       → Git-based configuration (source of truth)
  /media            → Physical media definitions (YAML)
  /templates        → HTML/CSS templates (reusable layouts)
  /presets          → User-facing workflow presets (YAML)
  /printers         → Printer adapters & profiles (YAML)
```

Configuration is the source of truth. Deploy by cloning the configuration repository.

---

## Implementation Notes

- **Configuration is permanent.** Media, templates, and presets are stored in Git.
- **Labels are ephemeral.** Drafts exist in memory or temporary storage. No history archive needed.
- Templates should be reusable and generic (not tied to specific products)
- Rendering pipeline: HTML/CSS → bitmap → preview/print (same pipeline)
- Printer adapters consume 1-bit bitmaps, ignorant of rendering source
- Avoid user management unless absolutely necessary
