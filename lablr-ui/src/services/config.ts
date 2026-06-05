import type { Template, Label, Printer, Draft } from "@/types"

// Hardcoded test data
const TEMPLATES: Template[] = [
  {
    id: "transistor-aidetek-small-landscape",
    label: "aidetek-small",
    printerId: "dymo-450",
    orientation: "landscape",
    requiredFields: ["name", "type"],
    elements: [
      {
        type: "text",
        field: "name",
        rect: { x: 1, y: 5, width: 18, height: 15 },
        align: "center",
        valign: "center",
        font: { maxSizeMm: 5, minSizeMm: 2, weight: "bold" },
        wrap: false,
        fit: "shrink",
      },
      {
        type: "text",
        field: "type",
        rect: { x: 1, y: 25, width: 18, height: 12 },
        align: "center",
        valign: "center",
        font: { maxSizeMm: 4, minSizeMm: 1.5, weight: "normal" },
        wrap: false,
        fit: "shrink",
      },
    ],
  },
]

const LABELS: Label[] = [
  {
    id: "aidetek-small",
    widthMm: 50,
    heightMm: 20,
    material: "PET",
    manufacturer: "Aidetek",
    marginsMm: { top: 1.5, left: 1.5, right: 1.5, bottom: 1.5 },
    offsetCorrectionMm: { x: 0, y: 0 },
    compatiblePrinters: ["dymo-450"],
  },
]

const PRINTERS: Printer[] = [
  {
    id: "dymo-450",
    name: "Dymo LabelWriter 450",
    dpi: 203,
  },
]

export class ConfigService {
  getTemplates(): Template[] {
    return TEMPLATES
  }

  getTemplate(id: string): Template | undefined {
    return TEMPLATES.find((t) => t.id === id)
  }

  getLabel(id: string): Label | undefined {
    return LABELS.find((l) => l.id === id)
  }

  getPrinter(id: string): Printer | undefined {
    return PRINTERS.find((p) => p.id === id)
  }

  /**
   * Find all templates that can print this draft.
   * A template can print a draft if the draft has all required fields.
   */
  getCompatibleTemplates(draft: Draft): Template[] {
    return TEMPLATES.filter((template) => {
      return template.requiredFields.every((field) => field in draft.fields)
    })
  }
}

export const configService = new ConfigService()
