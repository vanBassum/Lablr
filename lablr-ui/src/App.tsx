import { useEffect, useState } from "react"
import { Loader2, Moon, Settings, Sun, Tag, Usb } from "lucide-react"
import { useTheme } from "@/components/theme-provider"
import { usePrinter } from "@/printer"
import { LabelApp } from "@/components/LabelApp"
import { LabelStocksPage } from "@/components/LabelStocksPage"
import { useAppReady } from "@/services/bootstrap"
import { Button } from "@/components/ui/button"

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

function PrinterChip() {
  const { status, connect, disconnect } = usePrinter()
  const connected = status === "connected" || status === "printing"
  const label =
    status === "connecting" ? "Connecting…" : connected ? "Printer" : "Connect"
  return (
    <Button
      variant="ghost"
      size="sm"
      aria-label={connected ? "Disconnect printer" : "Connect printer"}
      onClick={() => (connected ? disconnect() : connect().catch(() => {}))}
    >
      <Usb className={connected ? "text-green-600" : "text-muted-foreground"} />
      {label}
    </Button>
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
          <PrinterChip />
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
      ) : (
        <LabelApp />
      )}
    </div>
  )
}

export default App
