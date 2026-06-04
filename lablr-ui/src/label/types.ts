// The label model. Two layers:
//   Template — owns the field schema AND the layout (varies per template).
//   Draft    — a generic envelope: which template + values for its fields.
// The template is the single source of truth for "what fields exist". A draft
// references a template by id and supplies a value for each declared field.

export type FieldType = "text" // v1 only; barcode/qr later.

export interface TemplateField {
  key: string
  label: string
  type: FieldType
}

/** A line of text — either a literal, or bound to a field's value via `field`. */
export interface TextNode {
  type: "text"
  field?: string
  text?: string
  size: number // in label dot-space
  weight?: "normal" | "bold"
  align?: "left" | "center" | "right"
}

/** A stack of nodes. v1 renders vertical stacks; horizontal comes later. */
export interface StackNode {
  type: "stack"
  direction?: "vertical"
  gap?: number
  children: LayoutNode[]
}

export type LayoutNode = TextNode | StackNode

export interface Template {
  id: string
  name: string
  widthMm: number
  heightMm: number
  fields: TemplateField[]
  layout: LayoutNode
}

/** An instance to print: a template reference plus a value per declared field. */
export interface Draft {
  templateId: string
  values: Record<string, string>
}
