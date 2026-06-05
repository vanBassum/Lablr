import type { Template, LabelStock, Printer, Draft, Orientation, TemplateElement } from "@/types"

interface PictogramRegistry {
  [name: string]: { image: string }
}

// Config is loaded once at runtime from the API (GET /api/config). The backend
// is the single source of truth; see lablr-api. Paths are root-relative — same
// origin in prod, proxied to the backend in dev.

let resolveReady!: () => void
export const configReady = new Promise<void>((r) => (resolveReady = r))

export class ConfigService {
  private templates = new Map<string, Template>()
  private labels = new Map<string, LabelStock>()
  private printers = new Map<string, Printer>()
  private pictograms: PictogramRegistry = {}

  async load(): Promise<void> {
    const res = await fetch("/api/config")
    if (!res.ok) throw new Error(`config load failed: ${res.status}`)
    const data = (await res.json()) as {
      labels: LabelStock[]
      templates: Template[]
      printers: Printer[]
      pictograms: PictogramRegistry
    }
    this.labels = new Map(data.labels.map((l) => [l.id, l]))
    this.templates = new Map(data.templates.map((t) => [t.id, t]))
    this.printers = new Map(data.printers.map((p) => [p.id, p]))
    this.pictograms = data.pictograms ?? {}
    resolveReady()
  }

  getPictogramRegistry(): PictogramRegistry {
    return this.pictograms
  }

  getTemplates(): Template[] {
    return Array.from(this.templates.values())
  }

  getTemplate(id: string): Template | undefined {
    return this.templates.get(id)
  }

  getLabelStock(id: string): LabelStock | undefined {
    return this.labels.get(id)
  }

  getPrinter(id: string): Printer | undefined {
    return this.printers.get(id)
  }

  /** The printer used to size a stock's bitmap — first compatible one, or a default. */
  getPrinterForStock(stock: LabelStock): Printer {
    for (const id of stock.compatiblePrinters ?? []) {
      const p = this.printers.get(id)
      if (p) return p
    }
    return { id: "default", name: "Default", dpi: 300 }
  }

  /**
   * A template matches a draft when every one of its requiredFields is present
   * (and non-empty) in the draft's fields.
   */
  getMatchingTemplates(draft: Draft): Template[] {
    const keys = new Set(
      Object.entries(draft.fields)
        .filter(([, v]) => v != null && String(v).trim() !== "")
        .map(([k]) => k),
    )
    return this.getTemplates().filter((t) => t.requiredFields.every((f) => keys.has(f)))
  }

  /** Which orientations a template supports. */
  getTemplateOrientations(template: Template): Orientation[] {
    if (template.variants) {
      return (Object.keys(template.variants) as Orientation[]).filter(
        (o) => template.variants?.[o],
      )
    }
    return [template.orientation ?? "portrait"]
  }

  /** The elements to render for a given orientation, handling both layouts. */
  getTemplateElements(template: Template, orientation: Orientation): TemplateElement[] {
    if (template.variants) {
      return (
        template.variants[orientation]?.elements ??
        template.variants.portrait?.elements ??
        template.variants.landscape?.elements ??
        []
      )
    }
    return template.elements ?? []
  }
}

export const configService = new ConfigService()
