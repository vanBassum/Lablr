import { Moon, Sun, Tag } from "lucide-react"
import { useTheme } from "@/components/theme-provider"
import { PrintScreen } from "@/components/PrintScreen"
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

export function App() {
  return (
    <div className="mx-auto flex min-h-svh max-w-md flex-col">
      <header className="bg-background/80 sticky top-0 z-10 flex items-center justify-between border-b px-4 py-3 backdrop-blur">
        <span className="flex items-center gap-2 font-semibold">
          <Tag className="size-4" />
          Lablr
        </span>
        <ThemeToggle />
      </header>
      <PrintScreen />
    </div>
  )
}

export default App
