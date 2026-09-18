// Label stock: what the paper IS, edited on the device.
//
// Everything here goes through `media list` / `media set` / `media delete` — the
// same commands an MCP agent uses, so a medium added from a browser and one
// added by an agent are the same object. Nothing about label stock is compiled
// into this bundle.
//
// Micrometres are what the device stores; this page shows and edits millimetres,
// because nobody reads a label box in micrometres. The conversion is the only
// thing the page does to the numbers.
import { useEffect, useState } from "react"
import { backend, type Medium } from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"
import { PlusIcon, RefreshCwIcon, SaveIcon, Trash2Icon } from "lucide-react"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
import { Input } from "@/components/ui/input"
import { Label } from "@/components/ui/label"
import { toast } from "sonner"

function errorMessage(e: unknown): string {
  return e instanceof Error ? e.message : "Unknown error"
}

const mmOf = (um: number) => (um / 1000).toFixed(1)
const umOf = (mm: string) => Math.round(parseFloat(mm || "0") * 1000)

/** The editor's fields are strings so a half-typed "-" or "1." is not fought. */
interface Draft {
  id: string
  name: string
  widthMm: string
  heightMm: string
  offsetXMm: string
  offsetYMm: string
}

const emptyDraft: Draft = {
  id: "", name: "", widthMm: "25.0", heightMm: "25.0", offsetXMm: "0.0", offsetYMm: "0.0",
}

function draftOf(m: Medium): Draft {
  return {
    id: m.id,
    name: m.name,
    widthMm: mmOf(m.widthUm),
    heightMm: mmOf(m.heightUm),
    offsetXMm: mmOf(m.offsetXUm),
    offsetYMm: mmOf(m.offsetYUm),
  }
}

export default function MediaPage() {
  const connection = useConnectionStatus()
  const [media, setMedia] = useState<Medium[]>([])
  const [dpi, setDpi] = useState(300)
  const [headDots, setHeadDots] = useState(672)
  const [draft, setDraft] = useState<Draft | null>(null)
  const [busy, setBusy] = useState(false)

  function refresh() {
    backend
      .mediaList()
      .then((r) => {
        setMedia(r.media ?? [])
        setDpi(r.dpi)
        setHeadDots(r.headDots)
      })
      .catch(() => setMedia([]))
  }

  useEffect(() => {
    if (connection !== "connected") return
    refresh()
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connection])

  async function save() {
    if (!draft) return
    setBusy(true)
    try {
      await backend.mediaSet({
        id: draft.id.trim(),
        name: draft.name.trim() || draft.id.trim(),
        widthUm: umOf(draft.widthMm),
        heightUm: umOf(draft.heightMm),
        offsetXUm: umOf(draft.offsetXMm),
        offsetYUm: umOf(draft.offsetYMm),
      })
      toast.success("Saved", { description: draft.id })
      setDraft(null)
      refresh()
    } catch (e) {
      toast.error("Could not save", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function remove(id: string) {
    setBusy(true)
    try {
      await backend.mediaDelete(id)
      toast.success("Deleted", { description: id })
      if (draft?.id === id) setDraft(null)
      refresh()
    } catch (e) {
      toast.error("Could not delete", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  const dotsPerMm = dpi / 25.4

  return (
    <div className="mx-auto max-w-3xl space-y-6">
      <div className="flex items-center justify-between">
        <h1 className="text-2xl font-bold">Media</h1>
        <div className="flex gap-2">
          <Button variant="outline" size="icon" onClick={refresh} disabled={busy} title="Reload">
            <RefreshCwIcon className="size-4" />
          </Button>
          <Button onClick={() => setDraft({ ...emptyDraft })} disabled={busy}>
            <PlusIcon className="size-4" />
            Add
          </Button>
        </div>
      </div>

      <p className="text-sm text-muted-foreground">
        A medium describes the <strong>paper</strong> — its size, and where it sits under the
        print head. It never describes the printer: this LabelWriter is{" "}
        <strong>{dpi} DPI</strong> with a <strong>{headDots}-dot</strong> head for every roll
        anyone loads, so those are not fields here.
      </p>

      {media.length === 0 ? (
        <div className="rounded-xl border bg-card p-4 text-sm text-muted-foreground shadow-sm">
          No media defined. Add one from the dimensions on the label box.
        </div>
      ) : (
        <div className="space-y-2">
          {media.map((m) => (
            <div
              key={m.id}
              className="flex flex-wrap items-center gap-3 rounded-xl border bg-card p-3 shadow-sm"
            >
              <div className="min-w-40 flex-1">
                <div className="font-medium">{m.name}</div>
                <div className="font-mono text-xs text-muted-foreground">{m.id}</div>
              </div>
              <div className="text-sm tabular-nums">
                {mmOf(m.widthUm)} × {mmOf(m.heightUm)} mm
                <span className="ml-2 text-muted-foreground">
                  {m.widthDots} × {m.heightDots} dots
                </span>
              </div>
              {(m.offsetXUm !== 0 || m.offsetYUm !== 0) && (
                <Badge variant="secondary" className="tabular-nums">
                  offset {mmOf(m.offsetXUm)}, {mmOf(m.offsetYUm)} mm
                </Badge>
              )}
              {m.printableWidthDots != null && (
                <span className="text-xs text-muted-foreground tabular-nums">
                  printable {m.printableWidthDots} x {m.printableHeightDots}
                </span>
              )}
              <div className="flex gap-1">
                <Button variant="outline" size="sm" onClick={() => setDraft(draftOf(m))}>
                  Edit
                </Button>
                <Button
                  variant="ghost"
                  size="icon"
                  onClick={() => remove(m.id)}
                  disabled={busy}
                  title="Delete"
                >
                  <Trash2Icon className="size-4" />
                </Button>
              </div>
            </div>
          ))}
        </div>
      )}

      {draft && (
        <div className="space-y-4 rounded-xl border bg-card p-4 shadow-sm">
          <h2 className="text-lg font-semibold">
            {media.some((m) => m.id === draft.id) ? "Edit medium" : "New medium"}
          </h2>

          <div className="flex flex-wrap gap-3">
            <div className="space-y-2">
              <Label htmlFor="m-id">Id</Label>
              <Input
                id="m-id"
                className="w-40 font-mono"
                value={draft.id}
                placeholder="square25"
                onChange={(e) => setDraft({ ...draft, id: e.target.value })}
              />
            </div>
            <div className="min-w-48 flex-1 space-y-2">
              <Label htmlFor="m-name">Name</Label>
              <Input
                id="m-name"
                value={draft.name}
                placeholder="Square 25 x 25 mm"
                onChange={(e) => setDraft({ ...draft, name: e.target.value })}
              />
            </div>
          </div>

          <div className="flex flex-wrap gap-3">
            <Field
              id="m-w"
              label="Width (mm)"
              hint="across the head"
              value={draft.widthMm}
              onChange={(v) => setDraft({ ...draft, widthMm: v })}
            />
            <Field
              id="m-h"
              label="Height (mm)"
              hint="along the feed"
              value={draft.heightMm}
              onChange={(v) => setDraft({ ...draft, heightMm: v })}
            />
            <Field
              id="m-ox"
              label="Offset X (mm)"
              hint="calibration"
              value={draft.offsetXMm}
              onChange={(v) => setDraft({ ...draft, offsetXMm: v })}
            />
            <Field
              id="m-oy"
              label="Offset Y (mm)"
              hint="calibration, often negative"
              value={draft.offsetYMm}
              onChange={(v) => setDraft({ ...draft, offsetYMm: v })}
            />
          </div>

          <p className="text-xs text-muted-foreground">
            Offsets are <strong>measured, not chosen</strong>. Print the calibration grid from the
            Render page, see where the label's edges fall against it, and put the result here.
            Offset Y is negative when the printer starts printing after the label's leading edge
            has passed, which crops that much off the top of a design. At {dpi} DPI one millimetre
            is {dotsPerMm.toFixed(2)} dots, so this medium is{" "}
            <span className="tabular-nums">
              {Math.round(parseFloat(draft.widthMm || "0") * dotsPerMm)} ×{" "}
              {Math.round(parseFloat(draft.heightMm || "0") * dotsPerMm)}
            </span>{" "}
            dots.
          </p>

          <div className="flex gap-2">
            <Button onClick={save} disabled={busy || !draft.id.trim()}>
              <SaveIcon className="size-4" />
              Save
            </Button>
            <Button variant="ghost" onClick={() => setDraft(null)} disabled={busy}>
              Cancel
            </Button>
          </div>
        </div>
      )}
    </div>
  )
}

function Field({
  id, label, hint, value, onChange,
}: {
  id: string
  label: string
  hint: string
  value: string
  onChange: (v: string) => void
}) {
  return (
    <div className="space-y-2">
      <Label htmlFor={id}>{label}</Label>
      <Input
        id={id}
        type="number"
        step="0.1"
        className="w-32"
        value={value}
        onChange={(e) => onChange(e.target.value)}
      />
      <div className="text-xs text-muted-foreground">{hint}</div>
    </div>
  )
}
