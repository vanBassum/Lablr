import { mmToDots, MAX_BYTES_PER_LINE } from "@/dymo"
import type { LayoutNode, Template, TextNode } from "./types"

const HEAD_DOTS = MAX_BYTES_PER_LINE * 8 // 672 — full print-head width

export interface RenderOptions {
  /** Label position on the head, in dots. Lives in the canvas so preview = print. */
  offsetX?: number
  offsetY?: number
}

function resolveText(node: TextNode, values: Record<string, string>): string {
  if (node.field !== undefined) return values[node.field] ?? ""
  return node.text ?? ""
}

function fontOf(node: TextNode): string {
  return `${node.weight === "bold" ? "bold " : ""}${node.size}px sans-serif`
}

/** Block height a node occupies (text ≈ font size; stack = sum + gaps). */
function measure(node: LayoutNode): number {
  if (node.type === "text") return node.size
  const gap = node.gap ?? 0
  const heights = node.children.map(measure)
  const sum = heights.reduce((a, b) => a + b, 0)
  return sum + gap * Math.max(0, node.children.length - 1)
}

/** Draw a node into [x, x+width] starting at top `y`; returns height consumed. */
function draw(
  ctx: CanvasRenderingContext2D,
  node: LayoutNode,
  values: Record<string, string>,
  x: number,
  y: number,
  width: number,
): number {
  if (node.type === "text") {
    ctx.font = fontOf(node)
    ctx.textBaseline = "top"
    const align = node.align ?? "center"
    ctx.textAlign = align
    const tx = align === "left" ? x : align === "right" ? x + width : x + width / 2
    ctx.fillText(resolveText(node, values), tx, y)
    return node.size
  }
  const gap = node.gap ?? 0
  let cy = y
  node.children.forEach((child, i) => {
    cy += draw(ctx, child, values, x, cy, width)
    if (i < node.children.length - 1) cy += gap
  })
  return cy - y
}

/**
 * Render a template + draft values onto `canvas` as a 1-bit-ready black/white
 * bitmap. The canvas is sized to the full head width × label height; the label
 * content is centered vertically and positioned by the offsets. This canvas is
 * both the preview and the print payload — there is no second renderer.
 */
export function renderLabel(
  canvas: HTMLCanvasElement,
  template: Template,
  values: Record<string, string>,
  opts: RenderOptions = {},
): void {
  const labelW = mmToDots(template.widthMm)
  const labelH = mmToDots(template.heightMm)
  canvas.width = HEAD_DOTS
  canvas.height = labelH

  const ctx = canvas.getContext("2d")
  if (!ctx) throw new Error("no 2d context")

  ctx.fillStyle = "white"
  ctx.fillRect(0, 0, canvas.width, canvas.height)
  ctx.fillStyle = "black"

  ctx.save()
  ctx.translate(opts.offsetX ?? 0, opts.offsetY ?? 0)
  const total = measure(template.layout)
  const startY = Math.max(0, (labelH - total) / 2)
  draw(ctx, template.layout, values, 0, startY, labelW)
  ctx.restore()
}
