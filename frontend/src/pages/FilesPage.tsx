// A file manager for the label filesystem.
//
// It is a client of the `fs` commands and nothing else - there is no HTTP
// filesystem route behind this page - so using it exercises the same interface
// an external caller (or the relay's MCP surface) will drive. If something
// works here it works there.
//
// A TREE rather than one directory at a time: the filesystem is three fixed
// folders deep and never grows a fourth level, so paging through it one
// `fs list` at a time only hid what was there. The whole tree is three listings,
// fetched once.
import { useCallback, useEffect, useMemo, useState } from "react"
import { backend, type FsEntry, type FsInfo } from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"
import {
  ChevronDownIcon,
  ChevronRightIcon,
  DownloadIcon,
  FileIcon,
  FileTextIcon,
  FolderIcon,
  PlusIcon,
  RefreshCwIcon,
  SaveIcon,
  SearchIcon,
  Trash2Icon,
  TypeIcon,
  FilePlusIcon,
  UploadIcon,
  XIcon,
} from "lucide-react"
import { Button } from "@/components/ui/button"
import { Input } from "@/components/ui/input"
import { toast } from "sonner"

function errorMessage(e: unknown): string {
  return e instanceof Error ? e.message : "Unknown error"
}

function fmtSize(bytes: number): string {
  if (bytes >= 1024 * 1024) return `${(bytes / 1024 / 1024).toFixed(1)} MB`
  if (bytes >= 1024) return `${(bytes / 1024).toFixed(1)} KB`
  return `${bytes} B`
}

/** What a file IS, in this filesystem's vocabulary rather than the extension's.
 *  The device cares about exactly three kinds and treats everything else as
 *  bytes, so that is what the column says. */
function kindOf(name: string): { label: string; icon: typeof FileIcon } {
  const ext = name.slice(name.lastIndexOf(".")).toLowerCase()
  if (ext === ".svg") return { label: "SVG", icon: FileTextIcon }
  if (ext === ".ttf" || ext === ".otf") return { label: "Font", icon: TypeIcon }
  if (ext === ".json") return { label: "JSON", icon: FileTextIcon }
  return { label: "File", icon: FileIcon }
}

// Which files are worth opening in the editor. Everything else is offered as a
// download only - a font in a textarea helps nobody.
const TEXT_EXT = [".svg", ".json", ".txt", ".csv", ".xml", ".md"]
function isText(name: string): boolean {
  const dot = name.lastIndexOf(".")
  return dot >= 0 && TEXT_EXT.includes(name.slice(dot).toLowerCase())
}

function join(dir: string, name: string): string {
  return dir === "/" ? `/${name}` : `${dir}/${name}`
}

type Row = { path: string; name: string; dir: boolean; size: number; depth: number }

export default function FilesPage() {
  const connection = useConnectionStatus()
  const [tree, setTree] = useState<Record<string, FsEntry[]>>({})
  const [info, setInfo] = useState<FsInfo | null>(null)
  const [open, setOpen] = useState<Record<string, boolean>>({ "/fonts": true, "/labels": true })
  const [filter, setFilter] = useState("")
  const [sortBy, setSortBy] = useState<"name" | "size">("size")
  const [desc, setDesc] = useState(true)
  const [busy, setBusy] = useState(false)

  const [selected, setSelected] = useState<string | null>(null)
  // The editor buffer, and what was read, so Save is offered only when it would
  // change something.
  const [editing, setEditing] = useState<{ path: string; text: string; original: string } | null>(null)

  const refresh = useCallback(async () => {
    setBusy(true)
    try {
      backend.fsInfo().then(setInfo).catch(() => setInfo(null))
      const root = await backend.fsList("/")
      if (!root.ok) throw new Error(root.error ?? "list failed")
      const next: Record<string, FsEntry[]> = { "/": root.entries }
      // Three folders deep and no deeper, so this is three more calls, not a walk.
      for (const e of root.entries.filter((x) => x.dir)) {
        try {
          const sub = await backend.fsList(`/${e.name}`)
          if (sub.ok) next[`/${e.name}`] = sub.entries
        } catch {
          /* a folder that will not list shows as empty */
        }
      }
      setTree(next)
    } catch (e) {
      toast.error("Failed to list the filesystem", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }, [])

  useEffect(() => {
    if (connection !== "connected") return
    refresh()
  }, [connection, refresh])

  // Folder sizes are the sum of their children: the device reports 0 for a
  // directory, which is true of the entry and useless in a size column.
  const folderSize = useCallback(
    (dir: string) => (tree[dir] ?? []).reduce((n, e) => n + (e.dir ? 0 : e.size), 0),
    [tree],
  )

  const occupied = useMemo(
    () => Object.keys(tree).reduce((n, d) => n + (d === "/" ? 0 : folderSize(d)), 0) +
          (tree["/"] ?? []).filter((e) => !e.dir).reduce((n, e) => n + e.size, 0),
    [tree, folderSize],
  )

  const rows: Row[] = useMemo(() => {
    const q = filter.trim().toLowerCase()
    const cmp = (a: { name: string; size: number }, b: { name: string; size: number }) => {
      const r = sortBy === "name" ? a.name.localeCompare(b.name) : a.size - b.size
      return desc ? -r : r
    }
    const out: Row[] = []
    const top = (tree["/"] ?? []).map((e) => ({
      ...e,
      size: e.dir ? folderSize(`/${e.name}`) : e.size,
    }))
    // Folders first: /labels is hard to find under twenty loose files otherwise.
    for (const e of [...top].sort((a, b) => (a.dir === b.dir ? cmp(a, b) : a.dir ? -1 : 1))) {
      const path = `/${e.name}`
      const children = (tree[path] ?? []).filter((c) => !q || c.name.toLowerCase().includes(q))
      // A filter that matches nothing inside a folder hides the folder too,
      // unless the folder's own name matched.
      if (q && !e.dir && !e.name.toLowerCase().includes(q)) continue
      if (q && e.dir && children.length === 0 && !e.name.toLowerCase().includes(q)) continue
      out.push({ path, name: e.name, dir: e.dir, size: e.size, depth: 0 })
      if (e.dir && (open[path] || q)) {
        for (const c of [...children].sort(cmp))
          out.push({ path: join(path, c.name), name: c.name, dir: false, size: c.size, depth: 1 })
      }
    }
    return out
  }, [tree, open, filter, sortBy, desc, folderSize])

  const largest = useMemo(
    () =>
      rows
        .filter((r) => !r.dir)
        .concat(
          Object.entries(tree)
            .filter(([d]) => d !== "/")
            .flatMap(([d, es]) =>
              es.filter((e) => !e.dir).map((e) => ({
                path: join(d, e.name), name: e.name, dir: false, size: e.size, depth: 1,
              })),
            ),
        )
        .filter((r, i, a) => a.findIndex((x) => x.path === r.path) === i)
        .sort((a, b) => b.size - a.size)
        .slice(0, 3),
    [rows, tree],
  )

  const selectedRow = useMemo(() => {
    for (const [dir, entries] of Object.entries(tree))
      for (const e of entries)
        if (join(dir, e.name) === selected && !e.dir)
          return { path: selected, name: e.name, size: e.size }
    return null
  }, [tree, selected])

  async function openFile(path: string) {
    setBusy(true)
    try {
      const { bytes } = await backend.fsRead(path)
      const decoded = new TextDecoder().decode(bytes)
      setEditing({ path, text: decoded, original: decoded })
    } catch (e) {
      toast.error("Read failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function download(path: string, name: string) {
    setBusy(true)
    try {
      const { bytes } = await backend.fsRead(path)
      // Copy into a fresh buffer: `bytes` is a subarray of the reply, and Blob
      // would otherwise keep the whole reply alive behind it.
      const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)]))
      const a = document.createElement("a")
      a.href = url
      a.download = name
      a.click()
      URL.revokeObjectURL(url)
    } catch (e) {
      toast.error("Download failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function remove(path: string) {
    if (!confirm(`Delete ${path}?`)) return
    setBusy(true)
    try {
      await backend.fsDelete(path)
      if (selected === path) setSelected(null)
      if (editing?.path === path) setEditing(null)
      toast.success(`Deleted ${path}`)
      await refresh()
    } catch (e) {
      toast.error("Delete failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function save() {
    if (!editing) return
    setBusy(true)
    try {
      const written = await backend.fsWrite(editing.path, new Blob([editing.text]))
      setEditing({ ...editing, original: editing.text })
      toast.success(`Saved ${editing.path}`, { description: `${written} bytes` })
      await refresh()
    } catch (e) {
      toast.error("Save failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function upload(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0]
    e.target.value = ""
    if (!file) return
    // Route by what it IS, so a font does not land in /labels. A .ttf needs a
    // reboot before the renderer knows about it, which is worth saying at the
    // moment it is uploaded rather than when text goes missing.
    const font = /\.(ttf|otf)$/i.test(file.name)
    const dir = font ? "/fonts" : file.name.toLowerCase().endsWith(".svg") ? "/labels" : "/"
    setBusy(true)
    try {
      const written = await backend.fsWrite(join(dir, file.name), file)
      toast.success(`Uploaded to ${dir}`, {
        description: font
          ? `${written} bytes - reboot before the renderer will register it`
          : `${written} bytes`,
      })
      await refresh()
    } catch (err) {
      toast.error("Upload failed", { description: errorMessage(err) })
    } finally {
      setBusy(false)
    }
  }

  // Create an empty text file and open it in the editor. Upload covers bringing
  // a file FROM somewhere; this covers the case where the thing you want does
  // not exist yet anywhere - a medium's /media/<id>.json, most obviously, which
  // is how a new stock size gets defined now that it is an ordinary file.
  async function createFile() {
    const name = window.prompt(
      "New file, with its folder - e.g. /media/roll54x70.json",
      "/media/new.json",
    )
    if (!name) return
    const path = name.startsWith("/") ? name : `/${name}`
    if (rows.some((r) => r.path === path)) {
      toast.error("That file already exists", { description: path })
      return
    }
    setBusy(true)
    try {
      await backend.fsWrite(path, new Blob([""]))
      toast.success(`Created ${path}`)
      await refresh()
      await openFile(path)
    } catch (err) {
      toast.error("Create failed", { description: errorMessage(err) })
    } finally {
      setBusy(false)
    }
  }

  const dirty = editing !== null && editing.text !== editing.original
  const total = info?.total ?? 0
  const used = info?.used ?? 0

  return (
    <div className="mx-auto max-w-[1400px] space-y-6">
      <div className="flex flex-wrap items-center justify-between gap-4">
        <h1 className="text-2xl font-bold">Files</h1>

        <div className="flex flex-1 items-center justify-end gap-4">
          {info && (
            <div className="min-w-[16rem] flex-1 max-w-md">
              <div className="flex justify-between gap-4 text-sm">
                <span><b>{fmtSize(used)}</b> <span className="text-muted-foreground">used</span></span>
                <span><b>{fmtSize(info.free ?? 0)}</b> <span className="text-muted-foreground">free</span></span>
                <span><b>{fmtSize(total)}</b> <span className="text-muted-foreground">total</span></span>
              </div>
              <div className="mt-1 h-2 w-full overflow-hidden rounded-full bg-muted">
                <div
                  className="h-full rounded-full bg-emerald-500"
                  style={{ width: `${total ? Math.min(100, (used / total) * 100) : 0}%` }}
                />
              </div>
            </div>
          )}
          <Button variant="outline" size="icon" onClick={refresh} disabled={busy} title="Reload">
            <RefreshCwIcon className={"size-4 " + (busy ? "animate-spin" : "")} />
          </Button>
          <Button variant="outline" onClick={createFile} disabled={busy}>
            <FilePlusIcon className="size-4" />
            New file
          </Button>
          <Button asChild>
            <label>
              <UploadIcon className="size-4" />
              Upload
              <input type="file" className="hidden" onChange={upload} />
            </label>
          </Button>
        </div>
      </div>

      <div className="grid gap-6 lg:grid-cols-[minmax(0,1fr)_minmax(0,20rem)]">
        {/* ── The tree ── */}
        <section className="rounded-xl border bg-card shadow-sm">
          <div className="flex flex-wrap items-center gap-3 border-b p-3">
            <div className="relative min-w-[12rem] flex-1">
              <SearchIcon className="pointer-events-none absolute left-3 top-1/2 size-4 -translate-y-1/2 text-muted-foreground" />
              <Input
                className="pl-9"
                placeholder="Search files..."
                value={filter}
                onChange={(e) => setFilter(e.target.value)}
              />
            </div>
            <div className="flex items-center gap-2">
              <span className="text-sm text-muted-foreground">Sort</span>
              <select
                className="h-9 rounded-md border bg-transparent px-2 text-sm"
                value={sortBy}
                onChange={(e) => setSortBy(e.target.value as "name" | "size")}
              >
                <option value="size">Size</option>
                <option value="name">Name</option>
              </select>
              <Button variant="outline" size="sm" onClick={() => setDesc((d) => !d)}>
                {sortBy === "size"
                  ? desc ? "Largest first" : "Smallest first"
                  : desc ? "Z to A" : "A to Z"}
              </Button>
            </div>
          </div>

          <table className="w-full text-sm">
            <thead>
              <tr className="border-b text-left text-muted-foreground">
                <th className="p-3 font-medium">Name</th>
                <th className="w-24 p-3 font-medium">Type</th>
                <th className="w-24 p-3 text-right font-medium">Size</th>
                <th className="w-48 p-3 font-medium">% used</th>
              </tr>
            </thead>
            <tbody>
              {rows.length === 0 && (
                <tr>
                  <td colSpan={4} className="p-8 text-center text-muted-foreground">
                    {busy ? "Loading..." : "Nothing here."}
                  </td>
                </tr>
              )}
              {rows.map((r) => {
                const K = r.dir ? FolderIcon : kindOf(r.name).icon
                const pct = occupied ? (r.size / occupied) * 100 : 0
                const active = selected === r.path
                return (
                  <tr
                    key={r.path}
                    className={
                      "cursor-pointer border-b last:border-0 " +
                      (active ? "bg-accent" : "hover:bg-accent/40")
                    }
                    onClick={() => {
                      if (r.dir) setOpen((o) => ({ ...o, [r.path]: !o[r.path] }))
                      else setSelected(r.path)
                    }}
                  >
                    <td className="p-3">
                      <span
                        className="flex items-center gap-2"
                        style={{ paddingLeft: `${r.depth * 1.5}rem` }}
                      >
                        {r.dir ? (
                          open[r.path] || filter ? (
                            <ChevronDownIcon className="size-4 shrink-0 text-muted-foreground" />
                          ) : (
                            <ChevronRightIcon className="size-4 shrink-0 text-muted-foreground" />
                          )
                        ) : (
                          <span className="size-4 shrink-0" />
                        )}
                        <K className="size-4 shrink-0 text-muted-foreground" />
                        <span className={"truncate " + (r.dir ? "font-medium" : "")}>{r.name}</span>
                      </span>
                    </td>
                    <td className="p-3 text-muted-foreground">
                      {r.dir ? "Folder" : kindOf(r.name).label}
                    </td>
                    <td className="p-3 text-right tabular-nums">{fmtSize(r.size)}</td>
                    <td className="p-3">
                      <span className="flex items-center gap-2">
                        <span className="w-12 shrink-0 text-right text-xs tabular-nums text-muted-foreground">
                          {pct >= 0.1 ? `${pct.toFixed(1)}%` : "<0.1%"}
                        </span>
                        <span className="h-1.5 min-w-0 flex-1 overflow-hidden rounded-full bg-muted">
                          <span
                            className="block h-full rounded-full bg-emerald-500"
                            style={{ width: `${Math.max(1, Math.min(100, pct))}%` }}
                          />
                        </span>
                      </span>
                    </td>
                  </tr>
                )
              })}
            </tbody>
          </table>
        </section>

        {/* ── Insights and the selected file ── */}
        <aside className="space-y-4 self-start">
          <section className="rounded-xl border bg-card p-4 shadow-sm">
            <h2 className="mb-3 font-semibold">Storage insights</h2>
            <div className="mb-2 text-sm text-muted-foreground">Largest items</div>
            <ol className="space-y-1 text-sm">
              {largest.map((r, i) => (
                <li key={r.path} className="flex items-center justify-between gap-2">
                  <span className="min-w-0 truncate">
                    <span className="mr-2 text-muted-foreground tabular-nums">{i + 1}.</span>
                    {r.name}
                  </span>
                  <span className="shrink-0 tabular-nums text-muted-foreground">
                    {fmtSize(r.size)}
                  </span>
                </li>
              ))}
              {largest.length === 0 && (
                <li className="text-muted-foreground">Nothing stored yet.</li>
              )}
            </ol>
            {occupied > 0 && folderSize("/fonts") > 0 && (
              <p className="mt-3 text-xs text-muted-foreground">
                Fonts use {Math.round((folderSize("/fonts") / occupied) * 100)}% of occupied
                space. They are read into PSRAM at boot, so they cost RAM as well as flash.
              </p>
            )}
          </section>

          <section className="rounded-xl border bg-card p-4 shadow-sm">
            <h2 className="mb-3 font-semibold">Selected file</h2>
            {!selectedRow ? (
              <p className="text-sm text-muted-foreground">
                Pick a file in the tree to see it here.
              </p>
            ) : (
              <>
                <div className="mb-3 flex items-center gap-2">
                  {(() => {
                    const K = kindOf(selectedRow.name).icon
                    return <K className="size-5 shrink-0 text-muted-foreground" />
                  })()}
                  <span className="truncate font-medium">{selectedRow.name}</span>
                </div>
                <dl className="mb-4 space-y-1 text-sm">
                  <Row2 k="Type" v={kindOf(selectedRow.name).label} />
                  <Row2 k="Size" v={fmtSize(selectedRow.size)} />
                  <Row2 k="Path" v={selectedRow.path} mono />
                </dl>
                <div className="space-y-2">
                  {isText(selectedRow.name) && (
                    <Button
                      variant="outline"
                      className="w-full"
                      disabled={busy}
                      onClick={() => openFile(selectedRow.path)}
                    >
                      <FileTextIcon className="size-4" />
                      Edit
                    </Button>
                  )}
                  <Button
                    variant="outline"
                    className="w-full"
                    disabled={busy}
                    onClick={() => download(selectedRow.path, selectedRow.name)}
                  >
                    <DownloadIcon className="size-4" />
                    Download
                  </Button>
                  <Button
                    variant="outline"
                    className="w-full border-destructive/50 text-destructive hover:bg-destructive/10 hover:text-destructive"
                    disabled={busy}
                    onClick={() => remove(selectedRow.path)}
                  >
                    <Trash2Icon className="size-4" />
                    Delete
                  </Button>
                  <p className="text-center text-xs text-muted-foreground">
                    Frees {fmtSize(selectedRow.size)}
                  </p>
                </div>
              </>
            )}
          </section>
        </aside>
      </div>

      {/* ── The editor, when a text file is open ── */}
      {editing && (
        <section className="rounded-xl border bg-card shadow-sm">
          <div className="flex flex-wrap items-center gap-2 border-b p-3">
            <FileTextIcon className="size-4 shrink-0 text-muted-foreground" />
            <span className="min-w-0 flex-1 truncate font-mono text-sm">{editing.path}</span>
            {dirty && <span className="text-xs text-amber-600">unsaved</span>}
            <Button size="sm" onClick={save} disabled={!dirty || busy}>
              <SaveIcon className="size-4" />
              Save
            </Button>
            <Button variant="ghost" size="icon" onClick={() => setEditing(null)} title="Close">
              <XIcon className="size-4" />
            </Button>
          </div>
          <textarea
            className="h-96 w-full resize-y bg-transparent p-3 font-mono text-xs outline-none"
            spellCheck={false}
            value={editing.text}
            onChange={(e) => setEditing({ ...editing, text: e.target.value })}
          />
          <p className="border-t p-3 text-xs text-muted-foreground">
            Saving writes the whole file back with <code>fs write</code>. An SVG saved here is
            live immediately - the renderer reads it per request. A font is not: /fonts is read
            once, at boot.
          </p>
        </section>
      )}

      <p className="text-xs text-muted-foreground">
        <PlusIcon className="mr-1 inline size-3" />
        Uploads are routed by what the file is: <code>.svg</code> to /labels,{" "}
        <code>.ttf</code> and <code>.otf</code> to /fonts. Media definitions are ordinary
        JSON in /media - edit one here, or make a new stock size with New file.
      </p>
    </div>
  )
}

function Row2({ k, v, mono }: { k: string; v: string; mono?: boolean }) {
  return (
    <div className="flex justify-between gap-3">
      <dt className="shrink-0 text-muted-foreground">{k}</dt>
      <dd className={"min-w-0 truncate text-right " + (mono ? "font-mono text-xs" : "")}>{v}</dd>
    </div>
  )
}
