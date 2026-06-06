import { createServer, type IncomingMessage, type ServerResponse } from "node:http"
import { createCanvas, loadImage, type Image } from "canvas"
// The ONE renderer: the exact code the PWA uses, run here in Node.
import { renderService } from "../lablr-ui/src/services/render"
import { buildJobFromCanvas } from "../lablr-ui/src/dymo"

const PORT = Number(process.env.PORT ?? 8090)

// node-canvas Canvas duck-types to the DOM canvas the renderer expects.
const makeCanvas = (w: number, h: number) =>
  createCanvas(Math.max(1, Math.round(w)), Math.max(1, Math.round(h))) as unknown as HTMLCanvasElement

function rotate90cw(src: HTMLCanvasElement): HTMLCanvasElement {
  const out = makeCanvas(src.height, src.width)
  const ctx = out.getContext("2d")!
  ctx.translate(out.width, 0)
  ctx.rotate(Math.PI / 2)
  ctx.drawImage(src as unknown as CanvasImageSource, 0, 0)
  return out
}

interface RenderRequest {
  fields: Record<string, string>
  elements: unknown[]
  orientation: string
  stock: { offsetCorrectionMm?: { x?: number; y?: number } } & Record<string, unknown>
  printer: { dpi: number } & Record<string, unknown>
  pictograms?: Record<string, string>
}

async function renderJob(body: RenderRequest): Promise<Buffer> {
  // Preload referenced pictograms (SVG markup → image) so the sync renderer can draw them.
  const imgs = new Map<string, Image>()
  for (const [name, svg] of Object.entries(body.pictograms ?? {})) {
    try {
      imgs.set(name, await loadImage(Buffer.from(svg)))
    } catch {
      /* unknown/invalid svg — skip, render leaves it blank */
    }
  }
  const getPictogram = (name: string) =>
    (imgs.get(name) ?? null) as unknown as CanvasImageSource | null

  const canvas = makeCanvas(1, 1) // renderer resizes to the stock's dot dimensions
  // eslint-disable-next-line @typescript-eslint/no-explicit-any
  renderService.render(canvas, {
    draft: { id: "render", fields: body.fields },
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    elements: body.elements as any,
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    orientation: body.orientation as any,
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    stock: body.stock as any,
    // eslint-disable-next-line @typescript-eslint/no-explicit-any
    printer: body.printer as any,
    getPictogram,
  })

  const mmToDots = body.printer.dpi / 25.4
  const offX = Math.round((body.stock.offsetCorrectionMm?.x ?? 0) * mmToDots)
  const offY = Math.round((body.stock.offsetCorrectionMm?.y ?? 0) * mmToDots)
  const oriented = body.orientation === "landscape" ? rotate90cw(canvas) : canvas
  const job = buildJobFromCanvas(oriented, { x: offX, y: offY }, 128, makeCanvas)
  return Buffer.from(job)
}

const server = createServer((req: IncomingMessage, res: ServerResponse) => {
  if (req.method === "GET" && req.url === "/health") {
    res.writeHead(200)
    res.end("ok")
    return
  }
  if (req.method === "POST" && req.url === "/render") {
    const chunks: Buffer[] = []
    req.on("data", (c: Buffer) => chunks.push(c))
    req.on("end", async () => {
      try {
        const body = JSON.parse(Buffer.concat(chunks).toString("utf8")) as RenderRequest
        const job = await renderJob(body)
        res.writeHead(200, { "Content-Type": "application/octet-stream", "Content-Length": job.length })
        res.end(job)
      } catch (e) {
        res.writeHead(500, { "Content-Type": "application/json" })
        res.end(JSON.stringify({ error: (e as Error)?.message ?? String(e) }))
      }
    })
    return
  }
  res.writeHead(404)
  res.end()
})

server.listen(PORT, () => console.log(`lablr-render listening on :${PORT}`))
