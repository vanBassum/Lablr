import { Moon, Sun, Tag, Usb } from "lucide-react"
import { useTheme } from "@/components/theme-provider"
import { usePrinter } from "@/printer"
import { LabelApp } from "@/components/LabelApp"
import { Button } from "@/components/ui/button"

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

export function App() {
  return (
    <div className="mx-auto flex min-h-svh max-w-md flex-col">
      <header className="bg-background/80 sticky top-0 z-10 flex items-center justify-between border-b px-4 py-3 backdrop-blur">
        <span className="flex items-center gap-2 font-semibold">
          <Tag className="size-4" />
          Lablr
        </span>
        <div className="flex items-center gap-1">
          <PrinterChip />
          <ThemeToggle />
        </div>
      </header>
      <LabelApp />
    </div>
  )
}

export default App
