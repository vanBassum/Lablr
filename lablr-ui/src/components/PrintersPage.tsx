import { useEffect, useState } from "react"
import { ChevronLeft, Loader2, Printer as PrinterIcon, RefreshCw } from "lucide-react"
import { listAgents, type PrintAgent } from "@/services/agents"
import { Button } from "@/components/ui/button"

// Lists print-bridge agents currently connected to the backend (Option C).
// Read-only for now — selecting one as the active print target comes with the
// print path. Polls so connect/disconnect shows up without a manual refresh.
export function PrintersPage() {
  const [agents, setAgents] = useState<PrintAgent[] | null>(null)
  const [error, setError] = useState<string | null>(null)

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

  return (
    <main className="flex flex-1 flex-col gap-3 p-4">
      <div className="flex items-center gap-1">
        <Button variant="ghost" size="icon" aria-label="Back" onClick={goHome}>
          <ChevronLeft />
        </Button>
        <span className="font-medium">Printers</span>
        <Button
          variant="ghost"
          size="icon"
          className="ml-auto"
          aria-label="Refresh"
          onClick={load}
        >
          <RefreshCw className="size-4" />
        </Button>
      </div>

      {error && <p className="text-destructive text-sm">{error}</p>}

      {agents === null ? (
        <div className="text-muted-foreground flex items-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" /> Loading…
        </div>
      ) : agents.length === 0 ? (
        <p className="text-muted-foreground text-sm">
          No printers connected. Power on a lablr-bridge with the cloud link enabled.
        </p>
      ) : (
        <ul className="flex flex-col gap-2">
          {agents.map((a) => {
            const ready = a.status === "ready"
            return (
              <li key={a.id} className="bg-card flex items-center gap-3 rounded-lg border p-3">
                <PrinterIcon className={ready ? "text-green-600" : "text-muted-foreground"} />
                <div className="min-w-0 flex-1">
                  <div className="truncate text-sm font-medium">{a.name}</div>
                  <div className="text-muted-foreground truncate text-xs">{a.id}</div>
                </div>
                <span
                  className={
                    "rounded-full px-2 py-0.5 text-xs " +
                    (ready
                      ? "bg-green-600/15 text-green-600"
                      : "bg-muted text-muted-foreground")
                  }
                >
                  {ready ? "Ready" : "No printer"}
                </span>
              </li>
            )
          })}
        </ul>
      )}
    </main>
  )
}
