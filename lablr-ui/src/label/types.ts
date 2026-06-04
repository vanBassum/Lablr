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
  /** Design-time fixtures: full value sets to preview the template against. */
  samples?: Array<Record<string, string>>
}

/** An instance to print: a template reference plus a value per declared field. */
export interface Draft {
  templateId: string
  values: Record<string, string>
}
