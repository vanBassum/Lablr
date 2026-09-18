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
  type Medium,
  type PrintResult,
  type PrintStatus,
  type RenderHeader,
} from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"
import { ImageIcon, PrinterIcon, RefreshCwIcon } from "lucide-react"
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

  // Printing. Geometry comes from the selected medium - size AND the calibrated
  // offsets - so nothing here converts millimetres or knows where the paper sits
  // under the head. Threshold stays, because it is about the artwork.
  const [printer, setPrinter] = useState<PrintStatus | null>(null)
  const [media, setMedia] = useState<Medium[]>([])
  const [mediaId, setMediaId] = useState("")
  const [threshold, setThreshold] = useState(128)
  const [printing, setPrinting] = useState(false)
  const [lastJob, setLastJob] = useState<PrintResult | null>(null)

  const medium = media.find((m) => m.id === mediaId) ?? null

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
    backend.printStatus().then(setPrinter).catch(() => setPrinter(null))
    backend
      .mediaList()
      .then((r) => {
        setMedia(r.media ?? [])
        // Steer the selection only when it points at nothing, so a reload does
        // not silently move a print onto different stock.
        if (r.media?.length && !r.media.some((m) => m.id === mediaId))
          setMediaId(r.media[0].id)
      })
      .catch(() => setMedia([]))
  }

  useEffect(() => {
    if (connection !== "connected") return
    refresh()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connection])

  // Picking a medium sizes the preview as well as the print. The preview is
  // only honest if it is the same raster the printer gets.
  function selectMedium(id: string) {
    setMediaId(id)
    const m = media.find((x) => x.id === id)
    if (m?.widthDots && m?.heightDots) {
      setWidth(m.widthDots)
      setHeight(m.heightDots)
    }
  }

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

  async function print() {
    setPrinting(true)
    try {
      // Render happens ON the device for a print: shipping the bitmap up here
      // and the job back down would be the same pixels twice, and would make
      // the browser a step the MCP path does not have.
      // Send the medium, not the numbers: the device applies its calibration,
      // and a value typed here would quietly override it.
      const res = await backend.printSvg(
        mediaId ? { path, media: mediaId, threshold }
                : { path, width, height, threshold },
      )
      setLastJob(res)
      if (res.warning) toast.warning("Printed, but blank", { description: res.warning })
      else toast.success("Printed", {
        description: `${res.jobBytes?.toLocaleString()} bytes, ${res.blackDots?.toLocaleString()} dots of ink`,
      })
    } catch (e) {
      toast.error("Print failed", { description: errorMessage(e) })
    } finally {
      setPrinting(false)
      backend.printStatus().then(setPrinter).catch(() => {})
    }
  }

  async function printCalibrate() {
    setPrinting(true)
    try {
      const res = await backend.printCalibrate()
      setLastJob(res)
      toast.success("Calibration grid sent", {
        description: "Measure where the label's edges fall, then set the offsets on Media.",
      })
    } catch (e) {
      toast.error("Calibration print failed", { description: errorMessage(e) })
    } finally {
      setPrinting(false)
    }
  }

  async function printTest() {
    setPrinting(true)
    try {
      const res = await backend.printTest()
      setLastJob(res)
      toast.success("Test pattern sent", {
        description: `${res.jobBytes?.toLocaleString()} bytes`,
      })
    } catch (e) {
      toast.error("Test print failed", { description: errorMessage(e) })
    } finally {
      setPrinting(false)
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

      <div className="space-y-4 rounded-xl border bg-card p-4 shadow-sm">
        <div className="flex items-center justify-between">
          <h2 className="text-lg font-semibold">Print</h2>
          {printer === null ? (
            <Badge variant="outline">Unknown</Badge>
          ) : printer.ready ? (
            <Badge variant="secondary" className="gap-1">
              {printer.product || printer.id || "Printer"}
              {printer.paperEmpty && <span className="text-destructive">no paper</span>}
            </Badge>
          ) : (
            <Badge variant="outline">No printer</Badge>
          )}
        </div>

        {printer && !printer.ready && (
          <p className="text-xs text-muted-foreground">
            {printer.note ??
              "Nothing on the USB host port."}
          </p>
        )}

        {printer?.deviceId && (
          <p className="break-all font-mono text-xs text-muted-foreground">{printer.deviceId}</p>
        )}

        <div className="space-y-2">
          <Label>Media</Label>
          {media.length === 0 ? (
            <p className="text-sm text-muted-foreground">
              No media defined. Add one on the Media page, then the size and the calibration
              come from it.
            </p>
          ) : (
            <div className="flex flex-wrap gap-1">
              {media.map((m) => (
                <Button
                  key={m.id}
                  variant={m.id === mediaId ? "secondary" : "ghost"}
                  size="sm"
                  className="h-7"
                  onClick={() => selectMedium(m.id)}
                >
                  {m.name}
                </Button>
              ))}
            </div>
          )}
          {medium && (
            <p className="text-xs text-muted-foreground tabular-nums">
              {(medium.widthUm / 1000).toFixed(1)} x {(medium.heightUm / 1000).toFixed(1)} mm
              {" = "}
              {medium.widthDots} x {medium.heightDots} dots, placed at head (
              {medium.offsetXDots ?? 0}, {medium.offsetYDots ?? 0}). Printable{" "}
              {medium.printableWidthDots} x {medium.printableHeightDots} dots.
              {((medium.offsetXDots ?? 0) < 0 || (medium.offsetYDots ?? 0) < 0) &&
                " A negative offset is label that sits before the head's origin and cannot be printed at all - keep the design's content clear of it."}
            </p>
          )}
        </div>

        <div className="flex flex-wrap items-end gap-3">
          <div className="space-y-2">
            <Label htmlFor="print-threshold">Threshold</Label>
            <Input
              id="print-threshold"
              type="number"
              className="w-24"
              value={threshold}
              onChange={(e) => setThreshold(Number(e.target.value))}
            />
          </div>
          <Button onClick={print} disabled={printing || !path || !printer?.ready}>
            <PrinterIcon className="size-4" />
            {printing ? "Printing..." : "Print"}
          </Button>
          <Button variant="outline" onClick={printTest} disabled={printing || !printer?.ready}>
            Test pattern
          </Button>
          <Button variant="outline" onClick={printCalibrate} disabled={printing || !printer?.ready}>
            Calibration grid
          </Button>
        </div>

        <p className="text-xs text-muted-foreground">
          The medium supplies the size and the calibrated offsets, so Print sends its name
          rather than any numbers from this page. Threshold is about the artwork, not the
          paper: raise it to make thin anti-aliased text print heavier. The calibration grid
          is how a medium's offsets get measured in the first place.
        </p>

        {lastJob && (
          <div className="grid grid-cols-2 gap-3 border-t pt-3 text-sm sm:grid-cols-4">
            <Field label="Job" value={lastJob.jobBytes != null ? `${lastJob.jobBytes.toLocaleString()} B` : "-"} />
            <Field label="Bytes/line" value={lastJob.bytesPerLine?.toString() ?? "-"} />
            <Field label="Ink dots" value={lastJob.blackDots?.toLocaleString() ?? "-"} />
            <Field label="Render" value={lastJob.renderMs != null ? `${lastJob.renderMs} ms` : "-"} />
            <Field label="Convert" value={lastJob.convertMs != null ? `${lastJob.convertMs} ms` : "-"} />
            <Field label="Send" value={lastJob.sendMs != null ? `${lastJob.sendMs} ms` : "-"} />
          </div>
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
