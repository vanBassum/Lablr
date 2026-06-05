import { load } from "js-yaml"
import type {
  Draft,
  LabelStock,
  Orientation,
  Printer,
  Template,
  TemplateElement,
} from "@/types"

// ---------- Build-time load of the YAML config in public/label-config ----------
//
// Config is authored as YAML in Git and bundled at build time via import.meta.glob.
// This is the single source of truth — there is no hardcoded fallback.

function parseAll<T>(modules: Record<string, string>): T[] {
  return Object.values(modules).map((raw) => load(raw) as T)
}

const labelModules = import.meta.glob("../../public/label-config/labels/*.yaml", {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>

const templateModules = import.meta.glob("../../public/label-config/templates/*.yaml", {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>

const printerModules = import.meta.glob("../../public/label-config/printers/*.yaml", {
  query: "?raw",
  import: "default",
  eager: true,
}) as Record<string, string>

const FALLBACK_PRINTER: Printer = { id: "default", name: "Default", dpi: 300 }

export class ConfigService {
  private labels = new Map<string, LabelStock>()
  private templates = new Map<string, Template>()
  private printers = new Map<string, Printer>()

  constructor() {
    for (const l of parseAll<LabelStock>(labelModules)) this.labels.set(l.id, l)
    for (const t of parseAll<Template>(templateModules)) this.templates.set(t.id, t)
    for (const p of parseAll<Printer>(printerModules)) this.printers.set(p.id, p)
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
    for (const id of stock.compatiblePrinters) {
      const p = this.printers.get(id)
      if (p) return p
    }
    return FALLBACK_PRINTER
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
