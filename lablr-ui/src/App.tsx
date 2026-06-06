import { useEffect, useState } from "react"
import { LayoutTemplate, Loader2, Moon, Printer, Settings, Sun, Tag } from "lucide-react"
import { DropdownMenu } from "radix-ui"
import { useTheme } from "@/components/theme-provider"
import { usePrintTarget } from "@/printTarget"
import { LabelApp } from "@/components/LabelApp"
import { LabelStocksPage } from "@/components/LabelStocksPage"
import { PrintersPage } from "@/components/PrintersPage"
import { TemplatesPage } from "@/components/TemplatesPage"
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

// Settings gear → menu: Label stocks, Printers, and the dark-mode toggle.
function SettingsMenu() {
  const { theme, setTheme } = useTheme()
  const dark = theme === "dark"
  const item =
    "flex items-center gap-2 rounded-sm px-2 py-1.5 text-sm outline-none cursor-default select-none data-[highlighted]:bg-accent"
  return (
    <DropdownMenu.Root>
      <DropdownMenu.Trigger asChild>
        <Button variant="ghost" size="icon" aria-label="Settings">
          <Settings />
        </Button>
      </DropdownMenu.Trigger>
      <DropdownMenu.Portal>
        <DropdownMenu.Content
          align="end"
          sideOffset={4}
          className="bg-popover text-popover-foreground z-50 min-w-44 rounded-md border p-1 shadow-md"
        >
          <DropdownMenu.Item className={item} onSelect={() => (window.location.hash = "#/labels")}>
            <Tag className="size-4" /> Label stocks
          </DropdownMenu.Item>
          <DropdownMenu.Item className={item} onSelect={() => (window.location.hash = "#/templates")}>
            <LayoutTemplate className="size-4" /> Templates
          </DropdownMenu.Item>
          <DropdownMenu.Item className={item} onSelect={() => (window.location.hash = "#/printers")}>
            <Printer className="size-4" /> Printers
          </DropdownMenu.Item>
          <DropdownMenu.Separator className="bg-border my-1 h-px" />
          <DropdownMenu.Item
            className={item}
            onSelect={(e) => {
              e.preventDefault() // keep the menu open while toggling
              setTheme(dark ? "light" : "dark")
            }}
          >
            {dark ? <Sun className="size-4" /> : <Moon className="size-4" />}
            {dark ? "Light mode" : "Dark mode"}
          </DropdownMenu.Item>
        </DropdownMenu.Content>
      </DropdownMenu.Portal>
    </DropdownMenu.Root>
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
  const onTemplatesPage = hash.startsWith("#/templates")
  const onPrintersPage = hash.startsWith("#/printers")
  return (
    <div className="mx-auto flex min-h-svh max-w-md flex-col">
      <header className="bg-background/80 sticky top-0 z-10 flex items-center justify-between border-b px-4 py-3 backdrop-blur">
        <div className="flex items-center gap-2">
          <button
            type="button"
            aria-label="Lablr — home"
            onClick={() => (window.location.hash = "")}
            className="focus-visible:ring-ring -m-1 flex items-center rounded-md p-1 hover:opacity-80 focus-visible:ring-2 focus-visible:outline-none"
          >
            <img src="/logo-light.png" alt="Lablr" className="h-6 w-auto dark:hidden" />
            <img src="/logo-dark.png" alt="Lablr" className="hidden h-6 w-auto dark:block" />
          </button>
          <span className="text-muted-foreground text-[10px] font-normal" title="build">
            {VERSION}
          </span>
        </div>
        <div className="flex items-center gap-1">
          <PrinterSelect />
          <SettingsMenu />
        </div>
      </header>
      {!ready ? (
        <div className="text-muted-foreground flex flex-1 items-center justify-center gap-2 text-sm">
          <Loader2 className="size-4 animate-spin" />
          Loading…
        </div>
      ) : onLabelsPage ? (
        <LabelStocksPage />
      ) : onTemplatesPage ? (
        <TemplatesPage />
      ) : onPrintersPage ? (
        <PrintersPage />
      ) : (
        <LabelApp />
      )}
    </div>
  )
}

export default App
