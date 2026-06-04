import { mmToDots } from "@/dymo"
import type { LayoutNode, Media, Orientation, Template, TextNode } from "./types"

type Align = "left" | "center" | "right"

function resolveText(node: TextNode, values: Record<string, string>): string {
  if (node.field !== undefined) return values[node.field] ?? ""
  return node.text ?? ""
}

function fontOf(node: TextNode): string {
  // Layout sizes are in mm; convert to device dots for the canvas.
  return `${node.weight === "bold" ? "bold " : ""}${mmToDots(node.size)}px sans-serif`
}

/** Block height a node occupies, in dots (text ≈ cap size; stack = sum + gaps). */
function measure(node: LayoutNode): number {
  if (node.type === "text") return mmToDots(node.size)
  const gap = mmToDots(node.gap ?? 0)
  const sum = node.children.map(measure).reduce((a, b) => a + b, 0)
  return sum + gap * Math.max(0, node.children.length - 1)
}

/** Draw a node into [x, x+width] starting at top `y`; returns dots consumed. */
function draw(
  ctx: CanvasRenderingContext2D,
  node: LayoutNode,
  values: Record<string, string>,
  x: number,
  y: number,
  width: number,
  inherit: Align,
): number {
  if (node.type === "text") {
    const align = node.align ?? inherit
    ctx.font = fontOf(node)
    ctx.textBaseline = "top"
    ctx.textAlign = align
    const tx = align === "left" ? x : align === "right" ? x + width : x + width / 2
    ctx.fillText(resolveText(node, values), tx, y)
    return mmToDots(node.size)
  }
  const childAlign = node.align ?? inherit
  const gap = mmToDots(node.gap ?? 0)
  let cy = y
  node.children.forEach((child, i) => {
    cy += draw(ctx, child, values, x, cy, width, childAlign)
    if (i < node.children.length - 1) cy += gap
  })
  return cy - y
}

/**
 * Render a template + draft values onto `canvas` as a 1-bit-ready black/white
 * bitmap. The canvas is always the MEDIA's physical size (the head width can't
 * change); `orientation` decides whether the design is laid out upright or
 * rotated 90° within that physical label. Layout is authored in mm with a
 * center origin and centered within the design area. This canvas IS both the
 * preview and the print payload — there is no second renderer, and no head
 * offset here (that's a print-time printer command — see dymo.ts HeadOffset).
 */
export function renderLabel(
  canvas: HTMLCanvasElement,
  template: Template,
  values: Record<string, string>,
  media: Media,
  orientation: Orientation = "portrait",
): void {
  const Wd = mmToDots(media.size.w)
  const Hd = mmToDots(media.size.h)
  canvas.width = Wd
  canvas.height = Hd

  const ctx = canvas.getContext("2d")
  if (!ctx) throw new Error("no 2d context")

  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, Wd, Hd)
  ctx.fillStyle = "black"

  // In landscape the design area is the label rotated 90°: its width is the
  // label's height and vice-versa. We lay out into that area, then rotate it
  // onto the physical bitmap.
  const landscape = orientation === "landscape"
  const designW = landscape ? Hd : Wd
  const designH = landscape ? Wd : Hd

  ctx.save()
  if (landscape) {
    ctx.translate(Wd / 2, Hd / 2)
    ctx.rotate(Math.PI / 2)
    ctx.translate(-designW / 2, -designH / 2)
  }
  const total = measure(template.layout)
  const startY = Math.max(0, (designH - total) / 2)
  draw(ctx, template.layout, values, 0, startY, designW, "center")
  ctx.restore()
}
