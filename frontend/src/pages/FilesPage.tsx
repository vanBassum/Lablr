// A bench file manager for the label filesystem.
//
// Deliberately small. It is a client of the `fs` commands and nothing else -
// there is no HTTP filesystem route behind this page, so using it exercises the
// same interface an external caller (or the relay's MCP surface) will drive. If
// something works here it works there.
import { useEffect, useState } from "react"
import {
  backend,
  type FsEntry,
  type FsInfo,
} from "@/lib/backend"
import { useConnectionStatus } from "@/hooks/use-connection-status"
import {
  ArrowUpIcon,
  DownloadIcon,
  FileIcon,
  FolderIcon,
  RefreshCwIcon,
  SaveIcon,
  Trash2Icon,
  UploadIcon,
  XIcon,
} from "lucide-react"
import { Button } from "@/components/ui/button"
import { Badge } from "@/components/ui/badge"
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

function parentOf(dir: string): string {
  const cut = dir.lastIndexOf("/")
  return cut <= 0 ? "/" : dir.slice(0, cut)
}

export default function FilesPage() {
  const connection = useConnectionStatus()
  const [dir, setDir] = useState("/")
  const [entries, setEntries] = useState<FsEntry[] | null>(null)
  const [info, setInfo] = useState<FsInfo | null>(null)
  const [busy, setBusy] = useState(false)

  // The open file, if any. `text` is the editable buffer; `original` is what was
  // read, so Save can be offered only when it would change something.
  const [openPath, setOpenPath] = useState<string | null>(null)
  const [text, setText] = useState("")
  const [original, setOriginal] = useState("")

  function refresh(target = dir) {
    backend.fsInfo().then(setInfo).catch(() => setInfo(null))
    backend
      .fsList(target)
      .then((r) => {
        if (!r.ok) throw new Error(r.error ?? "list failed")
        // Directories first, then by name: a flat readdir order makes /labels
        // hard to find once there are twenty files in it.
        const sorted = [...r.entries].sort((a, b) =>
          a.dir === b.dir ? a.name.localeCompare(b.name) : a.dir ? -1 : 1,
        )
        setEntries(sorted)
        setDir(target)
      })
      .catch((e) => {
        setEntries(null)
        toast.error("Failed to list directory", { description: errorMessage(e) })
      })
  }

  useEffect(() => {
    if (connection !== "connected") return
    refresh("/")
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [connection])

  async function open(entry: FsEntry) {
    const path = join(dir, entry.name)
    setBusy(true)
    try {
      const { bytes } = await backend.fsRead(path)
      const decoded = new TextDecoder().decode(bytes)
      setOpenPath(path)
      setText(decoded)
      setOriginal(decoded)
    } catch (e) {
      toast.error("Read failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function download(entry: FsEntry) {
    setBusy(true)
    try {
      const { bytes } = await backend.fsRead(join(dir, entry.name))
      // Copy into a fresh buffer: `bytes` is a subarray of the reply, and Blob
      // would otherwise keep the whole reply alive behind it.
      const url = URL.createObjectURL(new Blob([new Uint8Array(bytes)]))
      const a = document.createElement("a")
      a.href = url
      a.download = entry.name
      a.click()
      URL.revokeObjectURL(url)
    } catch (e) {
      toast.error("Download failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function save() {
    if (!openPath) return
    setBusy(true)
    try {
      const written = await backend.fsWrite(openPath, new Blob([text]))
      setOriginal(text)
      toast.success(`Saved ${openPath}`, { description: `${written} bytes` })
      refresh()
    } catch (e) {
      toast.error("Save failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function remove(entry: FsEntry) {
    const path = join(dir, entry.name)
    if (!confirm(`Delete ${path}?`)) return
    setBusy(true)
    try {
      await backend.fsDelete(path)
      if (openPath === path) setOpenPath(null)
      toast.success(`Deleted ${path}`)
      refresh()
    } catch (e) {
      toast.error("Delete failed", { description: errorMessage(e) })
    } finally {
      setBusy(false)
    }
  }

  async function upload(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0]
    e.target.value = ""
    if (!file) return
    const path = join(dir, file.name)
    setBusy(true)
    try {
      const written = await backend.fsWrite(path, file)
      toast.success(`Uploaded ${file.name}`, { description: `${written} bytes` })
      refresh()
    } catch (err) {
      toast.error("Upload failed", { description: errorMessage(err) })
    } finally {
      setBusy(false)
    }
  }

  const dirty = openPath !== null && text !== original

  return (
    <div className="mx-auto max-w-3xl space-y-6">
      <h1 className="text-2xl font-bold">Files</h1>

      {info && (
        <div className="grid grid-cols-3 gap-3 rounded-xl border bg-card p-4 text-sm shadow-sm">
          <Field label="Mounted" value={info.mounted ? "yes" : "no"} />
          <Field label="Used" value={info.used != null ? fmtSize(info.used) : "-"} />
          <Field label="Free" value={info.free != null ? fmtSize(info.free) : "-"} />
        </div>
      )}

      <div className="rounded-xl border bg-card shadow-sm">
        <div className="flex items-center gap-2 border-b p-3">
          <Button
            variant="outline"
            size="icon"
            disabled={dir === "/" || busy}
            onClick={() => refresh(parentOf(dir))}
            title="Up one level"
          >
            <ArrowUpIcon className="size-4" />
          </Button>
          <code className="min-w-0 flex-1 truncate text-sm">{dir}</code>
          <Button variant="outline" size="sm" disabled={busy} onClick={() => refresh()}>
            <RefreshCwIcon className="size-4" />
          </Button>
          <Button asChild variant="outline" size="sm" disabled={busy}>
            <label className="cursor-pointer">
              <UploadIcon className="size-4" />
              Upload
              <input type="file" className="hidden" onChange={upload} disabled={busy} />
            </label>
          </Button>
        </div>

        {entries === null ? (
          <p className="p-4 text-sm text-muted-foreground">Nothing to show.</p>
        ) : entries.length === 0 ? (
          <p className="p-4 text-sm text-muted-foreground">Empty directory.</p>
        ) : (
          <ul className="divide-y">
            {entries.map((entry) => (
              <li key={entry.name} className="flex items-center gap-3 p-3">
                {entry.dir ? (
                  <FolderIcon className="size-4 shrink-0 text-muted-foreground" />
                ) : (
                  <FileIcon className="size-4 shrink-0 text-muted-foreground" />
                )}

                {entry.dir ? (
                  <button
                    className="min-w-0 flex-1 truncate text-left text-sm hover:underline"
                    onClick={() => refresh(join(dir, entry.name))}
                  >
                    {entry.name}
                  </button>
                ) : (
                  <span className="min-w-0 flex-1 truncate text-sm">{entry.name}</span>
                )}

                {!entry.dir && (
                  <Badge variant="secondary" className="shrink-0 tabular-nums">
                    {fmtSize(entry.size)}
                  </Badge>
                )}

                {!entry.dir && (
                  <div className="flex shrink-0 gap-1">
                    {isText(entry.name) && (
                      <Button variant="ghost" size="sm" disabled={busy} onClick={() => open(entry)}>
                        Edit
                      </Button>
                    )}
                    <Button
                      variant="ghost"
                      size="icon"
                      disabled={busy}
                      onClick={() => download(entry)}
                      title="Download"
                    >
                      <DownloadIcon className="size-4" />
                    </Button>
                    <Button
                      variant="ghost"
                      size="icon"
                      disabled={busy}
                      onClick={() => remove(entry)}
                      title="Delete"
                    >
                      <Trash2Icon className="size-4" />
                    </Button>
                  </div>
                )}
              </li>
            ))}
          </ul>
        )}
      </div>

      {openPath && (
        <div className="rounded-xl border bg-card shadow-sm">
          <div className="flex items-center gap-2 border-b p-3">
            <code className="min-w-0 flex-1 truncate text-sm">{openPath}</code>
            {dirty && <Badge variant="secondary">unsaved</Badge>}
            <Button size="sm" disabled={!dirty || busy} onClick={save}>
              <SaveIcon className="size-4" />
              Save
            </Button>
            <Button variant="ghost" size="icon" onClick={() => setOpenPath(null)} title="Close">
              <XIcon className="size-4" />
            </Button>
          </div>
          <textarea
            className="h-80 w-full resize-y bg-transparent p-3 font-mono text-xs outline-none"
            spellCheck={false}
            value={text}
            onChange={(e) => setText(e.target.value)}
          />
        </div>
      )}

      <NewFile dir={dir} busy={busy} onCreated={() => refresh()} />
    </div>
  )
}

function Field({ label, value }: { label: string; value: string }) {
  return (
    <div>
      <div className="text-xs text-muted-foreground">{label}</div>
      <div className="truncate font-medium">{value}</div>
    </div>
  )
}

/** Create an empty file in the current directory.
 *
 *  There is no `fs mkdir`: the device creates /labels, /fonts and /media itself,
 *  and `fs write` makes one level of parent on demand. So a new directory is a
 *  side effect of naming a file inside it - "sub/x.svg" here creates "sub". That
 *  is the whole directory story and it is enough for a label filesystem. */
function NewFile({ dir, busy, onCreated }: { dir: string; busy: boolean; onCreated: () => void }) {
  const [name, setName] = useState("")

  async function create() {
    if (!name.trim()) return
    const path = join(dir, name.trim())
    try {
      await backend.fsWrite(path, new Blob([""]))
      setName("")
      toast.success(`Created ${path}`)
      onCreated()
    } catch (e) {
      toast.error("Create failed", { description: errorMessage(e) })
    }
  }

  return (
    <div className="flex items-center gap-2">
      <Input
        placeholder="new-file.svg (or sub/new-file.svg)"
        value={name}
        onChange={(e) => setName(e.target.value)}
        onKeyDown={(e) => { if (e.key === "Enter") create() }}
        disabled={busy}
      />
      <Button variant="outline" disabled={busy || !name.trim()} onClick={create}>
        Create
      </Button>
    </div>
  )
}
