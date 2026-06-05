export type Orientation = "portrait" | "landscape"

/** A physical label product (a roll/sheet of a specific size & material). */
export interface LabelStock {
  id: string
  name: string
  widthMm: number
  heightMm: number
  material: string
  marginsMm: { top: number; right: number; bottom: number; left: number }
  /** Print-head calibration. Applied only at print time, never to the preview. */
  offsetCorrectionMm: { x: number; y: number }
  compatiblePrinters: string[]
  manufacturer?: string
  sku?: string
}

export interface TemplateElement {
  type: "text"
  field: string
  rect: { x: number; y: number; width: number; height: number } // mm
  align?: "left" | "center" | "right"
  valign?: "top" | "center" | "bottom"
  wrap?: boolean
  fit?: "shrink" | "none"
  font?: {
    maxSizeMm?: number
    minSizeMm?: number
    weight?: "normal" | "bold"
  }
}

export interface TemplateVariant {
  elements: TemplateElement[]
}

/**
 * A handcrafted design for one label stock. A template either has a single set
 * of `elements` (with an `orientation`), or per-orientation `variants`.
 */
export interface Template {
  id: string
  name: string
  label: string // references a LabelStock id
  requiredFields: string[]
  orientation?: Orientation
  elements?: TemplateElement[]
  variants?: Partial<Record<Orientation, TemplateVariant>>
}

export interface Draft {
  id: string
  fields: Record<string, string>
}

/** Output device: identity + native resolution (used to size the bitmap). */
export interface Printer {
  id: string
  name: string
  dpi: number
}
