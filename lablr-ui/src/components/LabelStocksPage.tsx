import { useEffect, useState, type ReactNode } from "react"
import { ChevronLeft, Loader2, Plus, Trash2 } from "lucide-react"
import type { LabelStock, Printer } from "@/types"
import { deleteLabel, listLabels, listPrinters, saveLabel } from "@/services/labelsApi"
import { Button } from "@/components/ui/button"
import { Label } from "@/components/ui/label"

const BLANK: LabelStock = {
  id: "",
  name: "",
  widthMm: 50,
  heightMm: 25,
  material: "",
  marginsMm: { top: 1, right: 1, bottom: 1, left: 1 },
  offsetCorrectionMm: { x: 0, y: 0 },
  compatiblePrinters: [],
}

export function LabelStocksPage() {
  const [labels, setLabels] = useState<LabelStock[] | null>(null)
  const [printers, setPrinters] = useState<Printer[]>([])
  const [editing, setEditing] = useState<LabelStock | null>(null)
  const [isNew, setIsNew] = useState(false)
  const [error, setError] = useState<string | null>(null)

  function load() {
    listLabels()
      .then(setLabels)
      .catch((e) => setError(e.message))
  }
  useEffect(() => {
    load()
    listPrinters().then(setPrinters).catch(() => {})
  }, [])

  const goHome = () => (window.location.hash = "")

  if (editing) {
    return (
      <LabelForm
        value={editing}
        isNew={isNew}
        printers={printers}
        error={error}
        onCancel={() => {
          setEditing(null)
          setError(null)
        }}
        onSave={async (label) => {
          try {
            await saveLabel(label)
            setEditing(null)
            setError(null)
            load()
          } catch (e) {
            setError((e as Error).message)
          }
        }}
      />
    )
  }

  return (
    <main className="flex flex-1 flex-col gap-3 p-4">
      <div className="flex items-center gap-1">
        <Button variant="ghost" size="icon" aria-label="Back" onClick={goHome}>
          <ChevronLeft />
        </Button>
        <span className="font-medium">Label stocks</span>
        <Button
          size="sm"
          className="ml-auto"
          onClick={() => {
            setEditing(structuredClone(BLANK))
            setIsNew(true)
            setError(null)
          }}
        >
          <Plus className="size-4" /> New
        </Button>
      </div>

      {error && <p className="text-destructive text-sm">{error}</p>}

      {labels === null ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading…
        </div>
      ) : labels.length === 0 ? (
        <p className="text-muted-foreground text-sm">No label stocks yet. Add one.</p>
      ) : (
        <ul className="flex flex-col gap-2">
          {labels.map((l) => (
            <li
              key={l.id}
              className="bg-card flex items-center gap-3 rounded-lg border p-3"
            >
              <button
                className="min-w-0 flex-1 text-left"
                onClick={() => {
                  setEditing(structuredClone(l))
                  setIsNew(false)
                  setError(null)
                }}
              >
                <div className="truncate text-sm font-medium">{l.name || l.id}</div>
                <div className="text-muted-foreground truncate text-xs">
                  {l.id} · {l.widthMm}×{l.heightMm}mm{l.material ? ` · ${l.material}` : ""}
                </div>
              </button>
              <Button
                variant="ghost"
                size="icon"
                aria-label={`Delete ${l.id}`}
                onClick={async () => {
                  if (!window.confirm(`Delete label stock "${l.id}"?`)) return
                  try {
                    await deleteLabel(l.id)
                    load()
                  } catch (e) {
                    setError((e as Error).message)
                  }
                }}
              >
                <Trash2 className="text-destructive size-4" />
              </Button>
            </li>
          ))}
        </ul>
      )}
    </main>
  )
}

function LabelForm({
  value,
  isNew,
  printers,
  error,
  onSave,
  onCancel,
}: {
  value: LabelStock
  isNew: boolean
  printers: Printer[]
  error: string | null
  onSave: (label: LabelStock) => void
  onCancel: () => void
}) {
  const [form, setForm] = useState<LabelStock>(value)
  const set = <K extends keyof LabelStock>(k: K, v: LabelStock[K]) =>
    setForm((f) => ({ ...f, [k]: v }))
  const margins = form.marginsMm ?? BLANK.marginsMm!
  const offset = form.offsetCorrectionMm ?? BLANK.offsetCorrectionMm!
  const compat = form.compatiblePrinters ?? []

  return (
    <main className="flex flex-1 flex-col gap-4 p-4 pb-24">
      <div className="flex items-center gap-1">
        <Button variant="ghost" size="icon" aria-label="Cancel" onClick={onCancel}>
          <ChevronLeft />
        </Button>
        <span className="font-medium">{isNew ? "New label stock" : form.id}</span>
      </div>

      {error && <p className="text-destructive text-sm">{error}</p>}

      <Field label="Id">
        <TextInput
          value={form.id}
          disabled={!isNew}
          placeholder="e.g. label-54x70"
          onChange={(v) => set("id", v)}
        />
      </Field>
      <Field label="Name">
        <TextInput value={form.name} onChange={(v) => set("name", v)} />
      </Field>

      <div className="flex gap-3">
        <Field label="Width (mm)">
          <NumInput value={form.widthMm} onChange={(v) => set("widthMm", v)} />
        </Field>
        <Field label="Height (mm)">
          <NumInput value={form.heightMm} onChange={(v) => set("heightMm", v)} />
        </Field>
      </div>

      <Field label="Material">
        <TextInput value={form.material ?? ""} onChange={(v) => set("material", v)} />
      </Field>

      <div>
        <Label className="text-muted-foreground text-xs">Margins (mm)</Label>
        <div className="mt-1 flex flex-wrap gap-3">
          {(["top", "right", "bottom", "left"] as const).map((side) => (
            <Field key={side} label={side[0].toUpperCase() + side.slice(1)}>
              <NumInput
                value={margins[side]}
                onChange={(v) => set("marginsMm", { ...margins, [side]: v })}
              />
            </Field>
          ))}
        </div>
      </div>

      <div>
        <Label className="text-muted-foreground text-xs">Offset correction (mm, print-time)</Label>
        <div className="mt-1 flex gap-3">
          <Field label="X">
            <NumInput value={offset.x} onChange={(v) => set("offsetCorrectionMm", { ...offset, x: v })} />
          </Field>
          <Field label="Y">
            <NumInput value={offset.y} onChange={(v) => set("offsetCorrectionMm", { ...offset, y: v })} />
          </Field>
        </div>
      </div>

      <div>
        <Label className="text-muted-foreground text-xs">Compatible printers</Label>
        <div className="mt-1 flex flex-col gap-1">
          {printers.length === 0 && (
            <span className="text-muted-foreground text-xs">No printers configured.</span>
          )}
          {printers.map((p) => (
            <label key={p.id} className="flex items-center gap-2 text-sm">
              <input
                type="checkbox"
                checked={compat.includes(p.id)}
                onChange={(e) =>
                  set(
                    "compatiblePrinters",
                    e.target.checked ? [...compat, p.id] : compat.filter((x) => x !== p.id),
                  )
                }
              />
              {p.name} <span className="text-muted-foreground text-xs">({p.id})</span>
            </label>
          ))}
        </div>
      </div>

      <div className="flex gap-3">
        <Field label="Manufacturer">
          <TextInput value={form.manufacturer ?? ""} onChange={(v) => set("manufacturer", v)} />
        </Field>
        <Field label="SKU">
          <TextInput value={form.sku ?? ""} onChange={(v) => set("sku", v)} />
        </Field>
      </div>

      <div className="flex gap-2 pt-2">
        <Button className="flex-1" onClick={() => onSave(form)} disabled={!form.id || !form.name}>
          Save
        </Button>
        <Button variant="outline" onClick={onCancel}>
          Cancel
        </Button>
      </div>
    </main>
  )
}

function Field({ label, children }: { label: string; children: ReactNode }) {
  return (
    <div className="flex min-w-20 flex-1 flex-col gap-1.5">
      <Label className="text-muted-foreground text-xs">{label}</Label>
      {children}
    </div>
  )
}

function TextInput({
  value,
  onChange,
  disabled,
  placeholder,
}: {
  value: string
  onChange: (v: string) => void
  disabled?: boolean
  placeholder?: string
}) {
  return (
    <input
      type="text"
      value={value}
      disabled={disabled}
      placeholder={placeholder}
      onChange={(e) => onChange(e.target.value)}
      className="border-input bg-background h-9 w-full rounded-md border px-3 text-sm disabled:opacity-60"
    />
  )
}

function NumInput({ value, onChange }: { value: number; onChange: (v: number) => void }) {
  return (
    <input
      type="number"
      value={value}
      step="any"
      onChange={(e) => onChange(Number(e.target.value))}
      className="border-input bg-background h-9 w-20 rounded-md border px-2 text-sm"
    />
  )
}
