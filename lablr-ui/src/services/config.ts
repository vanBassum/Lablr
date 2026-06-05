import type { Template, Media, Printer, Draft } from "@/types"

// Hardcoded test data
const TEMPLATES: Template[] = [
  {
    id: "test-smd",
    mediaId: "aidetek-50x20",
    printerId: "dymo-450",
    orientation: "landscape",
    fields: {
      name: { required: true },
      type: { required: false },
    },
  },
]

const MEDIA: Media[] = [
  {
    id: "aidetek-50x20",
    widthMm: 50,
    heightMm: 20,
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

  getMedia(id: string): Media | undefined {
    return MEDIA.find((m) => m.id === id)
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
      const required = Object.entries(template.fields)
        .filter(([, field]) => field.required)
        .map(([key]) => key)

      return required.every((key) => key in draft.fields)
    })
  }
}

export const configService = new ConfigService()
