import type { Template, Label, Printer, Draft } from "@/types"
import { parseYaml } from "@/lib/yaml-parser"

export class ConfigService {
  private templates: Map<string, Template> = new Map()
  private labels: Map<string, Label> = new Map()
  private printers: Map<string, Printer> = new Map()
  private loaded = false

  constructor() {
    // Initialize with hardcoded fallback data
    this.initializeDefaults()
  }

  private initializeDefaults(): void {
    const defaultTemplate: Template = {
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
    }

    const defaultLabel: Label = {
      id: "aidetek-small",
      widthMm: 50,
      heightMm: 20,
      material: "PET",
      manufacturer: "Aidetek",
      marginsMm: { top: 1.5, left: 1.5, right: 1.5, bottom: 1.5 },
      offsetCorrectionMm: { x: 0, y: 0 },
      compatiblePrinters: ["dymo-450"],
    }

    const defaultPrinter: Printer = {
      id: "dymo-450",
      name: "Dymo LabelWriter 450",
      dpi: 203,
    }

    this.templates.set(defaultTemplate.id, defaultTemplate)
    this.labels.set(defaultLabel.id, defaultLabel)
    this.printers.set(defaultPrinter.id, defaultPrinter)
  }

  async load(): Promise<void> {
    if (this.loaded) return

    // Currently using hardcoded data
    // YAML loading can be added in the future by implementing:
    // await this.loadTemplates()
    // await this.loadLabels()
    // await this.loadPrinters()

    this.loaded = true
  }

  private async loadTemplates(): Promise<void> {
    try {
      const response = await fetch("/label-config/templates/transistor-aidetek-small-landscape.yaml")
      const content = await response.text()
      const data = parseYaml(content)
      const template = this.normalizeTemplate(data)
      this.templates.set(template.id, template)
    } catch (error) {
      console.error("Failed to load templates:", error)
    }
  }

  private async loadLabels(): Promise<void> {
    try {
      const response = await fetch("/label-config/labels/aidetek-small.yaml")
      const content = await response.text()
      const data = parseYaml(content)
      const label = this.normalizeLabel(data)
      this.labels.set(label.id, label)
    } catch (error) {
      console.error("Failed to load labels:", error)
    }
  }

  private async loadPrinters(): Promise<void> {
    try {
      const response = await fetch("/label-config/printers/dymo-450.yaml")
      const content = await response.text()
      const data = parseYaml(content)
      const printer = this.normalizePrinter(data)
      this.printers.set(printer.id, printer)
    } catch (error) {
      console.error("Failed to load printers:", error)
    }
  }

  private normalizeTemplate(data: any): Template {
    return {
      id: data.id,
      label: data.label,
      printerId: data.printerId,
      orientation: data.orientation,
      requiredFields: data.requiredFields,
      elements: (data.elements || []).map((el: any) => ({
        type: el.type,
        field: el.field,
        rect: {
          x: el.rect.x,
          y: el.rect.y,
          width: el.rect.width,
          height: el.rect.height,
        },
        align: el.align,
        valign: el.valign,
        font: {
          maxSizeMm: el.font.maxSizeMm,
          minSizeMm: el.font.minSizeMm,
          weight: el.font.weight,
        },
        wrap: el.wrap,
        fit: el.fit,
      })),
    }
  }

  private normalizeLabel(data: any): Label {
    return {
      id: data.id,
      widthMm: data.widthMm,
      heightMm: data.heightMm,
      material: data.material,
      color: data.color,
      manufacturer: data.manufacturer,
      sku: data.sku,
      marginsMm: data.marginsMm,
      offsetCorrectionMm: data.offsetCorrectionMm,
      compatiblePrinters: data.compatiblePrinters,
    }
  }

  private normalizePrinter(data: any): Printer {
    return {
      id: data.id,
      name: data.name,
      dpi: data.dpi,
    }
  }

  getTemplates(): Template[] {
    return Array.from(this.templates.values())
  }

  getTemplate(id: string): Template | undefined {
    return this.templates.get(id)
  }

  getLabel(id: string): Label | undefined {
    return this.labels.get(id)
  }

  getPrinter(id: string): Printer | undefined {
    return this.printers.get(id)
  }

  /**
   * Find all templates that can print this draft.
   * A template can print a draft if the draft has all required fields.
   */
  getCompatibleTemplates(draft: Draft): Template[] {
    return this.getTemplates().filter((template) => {
      return template.requiredFields.every((field) => field in draft.fields)
    })
  }
}

export const configService = new ConfigService()

// Auto-load on app start
configService.load().catch((error) => {
  console.error("Config loading failed:", error)
})
