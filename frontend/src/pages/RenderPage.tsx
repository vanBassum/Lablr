// Bench page for the renderer: pick a stored SVG, give it a size, look at what
// the device drew.
//
// It calls `render svg` - the same command an external caller uses - and does
// the pixel conversion here. The device has no business encoding a PNG for a
// browser that already knows how to put an ImageData on a canvas.
import { useEffect, useRef, useState } from "react"
import {
  backend,
  type FontEntry,
  type RenderHeader,
} from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"
import { ImageIcon, RefreshCwIcon } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { toast } from "sonner"

function errorMessage(e: unknown): string {
  return e instanceof Error ? e.message : "Unknown error"
}

/** ARGB8888S is one 32-bit LITTLE-ENDIAN word per pixel, so in memory the bytes
 *  are B,G,R,A. ImageData wants R,G,B,A. Un-premultiplied is what makes this a
 *  channel swap and not an un-multiply, which is why the device sends the S
 *  variant. */
function toImageData(bytes: Uint8Array, width: number, height: number): ImageData {
  const out = new Uint8ClampedArray(width * height * 4)
  for (let i = 0; i < out.length; i += 4) {
    out[i] = bytes[i + 2]
    out[i + 1] = bytes[i + 1]
    out[i + 2] = bytes[i]
    out[i + 3] = bytes[i + 3]
  }
  return new ImageData(out, width, height)
}

export default function RenderPage() {
  const connection = useConnectionStatus()
  const canvasRef = useRef<HTMLCanvasElement>(null)

  const [labels, setLabels] = useState<string[]>([])
  const [fonts, setFonts] = useState<FontEntry[] | null>(null)
  const [path, setPath] = useState("/labels/test.svg")
  const [width, setWidth] = useState(400)
  const [height, setHeight] = useState(200)
  const [busy, setBusy] = useState(false)
  const [header, setHeader] = useState<RenderHeader | null>(null)
  const [elapsed, setElapsed] = useState<number | null>(null)

  function refresh() {
    backend
      .fsList("/labels")
      .then((r) => {
        // Only SVGs: /labels is where label designs live, but nothing stops
        // someone dropping a note in it, and a shortcut to a .txt would only
        // produce a parse error.
        const svgs = r.entries
          .filter((e) => !e.dir && e.name.toLowerCase().endsWith(".svg"))
          .map((e) => `/labels/${e.name}`)
        setLabels(svgs)
        // Only steer the field if it still holds a path that is not there.
        if (svgs.length && !svgs.includes(path)) setPath(svgs[0])
      })
      .catch(() => setLabels([]))
    backend.renderFonts().then((r) => setFonts(r.fonts)).catch(() => setFonts(null))
  }

  useEffect(() => {
    if (connection !== "connected") return
    refresh()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connection])

  async function render() {
    setBusy(true)
    try {
      const res = await backend.renderSvg(path, width, height)
      setHeader(res.header)
      setElapsed(res.elapsedMs)

      const w = res.header.width ?? width
      const h = res.header.height ?? height
      const expected = w * h * 4
      if (res.bytes.length < expected)
        throw new Error(`short bitmap: ${res.bytes.length} of ${expected} bytes`)

      const canvas = canvasRef.current
      if (canvas) {
        canvas.width = w
        canvas.height = h
        canvas.getContext("2d")?.putImageData(toImageData(res.bytes, w, h), 0, 0)
      }
    } catch (e) {
      setHeader(null)
      setElapsed(null)
      toast.error("Render failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  return (
    <div className="mx-auto max-w-3xl space-y-6">
      <h1 className="text-2xl font-bold">Render</h1>

      <div className="space-y-4 rounded-xl border bg-card p-4 shadow-sm">
        <div className="space-y-2">
          <Label htmlFor="render-path">SVG path</Label>
          <Input
            id="render-path"
            value={path}
            onChange={(e) => setPath(e.target.value)}
            list="label-paths"
            placeholder="/labels/test.svg"
          />
          <datalist id="label-paths">
            {labels.map((l) => (
              <option key={l} value={l} />
            ))}
          </datalist>
          {labels.length > 0 && (
            <div className="flex flex-wrap gap-1 pt-1">
              {labels.map((l) => (
                <Button
                  key={l}
                  variant={l === path ? "secondary" : "ghost"}
                  size="sm"
                  className="h-7"
                  onClick={() => setPath(l)}
                >
                  {l.replace("/labels/", "")}
                </Button>
              ))}
            </div>
          )}
        </div>

        <div className="flex flex-wrap items-end gap-3">
          <div className="space-y-2">
            <Label htmlFor="render-w">Width</Label>
            <Input
              id="render-w"
              type="number"
              className="w-28"
              value={width}
              onChange={(e) => setWidth(Number(e.target.value))}
            />
          </div>
          <div className="space-y-2">
            <Label htmlFor="render-h">Height</Label>
            <Input
              id="render-h"
              type="number"
              className="w-28"
              value={height}
              onChange={(e) => setHeight(Number(e.target.value))}
            />
          </div>
          <Button onClick={render} disabled={busy || !path}>
            <ImageIcon className="size-4" />
            {busy ? "Rendering..." : "Render"}
          </Button>
          <Button variant="outline" size="icon" onClick={refresh} disabled={busy} title="Reload labels and fonts">
            <RefreshCwIcon className="size-4" />
          </Button>
        </div>
      </div>

      <div className="rounded-xl border bg-card p-4 shadow-sm">
        <h2 className="mb-3 text-lg font-semibold">Preview</h2>
        {/* Checkerboard, so a transparent background is visible as one rather
            than as whatever the card colour happens to be. */}
        <div
          className="inline-block max-w-full overflow-auto rounded-lg border p-2"
          style={{
            backgroundImage:
              "linear-gradient(45deg,#0001 25%,transparent 25%,transparent 75%,#0001 75%)," +
              "linear-gradient(45deg,#0001 25%,transparent 25%,transparent 75%,#0001 75%)",
            backgroundSize: "16px 16px",
            backgroundPosition: "0 0, 8px 8px",
          }}
        >
          <canvas ref={canvasRef} className="block max-w-full" />
        </div>

        {header && (
          <div className="mt-4 grid grid-cols-2 gap-3 text-sm sm:grid-cols-3">
            <Field label="Source" value={header.path ?? path} />
            <Field label="Size" value={`${header.width} x ${header.height}`} />
            <Field label="Format" value={header.format ?? "-"} />
            <Field label="Bytes" value={header.bytes != null ? header.bytes.toLocaleString() : "-"} />
            <Field label="Stride" value={header.stride != null ? `${header.stride} B` : "-"} />
            <Field label="Scale" value={header.scale != null ? header.scale.toFixed(3) : "-"} />
            <Field
              label="Round trip"
              value={elapsed != null ? `${Math.round(elapsed)} ms` : "-"}
            />
            <Field
              label="PSRAM used"
              value={header.psramUsed != null ? `${(header.psramUsed / 1024).toFixed(0)} KB` : "-"}
            />
            <Field
              label="Worker stack left"
              value={header.workerStackLeft != null ? `${header.workerStackLeft} B` : "-"}
            />
          </div>
        )}
        {elapsed != null && (
          <p className="mt-2 text-xs text-muted-foreground">
            Round trip is measured in the browser and includes transferring{" "}
            {header?.bytes != null ? `${(header.bytes / 1024).toFixed(0)} KB` : "the bitmap"} over
            the WebSocket - the device does not report its own render time.
          </p>
        )}
      </div>

      <div className="rounded-xl border bg-card p-4 shadow-sm">
        <h2 className="mb-1 text-lg font-semibold">Fonts</h2>
        <p className="mb-3 text-xs text-muted-foreground">
          An SVG's <code>font-family</code> must match one of these names exactly. Text with an
          unknown family renders as nothing at all, silently. Fonts are read from /fonts at
          boot, so a newly uploaded one needs a reboot.
        </p>
        {fonts === null ? (
          <p className="text-sm text-muted-foreground">Unknown.</p>
        ) : fonts.length === 0 ? (
          <p className="text-sm text-muted-foreground">
            No fonts registered - put a .ttf in /fonts and reboot.
          </p>
        ) : (
          <div className="flex flex-wrap gap-2">
            {fonts.map((f) => (
              <Badge key={f.name} variant="secondary" className="gap-1">
                {f.name}
                <span className="text-muted-foreground">{(f.bytes / 1024).toFixed(0)} KB</span>
              </Badge>
            ))}
          </div>
        )}
      </div>
    </div>
  )
}

function Field({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <div className="text-xs text-muted-foreground">{label}</div>
      <div className="truncate font-medium tabular-nums">{value}</div>
    </div>
  )
}
