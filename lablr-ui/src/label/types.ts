// The label model. Two layers:
//   Template — owns the field schema AND the layout (varies per template).
//   Draft    — a generic envelope: which template + values for its fields.
// The template is the single source of truth for "what fields exist". A draft
// references a template by id and supplies a value for each declared field.
//
// All layout numbers (size, gap, font size) are in MILLIMETRES, with a
// center origin. mm is resolution-independent: the renderer maps mm→dots using
// the media's DPI, so a template renders at the same physical size on any
// printer. Physical alignment offset is NOT here — it's media/printer
// calibration (roadmap items 12–14).

export type FieldType = "text" // v1 only; barcode/qr later.

/** How the design is laid out on the physical label. */
export type Orientation = "portrait" | "landscape"

export interface TemplateField {
  required?: boolean
  label: string
}

export interface TextElement {
  type: "text"
  field?: string // field key from template.fields
  text?: string // literal text if no field
  rect: {
    x: number | string // mm or "x%" for percentage of media width
    y: number | string // mm or "y%" for percentage of media height
    width: number | string // mm or "w%" for percentage
    height: number | string // mm or "h%" for percentage
  }
  align?: "left" | "center" | "right"
  valign?: "top" | "center" | "bottom"
  font: {
    maxSize: number // mm, cap height
    minSize: number // mm, minimum size before giving up
    weight?: "normal" | "bold"
  }
  wrap?: boolean
  maxLines?: number
  fit?: "shrink" // more modes later
}

export type TemplateElement = TextElement

export interface Template {
  id: string
  name: string
  fields: Record<string, TemplateField>
  elements: TemplateElement[]
}

/**
 * The data for a label. References a preset (template + media + orientation).
 * A draft is pure data; the preset determines how it renders.
 */
export interface Draft {
  label?: string // optional display name
  preset?: string // the preset id (template + media + orientation)
  /** Legacy: template suggestion from old-style deep links; will be resolved to preset. */
  template?: string
  fields: Record<string, string>
}

/**
 * A physical label (the roll loaded in the printer). Owns size, SKU, material,
 * and the calibrated offset of the label on the print head. DPI is NOT here —
 * that's the printer's head resolution (see dymo.ts), passed into mm→dots.
 */
export interface Media {
  id: string
  name: string
  sku?: string
  material?: string
  size: { w: number; h: number } // mm
  offset?: { x: number; y: number } // mm — where the label sits on the head
  printers?: string[] // ids of printers this roll can be used on
}

/**
 * A printer profile. Holds printer-level physical traits shared by all media —
 * notably the leading-edge dead zone (gap sensor → print head distance), which
 * is the same for every roll. (DPI + head width still live in the driver while
 * there's a single printer; they move here when item 24 adds more.)
 */
export interface Printer {
  id: string
  name: string
  // Identity for now, so media can link to it via `media.printers`. Physical
  // traits (DPI, head width, transport) join here with item 24. The print
  // placement offset lives on the media (a roll is specific to a printer).
}

/**
 * A named, reusable output format = template + media. A draft offers every
 * preset whose template fits its fields (so one chemical → "Vial" and
 * "Bucket"). Presets are shared across drafts, defined once.
 */
export interface Preset {
  id: string
  name: string
  template: string // template id
  media: string // media id
  orientation?: Orientation // defaults to "portrait"
}
