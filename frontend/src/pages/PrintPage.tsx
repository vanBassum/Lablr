// The product's own screen: pick a label, see it, print it.
//
// Three columns, left to right in the order the work happens - which labels
// exist, what the selected one looks like, and what it takes to put it on
// paper. It is a client of `fs list`, `render svg`, `media list` and
// `print svg` and of nothing else, so driving this page exercises exactly the
// interface an external caller (or the relay's MCP surface) drives.
//
// There are two pictures of a label here, and they are deliberately not the
// same picture.
//
// The STRIP shows the SVG itself, handed to the browser. It costs one `fs read`
// - which the strip already made, to learn the design's own size - and no
// device work at all. Eighty labels used to mean eighty device renders queued
// one behind another at about a second each; now it means eighty small file
// reads, and the list fills as fast as it can be scrolled.
//
// The PREVIEW is rendered ON THE DEVICE at the selected medium's dot geometry,
// because that is the only preview that is honest: the same rasteriser, the
// same size, the same fonts, thresholded to the same dots. The browser has none
// of the device's /fonts, so a design whose text uses one shows a substitute in
// the strip - close enough to recognise a label by, never close enough to print
// from. That difference is the whole reason both pictures exist.
import { useCallback, useEffect, useRef, useState } from "react"
import {
  backend,
  type FontEntry,
  type Medium,
  type PrintResult,
  type PrintStatus,
} from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"
import {
  ChevronDownIcon,
  ChevronUpIcon,
  FileTextIcon,
  GridIcon,
  MinusIcon,
  PlusIcon,
  PrinterIcon,
  RefreshCwIcon,
  SearchIcon,
  SquareDashedIcon,
  Trash2Icon,
  UploadIcon,
  ZoomInIcon,
  ZoomOutIcon,
} from "lucide-react"
import { Button } from "@/components/ui/button"
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

/** A design's own size, from its root element. width/height first, viewBox as
 *  the fallback - an SVG is allowed to carry only the latter. This is what says
 *  whether a design was drawn FOR the selected stock or merely fits on it, which
 *  is the one thing a thumbnail cannot show. */
function svgSize(text: string): { w: number; h: number } | null {
  // From the <svg> tag, not from the first ">" in the file: a document that
  // opens with an <?xml ... ?> prolog would otherwise be measured on the prolog.
  const start = text.indexOf("<svg")
  if (start < 0) return null
  const root = text.slice(start, text.indexOf(">", start) + 1)
  const num = (attr: string) => {
    const m = root.match(new RegExp(`\\b${attr}\\s*=\\s*["']([\\d.]+)`))
    return m ? Number(m[1]) : NaN
  }
  const w = num("width")
  const h = num("height")
  if (w > 0 && h > 0) return { w, h }
  const vb = root.match(/\bviewBox\s*=\s*["']\s*[\d.-]+\s+[\d.-]+\s+([\d.]+)\s+([\d.]+)/)
  if (vb) return { w: Number(vb[1]), h: Number(vb[2]) }
  return null
}

/** The SVG's own bytes as a data URL, for the strip. Base64 of the raw bytes
 *  rather than of decoded text: the file never has to be a string, and nothing
 *  has to guess its encoding. A data URL rather than a blob URL so it can live
 *  in state and be dropped with it - a blob URL would need revoking. */
function svgDataUrl(bytes: Uint8Array): string {
  let binary = ""
  for (let i = 0; i < bytes.length; i++) binary += String.fromCharCode(bytes[i])
  return `data:image/svg+xml;base64,${btoa(binary)}`
}

/** Identifies one preview: a picture is the current one only if it is of this
 *  label AT this geometry. One function so the request side and the "is what is
 *  on screen still current" side cannot drift apart. */
function previewKeyOf(path: string, w: number, h: number): string {
  return `${path}|${w}|${h}`
}

/** One unit of device work. There are only two kinds, and the queue below is a
 *  function that picks between them rather than a list anything pushes to.
 *
 *  They are no longer the same weight: a `thumb` is one small file read and a
 *  `preview` is a full rasterisation. That is what lets the order below put the
 *  expensive one second rather than last. */
type Job =
  | { kind: "thumb"; path: string }
  | { kind: "preview"; path: string }

export default function PrintPage() {
  const connection = useConnectionStatus()
  const canvasRef = useRef<HTMLCanvasElement>(null)

  const [labels, setLabels] = useState<string[]>([])
  const [selected, setSelected] = useState<string | null>(null)
  const [filter, setFilter] = useState("")
  const [fonts, setFonts] = useState<FontEntry[] | null>(null)

  const [printer, setPrinter] = useState<PrintStatus | null>(null)
  const [media, setMedia] = useState<Medium[]>([])
  const [mediaId, setMediaId] = useState("")
  const [threshold, setThreshold] = useState(128)
  const [quantity, setQuantity] = useState(1)
  const [advanced, setAdvanced] = useState(false)

  const [busy, setBusy] = useState(false)
  const [printing, setPrinting] = useState(false)
  const [zoom, setZoom] = useState(1)
  // What the canvas actually holds. The canvas is ONE element that outlives a
  // selection change, so without this the previous label's pixels stay on screen
  // for the seconds the new render takes - the worst of both, since they look
  // authoritative. Compared against the key the effect below requests.
  const [drawnKey, setDrawnKey] = useState<string | null>(null)
  // Monotonic request number. The effect's own `cancelled` flag closes over one
  // run, which is already enough for a re-render; this also rejects a reply that
  // outlived its request for any other reason, so a slow render of a label that
  // is no longer selected can never reach the canvas.
  const requestSeq = useRef(0)
  const [lastJob, setLastJob] = useState<PrintResult | null>(null)
  const [thumbs, setThumbs] = useState<Record<string, string>>({})
  const [dims, setDims] = useState<Record<string, { w: number; h: number } | null>>({})
  // Thumbnails ATTEMPTED, which is not the same as thumbnails we have: a label
  // that will not parse belongs here too, or the queue below offers it forever.
  const [tried, setTried] = useState<Record<string, true>>({})
  // Rows that have been scrolled into view at least once.
  const [seen, setSeen] = useState<Record<string, true>>({})
  // Bumped by a finished job, purely so the queue effect re-runs after one that
  // changed nothing else.
  const [tick, setTick] = useState(0)

  const listRef = useRef<HTMLDivElement>(null)
  const observerRef = useRef<IntersectionObserver | null>(null)
  const rowsRef = useRef(new Map<string, HTMLElement>())
  const workerBusy = useRef(false)

  // Stable, so React calls it once per row rather than on every re-render.
  const rowRef = useCallback((element: HTMLElement | null) => {
    const path = element?.dataset.path
    if (!element || !path) return
    rowsRef.current.set(path, element)
    observerRef.current?.observe(element)
  }, [])

  const medium = media.find((m) => m.id === mediaId) ?? null
  // Custom size is an override for calibration; until someone opens Advanced and
  // changes it, it simply mirrors the medium.
  const [custom, setCustom] = useState<{ w: number; h: number } | null>(null)
  const w = custom?.w ?? medium?.widthDots ?? 400
  const h = custom?.h ?? medium?.heightDots ?? 200

  const shown = labels.filter((l) =>
    l.toLowerCase().includes(filter.trim().toLowerCase()),
  )

  // Is the canvas showing the label that is selected now, at the geometry that is
  // selected now? While it is not, the strip's thumbnail stands in for it - the
  // browser already has that picture, and the device render takes seconds.
  const previewCurrent = selected !== null && drawnKey === previewKeyOf(selected, w, h)
  const standIn = selected ? thumbs[selected] : undefined

  const refresh = useCallback(() => {
    // Reload means "the files may have changed underneath me", so what was
    // rendered FROM them is stale. Clearing the attempt record is what re-offers
    // every thumbnail to the queue; the pictures themselves are kept so the
    // strip does not blank out while they come back, and the preview key is
    // dropped so the open label is drawn again too.
    setTried({})
    setDrawnKey(null)
    backend
      .fsList("/labels")
      .then((r) => {
        // Only SVGs. /labels is where designs live, but nothing stops someone
        // dropping a note in it, and offering a .txt here only buys a parse error.
        const svgs = r.entries
          .filter((e) => !e.dir && e.name.toLowerCase().endsWith(".svg"))
          .map((e) => `/labels/${e.name}`)
          .sort((a, b) => a.localeCompare(b))
        setLabels(svgs)
        setSelected((cur) => (cur && svgs.includes(cur) ? cur : (svgs[0] ?? null)))
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
        setMediaId((cur) =>
          r.media?.some((m) => m.id === cur) ? cur : (r.media?.[0]?.id ?? ""),
        )
      })
      .catch(() => setMedia([]))
  }, [])

  useEffect(() => {
    if (connection !== "connected") return
    refresh()
  }, [connection, refresh])

  // One label's own size and its strip picture, both out of ONE file read.
  //
  // The read was always here - the size comes from the SVG's root element, and
  // it is the DESIGN's size, not the medium's, so a design drawn for other stock
  // looks wrong here rather than looking fine and printing wrong. What is gone
  // is the second round trip that asked the device to rasterise a 96 px bitmap
  // of a file the browser was already holding.
  const loadThumb = useCallback(async (path: string) => {
    try {
      const { bytes } = await backend.fsRead(path)
      setDims((d) => ({ ...d, [path]: svgSize(new TextDecoder().decode(bytes)) }))
      setThumbs((t) => ({ ...t, [path]: svgDataUrl(bytes) }))
    } catch {
      /* unreadable: the row shows its name, no size and no picture */
    } finally {
      setTried((t) => ({ ...t, [path]: true }))
    }
  }, [])

  // The full preview, at the medium's real geometry, straight onto the canvas.
  const renderPreview = useCallback(
    async (path: string, seq: number) => {
      const key = previewKeyOf(path, w, h)
      try {
        const res = await backend.renderSvg(path, w, h)
        // Nothing else may have asked in the meantime. A slow render of a label
        // that is no longer selected must never reach the canvas.
        if (seq !== requestSeq.current) return
        const rw = res.header.width ?? w
        const rh = res.header.height ?? h
        const canvas = canvasRef.current
        if (!canvas) return
        canvas.width = rw
        canvas.height = rh
        canvas.getContext("2d")?.putImageData(toImageData(res.bytes, rw, rh), 0, 0)
        // Only now is the canvas this label's, and only now does the markup
        // below stop showing the thumbnail standing in for it.
        setDrawnKey(key)
      } catch (e) {
        if (seq === requestSeq.current)
          toast.error("Render failed", { description: errorMessage(e) })
        // Marked done anyway, so a design the rasteriser refuses does not have
        // the queue asking for it again on every tick.
        setDrawnKey(key)
      }
    },
    [w, h],
  )

  // Which rows the eye has actually reached. A label nobody has scrolled to is a
  // render nobody has asked for, and the device does them one at a time at about
  // a second each - so a drawer of eighty designs used to mean eighty jobs queued
  // on a page load, and the one label you were looking at waited behind all of
  // them. Membership is one-way: seen once is seen.
  useEffect(() => {
    const root = listRef.current
    if (!root) return
    const observer = new IntersectionObserver(
      (entries) => {
        setSeen((cur) => {
          let next = cur
          for (const entry of entries) {
            const path = (entry.target as HTMLElement).dataset.path
            if (!entry.isIntersecting || !path || cur[path]) continue
            if (next === cur) next = { ...cur }
            next[path] = true
          }
          return next
        })
      },
      // A screen's worth of lead, so scrolling finds pictures already there
      // rather than starting the render as the row lands.
      { root, rootMargin: "200px" },
    )
    observerRef.current = observer
    // Rows mounted before this effect ran - which is all of them on a reload -
    // are registered in the map and would otherwise never be observed.
    for (const element of rowsRef.current.values()) observer.observe(element)
    return () => {
      observer.disconnect()
      observerRef.current = null
    }
  }, [])

  // ── The render queue ──────────────────────────────────────────────────────
  //
  // ONE job at a time, chosen fresh each time the previous finishes, in the
  // order the page is actually read:
  //
  //   1. the selected label's own SVG        - it stands in for the preview, so
  //      it is what turns a blank panel into a picture, and it is one file read
  //   2. the selected label's full preview    - the only slow job on the list
  //   3. the SVGs of the other rows on screen - the strip fills in behind it
  //
  // The preview used to come last, behind every visible row, because a thumbnail
  // was a device render too and putting the expensive job first would have made
  // the strip crawl. Now that a thumbnail is a file read, the preview is the one
  // thing worth waiting for and everything else can happen behind it. It still
  // replaces something correct rather than nothing, because step 1 ran first.
  //
  // There is no queue object. `pick` is a pure function of current state, so a
  // change of selection or a scroll re-prioritises what happens next without
  // anything having to be cancelled or drained.
  const pick = useCallback((): Job | null => {
    if (!selected) return null
    if (!tried[selected]) return { kind: "thumb", path: selected }
    if (drawnKey !== previewKeyOf(selected, w, h)) return { kind: "preview", path: selected }
    for (const path of labels) if (seen[path] && !tried[path]) return { kind: "thumb", path }
    return null
  }, [selected, labels, seen, tried, drawnKey, w, h])

  useEffect(() => {
    if (connection !== "connected" || workerBusy.current) return
    const job = pick()
    if (!job) {
      setBusy(false)
      return
    }

    // Every job records an attempt whether it succeeds or not. That is what
    // makes the effect a pump - each completion changes state, which re-runs
    // this and picks the next - and it is also what stops a label that will not
    // render from being retried forever.
    workerBusy.current = true
    setBusy(true)
    let cancelled = false
    const seq = ++requestSeq.current

    ;(async () => {
      try {
        if (job.kind === "thumb") await loadThumb(job.path)
        else await renderPreview(job.path, seq)
      } finally {
        workerBusy.current = false
        // A completed job always lands in state, so the re-run comes from that.
        // This only covers the unmount race.
        if (!cancelled) setTick((t) => t + 1)
      }
    })()

    return () => {
      cancelled = true
    }
  }, [connection, pick, tick, loadThumb, renderPreview])

  async function print() {
    if (!selected) return
    setPrinting(true)
    try {
      for (let i = 0; i < quantity; i++) {
        // Send the medium's NAME, not its numbers: the device applies its own
        // calibration, and a value typed on this page would quietly override it.
        // Custom size is the deliberate exception, and it is behind Advanced.
        const res = await backend.printSvg(
          custom
            ? { path: selected, width: custom.w, height: custom.h, threshold }
            : { path: selected, media: mediaId, threshold },
        )
        setLastJob(res)
        if (res.warning) {
          toast.warning("Printed, but blank", { description: res.warning })
          break
        }
      }
      if (!custom) {
        toast.success(quantity > 1 ? `Printed ${quantity} labels` : "Printed")
      }
    } catch (e) {
      toast.error("Print failed", { description: errorMessage(e) })
    } finally {
      setPrinting(false)
      backend.printStatus().then(setPrinter).catch(() => {})
    }
  }

  async function deleteLabel(path: string, name: string) {
    // Confirmed, because the file is gone from the device's filesystem and
    // there is no copy of it anywhere else - unlike a print, which only costs a
    // label. Same shape as the prompt behind New label.
    if (!window.confirm(`Delete ${name}? This cannot be undone.`)) return
    try {
      await backend.fsDelete(path)
      toast.success(`Deleted ${name}`)
      // Drop what was rendered FROM it, or a label created under the same name
      // later would show the old one's picture.
      setThumbs(({ [path]: _gone, ...rest }) => rest)
      setDims(({ [path]: _also, ...rest }) => rest)
      setTried(({ [path]: _too, ...rest }) => rest)
      refresh()
    } catch (e) {
      toast.error("Delete failed", { description: errorMessage(e) })
    }
  }

  async function printSpecial(kind: "test" | "calibrate") {
    setPrinting(true)
    try {
      const res = kind === "test" ? await backend.printTest() : await backend.printCalibrate()
      setLastJob(res)
      toast.success(kind === "test" ? "Test pattern sent" : "Calibration grid sent", {
        description:
          kind === "test"
            ? `${res.jobBytes?.toLocaleString()} bytes`
            : "Measure where the label's edges fall, then set the offsets in the medium's JSON on the Files page.",
      })
    } catch (e) {
      toast.error("Print failed", { description: errorMessage(e) })
    } finally {
      setPrinting(false)
    }
  }

  return (
    <div className="mx-auto max-w-[1400px] space-y-6">
      <h1 className="text-2xl font-bold">Print labels</h1>

      <div className="grid gap-6 lg:grid-cols-[minmax(0,20rem)_minmax(0,1fr)_minmax(0,22rem)]">
        {/* ── Labels ── */}
        <section className="flex max-h-[calc(100vh-11rem)] flex-col rounded-xl border bg-card p-4 shadow-sm">
          <h2 className="mb-3 text-lg font-semibold">Labels</h2>
          <div className="relative mb-3">
            <SearchIcon className="pointer-events-none absolute left-3 top-1/2 size-4 -translate-y-1/2 text-muted-foreground" />
            <Input
              className="pl-9"
              placeholder="Search labels..."
              value={filter}
              onChange={(e) => setFilter(e.target.value)}
            />
          </div>

          <div ref={listRef} className="-mx-1 min-h-0 flex-1 space-y-2 overflow-y-auto px-1">
            {shown.length === 0 && (
              <p className="py-8 text-center text-sm text-muted-foreground">
                {labels.length === 0
                  ? "No SVGs in /labels yet."
                  : "Nothing matches that."}
              </p>
            )}
            {shown.map((path) => {
              const name = path.replace("/labels/", "")
              const active = path === selected
              const d = dims[path]
              // Scaling preserves aspect ratio, so a design of a different shape
              // prints with big empty margins and small text. Worth saying here,
              // where the fix is to draw a new one, not to nudge a number.
              const mismatch =
                d && medium?.widthDots && medium?.heightDots
                  ? Math.abs(d.w / d.h - medium.widthDots / medium.heightDots) > 0.05
                  : false
              return (
                // A row, not a button: it holds a second control now, and a
                // button inside a button is invalid markup that browsers
                // resolve by dropping one of them. `data-path` is how the
                // observer above names what it just saw.
                <div
                  key={path}
                  ref={rowRef}
                  data-path={path}
                  className={
                    "flex w-full items-center gap-1 rounded-lg border pr-1 transition-colors " +
                    (active ? "border-foreground/60 bg-accent" : "hover:bg-accent/50")
                  }
                >
                  <button
                    onClick={() => setSelected(path)}
                    className="flex min-w-0 flex-1 items-center gap-3 p-2 text-left"
                  >
                    <span className="flex size-14 shrink-0 items-center justify-center overflow-hidden rounded-md border bg-white">
                      {thumbs[path] ? (
                        <img src={thumbs[path]} alt="" className="max-h-full max-w-full" />
                      ) : (
                        <FileTextIcon className="size-5 text-muted-foreground" />
                      )}
                    </span>
                    <span className="min-w-0">
                      <span className="block truncate text-sm font-medium">{name}</span>
                      <span className="block text-xs tabular-nums text-muted-foreground">
                        {d ? `${d.w} \u00d7 ${d.h}` : "unknown size"}
                      </span>
                      {mismatch && (
                        <span className="block text-xs text-amber-600">
                          not this medium's shape
                        </span>
                      )}
                    </span>
                  </button>
                  <Button
                    variant="ghost"
                    size="icon"
                    className="size-8 shrink-0 text-muted-foreground hover:text-destructive"
                    title={`Delete ${name}`}
                    onClick={() => deleteLabel(path, name)}
                  >
                    <Trash2Icon className="size-4" />
                  </Button>
                </div>
              )
            })}
          </div>

          <div className="mt-3 flex gap-2 border-t pt-3">
            <Button variant="outline" size="sm" className="flex-1" asChild>
              <label>
                <UploadIcon className="size-4" />
                Upload
                <input
                  type="file"
                  accept=".svg"
                  className="hidden"
                  onChange={async (e) => {
                    const f = e.target.files?.[0]
                    e.target.value = ""
                    if (!f) return
                    try {
                      await backend.fsWrite(`/labels/${f.name}`, f)
                      toast.success(`Uploaded ${f.name}`)
                      refresh()
                    } catch (err) {
                      toast.error("Upload failed", { description: errorMessage(err) })
                    }
                  }}
                />
              </label>
            </Button>
            <Button
              variant="outline"
              size="sm"
              className="flex-1"
              onClick={async () => {
                // A new label starts as a real, renderable design at the
                // selected medium's dot geometry - an empty file would only
                // render as a parse error, and the point of the page is to
                // look at something.
                const name = window.prompt("New label file name", "label.svg")
                if (!name) return
                const path = `/labels/${name.endsWith(".svg") ? name : name + ".svg"}`
                const font = fonts?.[0]?.name ?? "DejaVuSans"
                const svg =
                  `<svg xmlns="http://www.w3.org/2000/svg" width="${w}" height="${h}" viewBox="0 0 ${w} ${h}">\n` +
                  `  <rect x="0" y="0" width="${w}" height="${h}" fill="#ffffff"/>\n` +
                  `  <text x="${Math.round(w / 2)}" y="${Math.round(h / 2)}" font-family="${font}"` +
                  ` font-size="${Math.round(Math.min(w, h) / 6)}" text-anchor="middle" fill="#000000">Label</text>\n` +
                  `</svg>\n`
                try {
                  await backend.fsWrite(path, new Blob([svg]))
                  toast.success(`Created ${path}`)
                  refresh()
                  setSelected(path)
                } catch (err) {
                  toast.error("Create failed", { description: errorMessage(err) })
                }
              }}
            >
              <PlusIcon className="size-4" />
              New label
            </Button>
          </div>
        </section>

        {/* ── Preview ── */}
        <section className="flex max-h-[calc(100vh-11rem)] flex-col rounded-xl border bg-card p-4 shadow-sm">
          <div className="mb-3 flex items-center justify-between gap-2">
            <h2 className="text-lg font-semibold">Preview</h2>
            <div className="flex items-center gap-2">
              <span className="rounded-md bg-muted px-2 py-1 text-xs tabular-nums text-muted-foreground">
                {w} &times; {h} dots
              </span>
              <Button variant="outline" size="icon" onClick={() => setZoom((z) => Math.min(4, z * 1.25))} title="Zoom in">
                <ZoomInIcon className="size-4" />
              </Button>
              <Button variant="outline" size="icon" onClick={() => setZoom((z) => Math.max(0.25, z / 1.25))} title="Zoom out">
                <ZoomOutIcon className="size-4" />
              </Button>
              <Button variant="outline" size="icon" onClick={refresh} disabled={busy} title="Reload">
                <RefreshCwIcon className={"size-4 " + (busy ? "animate-spin" : "")} />
              </Button>
            </div>
          </div>

          <div className="flex min-h-0 flex-1 items-center justify-center overflow-auto rounded-lg bg-muted/40 p-6">
            {selected ? (
              <>
                {/* Always mounted, never conditionally rendered: the render
                    effect draws through canvasRef, and a canvas swapped out
                    while a render is in flight would have nothing to draw on.
                    Hidden rather than absent, so it keeps its bitmap too. */}
                <canvas
                  ref={canvasRef}
                  className={
                    "max-w-full rounded-sm bg-white shadow-sm" + (previewCurrent ? "" : " hidden")
                  }
                  style={{ transform: `scale(${zoom})`, transformOrigin: "center", imageRendering: "pixelated" }}
                />
                {!previewCurrent &&
                  (standIn ? (
                    // The SVG the strip already has, in the box the real render
                    // is about to fill: same width, same dot geometry, and
                    // object-contain because that is how the device fits a
                    // design into the medium. So the swap moves nothing - it
                    // only replaces the browser's idea of the fonts with the
                    // device's, and smooth edges with real dots.
                    <img
                      src={standIn}
                      alt=""
                      className="max-w-full rounded-sm bg-white object-contain shadow-sm"
                      style={{
                        width: w,
                        aspectRatio: `${w} / ${h}`,
                        transform: `scale(${zoom})`,
                        transformOrigin: "center",
                      }}
                    />
                  ) : (
                    <p className="text-sm text-muted-foreground">Rendering on the device...</p>
                  ))}
              </>
            ) : (
              <p className="text-sm text-muted-foreground">No label selected.</p>
            )}
          </div>

          <p className="mt-3 text-xs text-muted-foreground">
            Rendered on the device at the medium's own dot geometry, by the same rasteriser
            and the same fonts the printer gets - so this is what comes out. Nothing is
            consumed by looking.
          </p>
        </section>

        {/* ── Print setup ── */}
        <section className="space-y-5 self-start rounded-xl border bg-card p-4 shadow-sm">
          <h2 className="text-lg font-semibold">Print setup</h2>

          <div className="space-y-2">
            <Label htmlFor="print-media">Media</Label>
            {media.length === 0 ? (
              <p className="text-sm text-muted-foreground">
                No media defined. Add a /media/&lt;name&gt;.json on the Files page; its size
                and calibration then come from there.
              </p>
            ) : (
              <>
                <select
                  id="print-media"
                  className="h-9 w-full rounded-md border bg-transparent px-3 text-sm shadow-xs outline-none focus-visible:ring-[3px] focus-visible:ring-ring/50"
                  value={mediaId}
                  onChange={(e) => {
                    setMediaId(e.target.value)
                    setCustom(null)
                  }}
                >
                  {media.map((m) => (
                    <option key={m.id} value={m.id}>
                      {m.name || m.id}
                    </option>
                  ))}
                </select>
                {medium && (
                  <>
                    <p className="text-xs tabular-nums text-muted-foreground">
                      {(medium.widthUm / 1000).toFixed(1)} &times;{" "}
                      {(medium.heightUm / 1000).toFixed(1)} mm &middot; {medium.widthDots} &times;{" "}
                      {medium.heightDots} dots
                    </p>
                    <span className="inline-block rounded-md bg-muted px-2 py-1 text-xs tabular-nums text-muted-foreground">
                      offset {(medium.offsetXUm / 1000).toFixed(1)},{" "}
                      {(medium.offsetYUm / 1000).toFixed(1)} mm
                    </span>
                    <p className="text-xs text-muted-foreground">
                      Printable {medium.printableWidthDots} &times; {medium.printableHeightDots}{" "}
                      dots - the offsets crop the top and left, so keep content inside it.
                    </p>
                  </>
                )}
              </>
            )}
          </div>

          <div className="space-y-2">
            <Label htmlFor="print-threshold">Threshold</Label>
            <Input
              id="print-threshold"
              type="number"
              min={1}
              max={255}
              value={threshold}
              onChange={(e) => setThreshold(Number(e.target.value))}
            />
            <p className="text-xs text-muted-foreground">
              About the artwork, not the paper. Raise it to make thin anti-aliased text
              print heavier.
            </p>
          </div>

          <div className="space-y-2">
            <Label>Printer</Label>
            <div className="rounded-md border px-3 py-2 text-sm">
              {printer?.product || printer?.id || "No printer"}
            </div>
            <p className="flex items-center gap-2 text-xs text-muted-foreground">
              <span
                className={
                  "size-2 rounded-full " + (printer?.ready ? "bg-emerald-500" : "bg-red-500")
                }
              />
              {printer?.ready
                ? printer.paperEmpty
                  ? "Connected, no paper"
                  : "Connected"
                : (printer?.note ?? "Nothing on the USB host port")}
            </p>
          </div>

          <div className="space-y-2">
            <Label>Quantity</Label>
            <div className="flex items-center gap-2">
              <Button variant="outline" size="icon" onClick={() => setQuantity((q) => Math.max(1, q - 1))}>
                <MinusIcon className="size-4" />
              </Button>
              <Input
                className="text-center tabular-nums"
                type="number"
                min={1}
                value={quantity}
                onChange={(e) => setQuantity(Math.max(1, Number(e.target.value) || 1))}
              />
              <Button variant="outline" size="icon" onClick={() => setQuantity((q) => q + 1)}>
                <PlusIcon className="size-4" />
              </Button>
            </div>
          </div>

          <Button
            className="w-full"
            size="lg"
            onClick={print}
            disabled={printing || !selected || !printer?.ready || (!mediaId && !custom)}
          >
            <PrinterIcon className="size-4" />
            {printing ? "Printing..." : "Print label"}
          </Button>

          <div className="border-t pt-3">
            <button
              className="flex w-full items-center gap-1 text-sm font-medium"
              onClick={() => setAdvanced((a) => !a)}
            >
              {advanced ? <ChevronUpIcon className="size-4" /> : <ChevronDownIcon className="size-4" />}
              Advanced
            </button>

            {advanced && (
              <div className="mt-3 space-y-3">
                <Label>Custom size (dots)</Label>
                <div className="flex items-center gap-2">
                  <Input
                    type="number"
                    className="tabular-nums"
                    value={w}
                    onChange={(e) => setCustom({ w: Number(e.target.value) || 1, h })}
                  />
                  <span className="text-muted-foreground">&times;</span>
                  <Input
                    type="number"
                    className="tabular-nums"
                    value={h}
                    onChange={(e) => setCustom({ w, h: Number(e.target.value) || 1 })}
                  />
                </div>
                <p className="text-xs text-muted-foreground">
                  {custom
                    ? "Overriding the medium - the calibrated offsets are NOT applied. For calibration only; a settled value belongs in the medium's JSON on the Files page."
                    : "Mirrors the selected medium. Changing it overrides the medium and drops its calibration."}
                </p>
                {custom && (
                  <Button variant="ghost" size="sm" onClick={() => setCustom(null)}>
                    Back to the medium
                  </Button>
                )}

                <div className="flex gap-2">
                  <Button
                    variant="outline"
                    size="sm"
                    className="flex-1"
                    disabled={printing || !printer?.ready}
                    onClick={() => printSpecial("test")}
                  >
                    <SquareDashedIcon className="size-4" />
                    Test pattern
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    className="flex-1"
                    disabled={printing || !printer?.ready}
                    onClick={() => printSpecial("calibrate")}
                  >
                    <GridIcon className="size-4" />
                    Calibration grid
                  </Button>
                </div>

                {lastJob && (
                  <div className="grid grid-cols-2 gap-2 border-t pt-3 text-xs">
                    <Stat label="Job" value={lastJob.jobBytes != null ? `${lastJob.jobBytes.toLocaleString()} B` : "-"} />
                    <Stat label="Ink dots" value={lastJob.blackDots?.toLocaleString() ?? "-"} />
                    <Stat label="Bytes/line" value={lastJob.bytesPerLine?.toString() ?? "-"} />
                    <Stat label="Render" value={lastJob.renderMs != null ? `${lastJob.renderMs} ms` : "-"} />
                  </div>
                )}

                {fonts && (
                  <div className="border-t pt-3">
                    <div className="text-xs text-muted-foreground">
                      Fonts on the device. An SVG's <code>font-family</code> must match one of
                      these exactly, or its text renders as nothing at all.
                    </div>
                    <div className="mt-2 flex flex-wrap gap-1">
                      {fonts.length === 0 ? (
                        <span className="text-xs text-muted-foreground">
                          None - put a .ttf in /fonts and reboot.
                        </span>
                      ) : (
                        fonts.map((f) => (
                          <span
                            key={f.name}
                            className="rounded-md bg-muted px-2 py-1 font-mono text-xs"
                          >
                            {f.name}
                          </span>
                        ))
                      )}
                    </div>
                  </div>
                )}
              </div>
            )}
          </div>
        </section>
      </div>
    </div>
  )
}

function Stat({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <div className="text-muted-foreground">{label}</div>
      <div className="font-medium tabular-nums">{value}</div>
    </div>
  )
}
