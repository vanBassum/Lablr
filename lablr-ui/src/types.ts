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
  type: "text" | "pictogram"
  field: string
  rect: { x: number; y: number; width: number; height: number } // mm
  align?: "left" | "center" | "right"
  valign?: "top" | "center" | "bottom"
  wrap?: boolean
  /** Cap wrapped text to this many lines (last line is ellipsised if it overflows). */
  maxLines?: number
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
  /** Fields a template can use but does not require for matching. */
  optionalFields?: string[]
  orientation?: Orientation
  elements?: TemplateElement[]
  variants?: Partial<Record<Orientation, TemplateVariant>>
}

/** A named symbol resolvable to an image asset (see pictograms.yaml). */
export interface Pictogram {
  image: string // filename under label-config/pictograms/
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
