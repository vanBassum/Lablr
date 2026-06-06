import { useEffect, useState } from "react"
import { Loader2, Moon, Printer, Settings, Sun, Tag } from "lucide-react"
import { useTheme } from "@/components/theme-provider"
import { usePrintTarget } from "@/printTarget"
import { LabelApp } from "@/components/LabelApp"
import { LabelStocksPage } from "@/components/LabelStocksPage"
import { PrintersPage } from "@/components/PrintersPage"
import { useAppReady } from "@/services/bootstrap"
import { Button } from "@/components/ui/button"
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from "@/components/ui/select"

// Build identifier — the short commit SHA injected at image build (VITE_COMMIT_SHA),
// or "dev" locally.
const VERSION = import.meta.env.VITE_COMMIT_SHA?.slice(0, 7) || "dev"

function ThemeToggle() {
  const { theme, setTheme } = useTheme()
  return (
    <Button
      variant="ghost"
      size="icon"
      aria-label="Toggle theme"
      onClick={() => setTheme(theme === "dark" ? "light" : "dark")}
    >
      <Sun className="hidden dark:block" />
      <Moon className="dark:hidden" />
    </Button>
  )
}

// Header dropdown to pick the active print target: the local USB printer
// (WebUSB) and/or any bridge connected to the backend (Option C).
function PrinterSelect() {
  const { targets, selectedId, setSelectedId } = usePrintTarget()
  if (targets.length === 0)
    return <span className="text-muted-foreground px-2 text-xs">No printer</span>
  return (
    <Select value={selectedId} onValueChange={setSelectedId}>
      <SelectTrigger size="sm" className="h-8 max-w-40" aria-label="Select printer">
        <Printer className="size-4 shrink-0" />
        <SelectValue placeholder="Printer" />
      </SelectTrigger>
      <SelectContent>
        {targets.map((t) => (
          <SelectItem key={t.id} value={t.id}>
            <span className={t.ready ? "" : "text-muted-foreground"}>
              {t.name}
              {t.kind === "agent" && !t.ready ? " · no printer" : ""}
            </span>
          </SelectItem>
        ))}
      </SelectContent>
    </Select>
  )
}

function useHash(): string {
  const [hash, setHash] = useState(() => window.location.hash)
  useEffect(() => {
    const onChange = () => setHash(window.location.hash)
    window.addEventListener("hashchange", onChange)
    return () => window.removeEventListener("hashchange", onChange)
  }, [])
  return hash
}

export function App() {
  const ready = useAppReady()
  const hash = useHash()
  const onLabelsPage = hash.startsWith("#/labels")
  const onPrintersPage = hash.startsWith("#/printers")
  return (
    <div className="mx-auto flex min-h-svh max-w-md flex-col">
      <header className="bg-background/80 sticky top-0 z-10 flex items-center justify-between border-b px-4 py-3 backdrop-blur">
        <span className="flex items-center gap-2 font-semibold">
          <Tag className="size-4" />
          Lablr
          <span className="text-muted-foreground text-[10px] font-normal" title="build">
            {VERSION}
          </span>
        </span>
        <div className="flex items-center gap-1">
          <PrinterSelect />
          <Button
            variant="ghost"
            size="icon"
            aria-label="Connected printers"
            onClick={() => (window.location.hash = "#/printers")}
          >
            <Printer className={onPrintersPage ? "text-foreground" : "text-muted-foreground"} />
          </Button>
          <Button
            variant="ghost"
            size="icon"
            aria-label="Manage label stocks"
            onClick={() => (window.location.hash = "#/labels")}
          >
            <Settings className={onLabelsPage ? "text-foreground" : "text-muted-foreground"} />
          </Button>
          <ThemeToggle />
        </div>
      </header>
      {!ready ? (
        <div className="text-muted-foreground flex flex-1 items-center justify-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" />
          Loading…
        </div>
      ) : onLabelsPage ? (
        <LabelStocksPage />
      ) : onPrintersPage ? (
        <PrintersPage />
      ) : (
        <LabelApp />
      )}
    </div>
  )
}

export default App
