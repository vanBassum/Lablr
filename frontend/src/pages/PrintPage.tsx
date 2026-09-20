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
  type JobProgress,
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

/** A file's date, short enough to sit on the same line as its size. Anything
 *  from 1980 is not a date: FAT's epoch starts there, so that is what a file
 *  written before the clock was set carries. Saying so beats printing a
 *  confident 1980. */
function fmtWhen(unixSeconds: number): string {
  const d = new Date(unixSeconds * 1000)
  if (d.getFullYear() <= 1980) return "no date"
  const now = new Date()
  const sameDay = d.toDateString() === now.toDateString()
  return sameDay
    ? d.toLocaleTimeString(undefined, { hour: "2-digit", minute: "2-digit" })
    : d.toLocaleDateString(undefined, { day: "numeric", month: "short" })
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

/** A thin progress bar. `fraction` of null means the work has started but has
 *  no number attached - a ThorVG render reports nothing while it draws - so the
 *  bar animates instead of sitting at 0%, which reads as stuck. */
function ProgressBar({ label, fraction }: { label: string; fraction: number | null }) {
  return (
    <div className="mt-2">
      <div className="mb-1 flex justify-between text-xs text-muted-foreground">
        <span>{label}</span>
        {fraction !== null && <span className="tabular-nums">{Math.round(fraction * 100)}%</span>}
      </div>
      <div className="h-1 overflow-hidden rounded-full bg-muted">
        {fraction === null ? (
          <div className="h-full w-1/3 animate-[progress-slide_1.1s_ease-in-out_infinite] rounded-full bg-primary" />
        ) : (
          <div
            className="h-full rounded-full bg-primary transition-all"
            style={{ width: `${Math.round(fraction * 100)}%` }}
          />
        )}
      </div>
    </div>
  )
}

/** Identifies one preview: a picture is the current one only if it is of this
 *  label AT this geometry. One function so the request side and the "is what is
 *  on screen still current" side cannot drift apart. */
// What the canvas is currently showing. The MODE is part of it because a
// calibration grid and a label at the same size are different pictures.
function previewKeyOf(mode: string, path: string, w: number, h: number): string {
  if (mode !== "label") return `${mode}:${w}x${h}`
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

/** Label, or one of the device's built-in designs. A mode is previewed like a
 *  label and printed like a label - the device generates the pattern at the
 *  medium's dot size and runs it through the same renderer - so nothing about
 *  this page has to special-case what is on the canvas. */
type PrintMode = "label" | "calibration" | "test"

/** Everything the Advanced panel can override, in the units the device's own
 *  arguments take: dots for geometry, 1-255 for the threshold. Undefined means
 *  "whatever the medium says", which is what makes Reset a single assignment. */
type Overrides = { w?: number; h?: number; ox?: number; oy?: number; th?: number }

export default function PrintPage() {
  const connection = useConnectionStatus()
  const canvasRef = useRef<HTMLCanvasElement>(null)

  const [labels, setLabels] = useState<string[]>([])
  const [mtimes, setMtimes] = useState<Record<string, number>>({})
  const [selected, setSelected] = useState<string | null>(null)
  const [filter, setFilter] = useState("")
  const [fonts, setFonts] = useState<FontEntry[] | null>(null)

  const [printer, setPrinter] = useState<PrintStatus | null>(null)
  const [media, setMedia] = useState<Medium[]>([])
  const [machine, setMachine] = useState<{
    dpi: number; headDots: number; printer?: string
    deadLeftDots: number; deadTopDots: number
  } | null>(null)
  const [mediaId, setMediaId] = useState("")
  const [quantity, setQuantity] = useState(1)
  const [advanced, setAdvanced] = useState(false)

  const [busy, setBusy] = useState(false)
  const [printing, setPrinting] = useState(false)
  // Where the print is. null when nothing is printing; `render` carries no
  // number, `send` carries bytes out of the job's total.
  const [printProgress, setPrintProgress] = useState<JobProgress | null>(null)
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

  // What the page is showing and would print. The calibration grid and the test
  // pattern are MODES rather than buttons that print immediately: entering one
  // puts it in the preview, and the ordinary Print button then prints exactly
  // that. Pressing a button and having a label come out is what the old pair
  // did, and it meant every look cost a label.
  const [mode, setMode] = useState<PrintMode>("label")

  // Temporary overrides. They change what is previewed and what is printed and
  // they touch NOTHING on the device: calibration is a loop of trying values,
  // and a page that wrote each attempt to flash would make the loop
  // destructive. "Save to media" is the only thing that stores one.
  const [ov, setOv] = useState<Overrides>({})

  const w = ov.w ?? medium?.widthDots ?? 400
  const h = ov.h ?? medium?.heightDots ?? 200
  // The FINISHED head position - the printer's dead zone is already in it,
  // which is why it comes from placeXDots and not from the alignment.
  const ox = ov.ox ?? medium?.placeXDots ?? 0
  const oy = ov.oy ?? medium?.placeYDots ?? 0
  const threshold = ov.th ?? 128
  // Two kinds of dirty. Reset clears everything, but Save writes GEOMETRY, so
  // a threshold someone nudged must not light up a button that would not store
  // it - the threshold is a page setting and belongs to no medium.
  const geomDirty = ov.w != null || ov.h != null || ov.ox != null || ov.oy != null
  const dirty = geomDirty || ov.th != null

  const shown = labels.filter((l) =>
    l.toLowerCase().includes(filter.trim().toLowerCase()),
  )

  // Is the canvas showing the label that is selected now, at the geometry that is
  // selected now? While it is not, the strip's thumbnail stands in for it - the
  // browser already has that picture, and the device render takes seconds.
  const previewCurrent = mode !== "label"
    ? drawnKey === previewKeyOf(mode, "", w, h)
    : selected !== null && drawnKey === previewKeyOf(mode, selected, w, h)
  // A pattern has no thumbnail to stand in for it, and should not borrow the
  // selected label's.
  const standIn = mode === "label" && selected ? thumbs[selected] : undefined

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
        //
        // Newest first: the label somebody just uploaded is the one they came
        // here to print, and it is also what `setSelected` below then opens.
        //
        // Name is the tie-break, not a fallback. FAT cannot store a date before
        // 1980, so every file written while the clock was unset shares one
        // timestamp - without a second key those would shuffle between loads.
        const svgs = r.entries
          .filter((e) => !e.dir && e.name.toLowerCase().endsWith(".svg"))
          .sort((a, b) => (b.mtime ?? 0) - (a.mtime ?? 0) || a.name.localeCompare(b.name))
          .map((e) => `/labels/${e.name}`)
        setLabels(svgs)
        setMtimes(
          Object.fromEntries(
            r.entries.filter((e) => e.mtime).map((e) => [`/labels/${e.name}`, e.mtime as number]),
          ),
        )
        setSelected((cur) => (cur && svgs.includes(cur) ? cur : (svgs[0] ?? null)))
      })
      .catch(() => setLabels([]))
    backend.renderFonts().then((r) => setFonts(r.fonts)).catch(() => setFonts(null))
    backend.printStatus().then(setPrinter).catch(() => setPrinter(null))
    backend
      .mediaList()
      .then((r) => {
        setMedia(r.media ?? [])
        // The machine's own numbers, reported once alongside the media. The
        // dead zone is what makes a printable area smaller than its label, so
        // the panel below needs it to say which loss is whose.
        setMachine({
          dpi: r.dpi,
          headDots: r.headDots,
          printer: r.printer,
          deadLeftDots: r.deadLeftDots ?? 0,
          deadTopDots: r.deadTopDots ?? 0,
        })
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
  //
  // As a PNG, not as the device's native ARGB8888S. The rasterisation is
  // identical either way - it is the same `Render()` the printer calls - but
  // the reply is not: raw is four bytes a pixel down a 512-byte session
  // window, so a 300x640 dot label was three quarters of a megabyte in about
  // 1,500 WebSocket frames. That, and not the drawing, is why a preview felt so
  // much slower than a print, which sends the label to the printer and only a
  // few hundred bytes of stats back here. A label is flat colour, so the PNG is
  // a fraction of that and the browser decodes it natively.
  const renderPreview = useCallback(
    async (path: string, seq: number) => {
      const key = previewKeyOf(mode, path, w, h)
      try {
        // No onProgress: a streamed PNG declares no length, so there is no
        // total to be a fraction of and the bar animates instead. Honest, and
        // no longer the several-second wait that made a number worth having.
        // A pattern goes through the same command, the same renderer and the
        // same fit as a label. That is what lets the Print button below print
        // the picture on the canvas rather than something generated twice.
        const res = await backend.renderSvg(
          mode === "label" ? path : null, w, h,
          { format: "png", ...(mode === "label" ? {} : { pattern: mode }) },
        )
        // Nothing else may have asked in the meantime. A slow render of a label
        // that is no longer selected must never reach the canvas.
        if (seq !== requestSeq.current) return
        // Decoded by the browser, which knows PNG; the device's own idea of the
        // size is still what the canvas is sized to, so a reply that disagreed
        // with the request would show as the wrong shape rather than silently
        // scaling.
        const bitmap = await createImageBitmap(
          new Blob([res.bytes as BlobPart], { type: "image/png" }),
        )
        const canvas = canvasRef.current
        if (seq !== requestSeq.current || !canvas) {
          bitmap.close()
          return
        }
        canvas.width = bitmap.width
        canvas.height = bitmap.height
        canvas.getContext("2d")?.drawImage(bitmap, 0, 0)
        bitmap.close()
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
    [w, h, mode],
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
  //   2. the SVGs of the other rows on screen - the strip fills in
  //   3. the selected label's full preview    - the only slow job on the list
  //
  // Every cheap job goes before the expensive one, and that ordering is only
  // affordable because a thumbnail stopped being a device render: the whole
  // visible strip is now a handful of small file reads, so putting it first
  // delays the preview by almost nothing and the list stops looking
  // half-loaded. When a thumbnail WAS a render, this same order meant the
  // preview waited behind a second per visible row.
  //
  // The preview going last still costs nothing visually, because step 1 already
  // put this label's own SVG in the panel: the render replaces something
  // correct rather than nothing.
  //
  // There is no queue object. `pick` is a pure function of current state, so a
  // change of selection or a scroll re-prioritises what happens next without
  // anything having to be cancelled or drained.
  const pick = useCallback((): Job | null => {
    // In a pattern mode there is no file to read and no strip to fill: the one
    // job is the pattern itself.
    if (mode !== "label") {
      return drawnKey !== previewKeyOf(mode, "", w, h) ? { kind: "preview", path: "" } : null
    }
    if (!selected) return null
    if (!tried[selected]) return { kind: "thumb", path: selected }
    for (const path of labels) if (seen[path] && !tried[path]) return { kind: "thumb", path }
    if (drawnKey !== previewKeyOf(mode, selected, w, h)) return { kind: "preview", path: selected }
    return null
  }, [mode, selected, labels, seen, tried, drawnKey, w, h])

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

  // Print exactly what the preview is showing. The arguments are built from the
  // same values the canvas was drawn with, so there is no second opinion about
  // geometry anywhere on this page.
  async function print() {
    if (mode === "label" && !selected) return
    const copies = mode === "label" ? quantity : 1
    setPrinting(true)
    try {
      for (let i = 0; i < copies; i++) {
        // The medium's NAME carries its size and calibration, and only the
        // overrides someone actually typed are sent on top - so an untouched
        // page prints what the device decided, and a calibration run prints
        // what the panel says.
        const res = await backend.printSvg(
          {
            ...(mode === "label" ? { path: selected! } : { pattern: mode }),
            media: mediaId || undefined,
            ...(ov.w != null ? { width: ov.w } : {}),
            ...(ov.h != null ? { height: ov.h } : {}),
            ...(ov.ox != null ? { offsetX: ov.ox } : {}),
            ...(ov.oy != null ? { offsetY: ov.oy } : {}),
            threshold,
          },
          setPrintProgress,
        )
        setLastJob(res)
        if (res.warning) {
          toast.warning("Printed, but blank", { description: res.warning })
          break
        }
      }
      if (mode === "calibration") {
        toast.success("Calibration grid printed", {
          description:
            "Count rings from the centre marker to the middle of the paper, then nudge the offsets in Advanced and print again.",
        })
      } else if (mode === "test") {
        toast.success("Test pattern printed")
      } else {
        toast.success(copies > 1 ? `Printed ${copies} labels` : "Printed")
      }
    } catch (e) {
      toast.error("Print failed", { description: errorMessage(e) })
    } finally {
      setPrinting(false)
      setPrintProgress(null)
      backend.printStatus().then(setPrinter).catch(() => {})
    }
  }

  // Alignment is measured from the first dot the machine can reach, while the
  // offsets on this page are the finished head position. They differ by exactly
  // the dead zone, so storing one as the other would move every future print by
  // that much - the same conversion the device does when it migrates an old
  // medium, and wrong in the same silent way if skipped.
  async function saveToMedium() {
    if (!medium || !machine) return
    const deadX = (medium.deadLeftDots ?? machine.deadLeftDots) + (medium.marginLeftDots ?? 0)
    const deadY = (medium.deadTopDots ?? machine.deadTopDots) + (medium.marginTopDots ?? 0)
    const toUm = (d: number) => Math.round((d * 25400) / machine.dpi)
    try {
      await backend.mediaSet({
        id: medium.id,
        ...(ov.w != null ? { widthUm: toUm(ov.w) } : {}),
        ...(ov.h != null ? { heightUm: toUm(ov.h) } : {}),
        alignXUm: toUm(ox + deadX),
        alignYUm: toUm(oy + deadY),
      })
      const r = await backend.mediaList()
      setMedia(r.media ?? [])
      setOv({})
      toast.success(`Saved to ${medium.name || medium.id}`, {
        description: "Stored as the medium's alignment; every print on this stock uses it now.",
      })
    } catch (e) {
      toast.error("Could not save", { description: errorMessage(e) })
    }
  }

  // Entering a mode prints NOTHING. It puts the pattern in the preview, and the
  // ordinary Print button below is what commits a label to it.
  function enterMode(next: PrintMode) {
    setMode(next)
    setDrawnKey(null)
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



  return (
    <div className="mx-auto max-w-[1400px] space-y-6">
      <h1 className="text-2xl font-bold">Print labels</h1>

      <div className="grid gap-6 lg:grid-cols-[minmax(0,20rem)_minmax(0,1fr)_minmax(0,22rem)]">
        {/* ── Labels ── */}
        <section
          className={
            "flex max-h-[calc(100vh-11rem)] flex-col rounded-xl border bg-card p-4 shadow-sm" +
            // Dimmed and inert while a pattern is being previewed: the Print
            // button is about to print the pattern, so offering a label to
            // select would be offering something that will not happen.
            (mode !== "label" ? " pointer-events-none opacity-40" : "")
          }
          aria-hidden={mode !== "label"}
        >
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
                        {mtimes[path] ? ` \u00b7 ${fmtWhen(mtimes[path])}` : ""}
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
          {mode !== "label" && (
            <div className="mb-3 flex items-center justify-between gap-2 rounded-md border border-primary/40 bg-primary/10 px-3 py-2">
              <span className="text-sm">
                <strong>
                  {mode === "calibration" ? "Calibration grid" : "Test pattern"}
                </strong>{" "}
                <span className="text-muted-foreground">
                  {mode === "calibration"
                    ? "- rings every 25 dots from the centre, heavier every 100."
                    : "- bars and a grey sweep; the first grey to ink is where the threshold sits."}
                </span>
              </span>
              <Button variant="outline" size="sm" onClick={() => enterMode("label")}>
                Back to label
              </Button>
            </div>
          )}

          <div className="mb-3 flex items-center justify-between gap-2">
            <h2 className="text-lg font-semibold">
              {mode === "label" ? "Preview" : "Preview - pattern"}
            </h2>
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
            {selected || mode !== "label" ? (
              <>
                {/* Always mounted, never conditionally rendered: the render
                    effect draws through canvasRef, and a canvas swapped out
                    while a render is in flight would have nothing to draw on.
                    Hidden rather than absent, so it keeps its bitmap too. */}
                {/* The canvas and the dead-zone overlay share one box, so the
                    shading scales with the zoom and stays registered to the
                    picture without measuring anything. */}
                <div
                  className={"relative" + (previewCurrent ? "" : " hidden")}
                  style={{ transform: `scale(${zoom})`, transformOrigin: "center" }}
                >
                  <canvas
                    ref={canvasRef}
                    className="block max-w-full rounded-sm bg-white shadow-sm"
                    style={{ imageRendering: "pixelated" }}
                  />
                  {/* What the machine cannot reach. A negative placement means
                      that much of the design falls before the head's first
                      column or the printer's first line, so it is drawn and
                      then simply never printed - the one thing a preview that
                      claims to be the print has to admit. */}
                  {ox < 0 && (
                    <div
                      className="pointer-events-none absolute inset-y-0 left-0 border-r border-red-500/60 bg-red-500/20"
                      style={{ width: `${Math.min(100, (-ox / w) * 100)}%` }}
                      title={`${-ox} dots the printer cannot reach`}
                    />
                  )}
                  {oy < 0 && (
                    <div
                      className="pointer-events-none absolute inset-x-0 top-0 border-b border-red-500/60 bg-red-500/20"
                      style={{ height: `${Math.min(100, (-oy / h) * 100)}%` }}
                      title={`${-oy} dots the printer cannot reach`}
                    />
                  )}
                </div>
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
                    <div className="w-48">
                      {/* Always indeterminate: a render reports nothing while
                          it draws, and the PNG that follows it declares no
                          length to count against. */}
                      <ProgressBar label="Rendering on the device" fraction={null} />
                    </div>
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
            {(ox < 0 || oy < 0) && (
              <>
                {" "}
                The shaded strips are the {-Math.min(0, ox)} &times; {-Math.min(0, oy)} dots
                that fall outside what the machine can reach, and will not print.
              </>
            )}
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
                    // Overrides are calibration for THIS stock; carrying them
                    // onto another roll would be carrying a measurement to
                    // paper it was not measured on.
                    setOv({})
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
                    <p className="text-xs tabular-nums text-muted-foreground">
                      Printable {medium.printableWidthDots} &times;{" "}
                      {medium.printableHeightDots} dots. Keep content inside it.
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
              onChange={(e) => setOv((o) => ({ ...o, th: Number(e.target.value) || 1 }))}
            />
            <p className="text-xs text-muted-foreground">
              About the artwork, not the paper. Raise it to make thin anti-aliased text
              print heavier. Not stored on the medium.
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
            disabled={
              printing ||
              !printer?.ready ||
              (mode === "label" && !selected) ||
              (!mediaId && ov.w == null)
            }
          >
            <PrinterIcon className="size-4" />
            {printing
              ? "Printing..."
              : mode === "calibration"
                ? "Print calibration grid"
                : mode === "test"
                  ? "Print test pattern"
                  : "Print label"}
          </Button>

          {printing && (
            <ProgressBar
              label={
                printProgress?.phase === "send"
                  ? quantity > 1
                    ? `Sending to the printer (${printProgress.done} of ${printProgress.total} bytes)`
                    : "Sending to the printer"
                  : "Rendering the label"
              }
              fraction={
                printProgress?.phase === "send" && printProgress.total
                  ? printProgress.done / printProgress.total
                  : null
              }
            />
          )}

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
                {/* What each number is a fact ABOUT, because the fix
                    differs: the machine's dead zone is measured once, a roll's
                    margin belongs to that roll, and only the alignment is a
                    choice. */}
                {medium && machine && (
                  <div className="space-y-1 rounded-md bg-muted/50 p-2 text-xs tabular-nums">
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Printer {machine.printer ?? ""}</span>
                      <span>{machine.dpi} dpi &middot; {machine.headDots} dots</span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Dead zone (machine)</span>
                      <span>
                        {medium.deadLeftDots ?? machine.deadLeftDots} &times;{" "}
                        {medium.deadTopDots ?? machine.deadTopDots} dots
                      </span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Margin (this roll)</span>
                      <span>
                        {medium.marginLeftDots ?? 0} &times; {medium.marginTopDots ?? 0} dots
                      </span>
                    </div>
                    <div className="flex justify-between">
                      <span className="text-muted-foreground">Alignment</span>
                      <span>
                        {((medium.alignXUm ?? 0) / 1000).toFixed(2)},{" "}
                        {((medium.alignYUm ?? 0) / 1000).toFixed(2)} mm
                      </span>
                    </div>
                    <div className="flex justify-between border-t pt-1">
                      <span className="text-muted-foreground">Printable</span>
                      <span>
                        {medium.printableWidthDots} &times; {medium.printableHeightDots} dots
                      </span>
                    </div>
                    {medium.legacyOffsets && (
                      <p className="pt-1 text-muted-foreground">
                        Still using its pre-split offsets. Saving below converts it to an
                        alignment without moving anything.
                      </p>
                    )}
                  </div>
                )}

                <Label>Overrides (dots)</Label>
                <div className="grid grid-cols-[auto_1fr_1fr] items-center gap-2">
                  <span className="text-xs text-muted-foreground">Size</span>
                  <Input
                    type="number"
                    className="h-8 tabular-nums"
                    value={w}
                    onChange={(e) => setOv((o) => ({ ...o, w: Number(e.target.value) || 1 }))}
                  />
                  <Input
                    type="number"
                    className="h-8 tabular-nums"
                    value={h}
                    onChange={(e) => setOv((o) => ({ ...o, h: Number(e.target.value) || 1 }))}
                  />
                  <span className="text-xs text-muted-foreground">Offset</span>
                  <Input
                    type="number"
                    className="h-8 tabular-nums"
                    value={ox}
                    onChange={(e) => setOv((o) => ({ ...o, ox: Number(e.target.value) || 0 }))}
                  />
                  <Input
                    type="number"
                    className="h-8 tabular-nums"
                    value={oy}
                    onChange={(e) => setOv((o) => ({ ...o, oy: Number(e.target.value) || 0 }))}
                  />
                </div>
                <p className="text-xs text-muted-foreground">
                  {dirty
                    ? "Overriding the medium for this page only - nothing is stored until you save."
                    : "Mirrors the selected medium. Offset is the finished head position, with the dead zone already in it."}
                </p>

                <div className="flex gap-2">
                  <Button
                    variant="ghost"
                    size="sm"
                    className="flex-1"
                    disabled={!dirty}
                    onClick={() => setOv({})}
                  >
                    Reset to media
                  </Button>
                  <Button
                    variant="outline"
                    size="sm"
                    className="flex-1"
                    disabled={!geomDirty || !medium || !machine}
                    onClick={saveToMedium}
                  >
                    Save to media
                  </Button>
                </div>

                <div className="flex gap-2 border-t pt-3">
                  <Button
                    variant={mode === "test" ? "default" : "outline"}
                    size="sm"
                    className="flex-1"
                    onClick={() => enterMode(mode === "test" ? "label" : "test")}
                  >
                    <SquareDashedIcon className="size-4" />
                    Test pattern
                  </Button>
                  <Button
                    variant={mode === "calibration" ? "default" : "outline"}
                    size="sm"
                    className="flex-1"
                    onClick={() => enterMode(mode === "calibration" ? "label" : "calibration")}
                  >
                    <GridIcon className="size-4" />
                    Calibration grid
                  </Button>
                </div>
                <p className="text-xs text-muted-foreground">
                  These show the design in the preview. Nothing prints until you press
                  Print.
                </p>

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
