export interface Draft {
  id: string
  fields: Record<string, string>
}

export interface Template {
  id: string
  label: string
  printerId: string
  orientation: "portrait" | "landscape"
  requiredFields: string[]
  elements: TemplateElement[]
}

export interface TemplateElement {
  type: "text"
  field: string
  rect: {
    x: number // mm
    y: number // mm
    width: number // mm
    height: number // mm
  }
  align: "left" | "center" | "right"
  valign: "top" | "center" | "bottom"
  font: {
    maxSizeMm: number
    minSizeMm: number
    weight: "normal" | "bold"
  }
  wrap: boolean
  fit: "shrink" | "none"
}

export interface Label {
  id: string
  widthMm: number
  heightMm: number
  material?: string
  color?: string
  manufacturer?: string
  sku?: string
  marginsMm?: {
    top: number
    left: number
    right: number
    bottom: number
  }
  offsetCorrectionMm?: {
    x: number
    y: number
  }
  compatiblePrinters?: string[]
}

export interface Printer {
  id: string
  name: string
  dpi: number
}
