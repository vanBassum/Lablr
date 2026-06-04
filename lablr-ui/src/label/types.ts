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
  key: string
  label: string
  type?: FieldType // defaults to "text"
}

/** A line of text — either a literal, or bound to a field's value via `field`. */
export interface TextNode {
  type: "text"
  field?: string
  text?: string
  size: number // cap height, mm
  weight?: "normal" | "bold"
  align?: "left" | "center" | "right"
}

/** A stack of nodes. v1 renders vertical stacks; horizontal comes later. */
export interface StackNode {
  type: "stack"
  direction?: "vertical"
  align?: "left" | "center" | "right" // cross-axis default for children
  gap?: number // mm
  children: LayoutNode[]
}

export type LayoutNode = TextNode | StackNode

export interface Template {
  id: string
  name: string
  size: { w: number; h: number } // mm — the label this template is designed for
  fields: TemplateField[]
  layout: LayoutNode
}

/**
 * The data for a label — values only, NOT bound to a template. The same draft
 * (e.g. a chemical) can be rendered by any compatible template (big bucket
 * label, small vial label). A template is compatible when the draft supplies
 * every field the template needs (see `templateAccepts`).
 */
export interface Draft {
  label?: string // optional display name
  /** Optional template the AI suggests for this data — a hint, freely overridable. */
  template?: string
  values: Record<string, string>
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
}
