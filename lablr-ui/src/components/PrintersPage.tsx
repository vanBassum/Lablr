import { useEffect, useState } from "react"
import {
  Check,
  ChevronLeft,
  Copy,
  Loader2,
  Pencil,
  Plus,
  Printer as PrinterIcon,
  Trash2,
  Usb,
} from "lucide-react"
import {
  createAgent,
  deleteAgent,
  listAgents,
  renameAgent,
  type PrintAgent,
  type PrintAgentCreated,
} from "@/services/agents"
import { Button } from "@/components/ui/button"
import { Label } from "@/components/ui/label"

// The wss URL a bridge should dial — derived from where the PWA is served.
const wsUrl = () =>
  `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/agent/ws`

export function PrintersPage() {
  const [agents, setAgents] = useState<PrintAgent[] | null>(null)
  const [error, setError] = useState<string | null>(null)
  const [adding, setAdding] = useState(false)
  const [newName, setNewName] = useState("")
  const [created, setCreated] = useState<PrintAgentCreated | null>(null)
  const [busy, setBusy] = useState(false)

  function load() {
    listAgents()
      .then((a) => {
        setAgents(a)
        setError(null)
      })
      .catch((e) => setError((e as Error).message))
  }
  useEffect(() => {
    load()
    const t = setInterval(load, 5000)
    return () => clearInterval(t)
  }, [])

  const goHome = () => (window.location.hash = "")

  async function add() {
    if (!newName.trim()) return
    setBusy(true)
    try {
      const result = await createAgent(newName.trim())
      setCreated(result)
      setAdding(false)
      setNewName("")
      load()
    } catch (e) {
      setError((e as Error).message)
    } finally {
      setBusy(false)
    }
  }

  return (
    <main className="flex flex-1 flex-col gap-3 p-4">
      <div className="flex items-center gap-1">
        <Button variant="ghost" size="icon" aria-label="Back" onClick={goHome}>
          <ChevronLeft />
        </Button>
        <span className="font-medium">Printers</span>
        {!adding && (
          <Button size="sm" className="ml-auto" onClick={() => setAdding(true)}>
            <Plus className="size-4" /> Add bridge
          </Button>
        )}
      </div>

      {error && <p className="text-destructive text-sm">{error}</p>}

      {/* New-printer name form */}
      {adding && (
        <div className="bg-card flex flex-col gap-2 rounded-lg border p-3">
          <Label className="text-muted-foreground text-xs">New bridge printer name</Label>
          <input
            autoFocus
            type="text"
            value={newName}
            placeholder="e.g. Workshop Dymo"
            onChange={(e) => setNewName(e.target.value)}
            onKeyDown={(e) => e.key === "Enter" && add()}
            className="border-input bg-background h-9 w-full rounded-md border px-3 text-sm"
          />
          <div className="flex gap-2">
            <Button size="sm" onClick={add} disabled={busy || !newName.trim()}>
              {busy ? <Loader2 className="size-4 animate-spin" /> : "Create token"}
            </Button>
            <Button size="sm" variant="outline" onClick={() => setAdding(false)}>
              Cancel
            </Button>
          </div>
        </div>
      )}

      {/* Token + setup instructions, shown once after create */}
      {created && <TokenPanel created={created} onDone={() => setCreated(null)} />}

      {/* USB (local) — always available on this device when WebUSB is supported */}
      <UsbCard />

      {/* Registered bridges */}
      {agents === null ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading…
        </div>
      ) : agents.length === 0 ? (
        <p className="text-muted-foreground text-sm">
          No bridges yet. “Add bridge” to generate a token, then enter it on the device.
        </p>
      ) : (
        <ul className="flex flex-col gap-2">
          {agents.map((a) => (
            <AgentRow key={a.id} agent={a} onChanged={load} onError={setError} />
          ))}
        </ul>
      )}
    </main>
  )
}

function TokenPanel({
  created,
  onDone,
}: {
  created: PrintAgentCreated
  onDone: () => void
}) {
  return (
    <div className="flex flex-col gap-2 rounded-lg border border-green-600/40 bg-green-600/5 p-3">
      <div className="text-sm font-medium">“{created.name}” created</div>
      <p className="text-muted-foreground text-xs">
        On the bridge’s Settings page, set these — the token is shown only now:
      </p>
      <CopyRow label="cloud.url" value={wsUrl()} />
      <CopyRow label="cloud.token" value={created.token} />
      <p className="text-muted-foreground text-xs">…and set cloud.enabled = on. It’ll appear below.</p>
      <Button size="sm" variant="outline" className="w-fit" onClick={onDone}>
        Done
      </Button>
    </div>
  )
}

function CopyRow({ label, value }: { label: string; value: string }) {
  const [copied, setCopied] = useState(false)
  return (
    <div className="flex items-center gap-2">
      <span className="text-muted-foreground w-20 shrink-0 text-xs">{label}</span>
      <code className="bg-background min-w-0 flex-1 truncate rounded border px-2 py-1 text-xs">
        {value}
      </code>
      <Button
        variant="ghost"
        size="icon"
        className="size-7"
        aria-label={`Copy ${label}`}
        onClick={() => {
          navigator.clipboard?.writeText(value)
          setCopied(true)
          setTimeout(() => setCopied(false), 1200)
        }}
      >
        {copied ? <Check className="size-4 text-green-600" /> : <Copy className="size-4" />}
      </Button>
    </div>
  )
}

function UsbCard() {
  const supported = typeof navigator !== "undefined" && "usb" in navigator
  if (!supported) return null
  return (
    <div className="bg-card flex items-center gap-3 rounded-lg border p-3">
      <Usb className="text-muted-foreground" />
      <div className="min-w-0 flex-1">
        <div className="text-sm font-medium">USB (this device)</div>
        <div className="text-muted-foreground truncate text-xs">
          Direct WebUSB — pick it in the top-bar printer menu when this device has the Dymo plugged in.
        </div>
      </div>
    </div>
  )
}

function AgentRow({
  agent,
  onChanged,
  onError,
}: {
  agent: PrintAgent
  onChanged: () => void
  onError: (m: string) => void
}) {
  const online = agent.online
  const ready = agent.status === "ready"
  const badge = !online ? "Offline" : ready ? "Ready" : "No printer"
  const badgeClass = !online
    ? "bg-muted text-muted-foreground"
    : ready
      ? "bg-green-600/15 text-green-600"
      : "bg-amber-500/15 text-amber-600"

  return (
    <li className="bg-card flex items-center gap-3 rounded-lg border p-3">
      <PrinterIcon className={online ? "text-green-600" : "text-muted-foreground"} />
      <div className="min-w-0 flex-1">
        <div className="truncate text-sm font-medium">{agent.name}</div>
        <div className="text-muted-foreground truncate text-xs">
          {agent.deviceId ? agent.deviceId : agent.id}
        </div>
      </div>
      <span className={"rounded-full px-2 py-0.5 text-xs " + badgeClass}>{badge}</span>
      <Button
        variant="ghost"
        size="icon"
        className="size-8"
        aria-label={`Rename ${agent.name}`}
        onClick={async () => {
          const name = window.prompt("Rename printer", agent.name)
          if (!name || name === agent.name) return
          try {
            await renameAgent(agent.id, name)
            onChanged()
          } catch (e) {
            onError((e as Error).message)
          }
        }}
      >
        <Pencil className="size-4" />
      </Button>
      <Button
        variant="ghost"
        size="icon"
        className="size-8"
        aria-label={`Delete ${agent.name}`}
        onClick={async () => {
          if (!window.confirm(`Delete printer "${agent.name}"? Its token stops working.`)) return
          try {
            await deleteAgent(agent.id)
            onChanged()
          } catch (e) {
            onError((e as Error).message)
          }
        }}
      >
        <Trash2 className="text-destructive size-4" />
      </Button>
    </li>
  )
}
