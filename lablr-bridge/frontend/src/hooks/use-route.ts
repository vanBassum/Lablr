import { useState, useEffect, useCallback } from "react"
import type { Page } from "@/components/AppSidebar"

const validPages: Page[] = [
  "home",
  "console",
  "settings",
  "firmware",
]

function pathToPage(pathname: string): Page {
  const segment = pathname.replace(/^\/+/, "").split("/")[0]?.toLowerCase()
  if (segment && validPages.includes(segment as Page)) return segment as Page
  return "home"
}

function pageToPath(page: Page): string {
  return page === "home" ? "/" : `/${page}`
}

export function useRoute() {
  const [page, setPageState] = useState<Page>(() =>
    pathToPage(window.location.pathname),
  )

  const navigate = useCallback((p: Page) => {
    const path = pageToPath(p)
    if (window.location.pathname !== path) {
      window.history.pushState(null, "", path)
    }
    setPageState(p)
  }, [])

  useEffect(() => {
    const onPopState = () => setPageState(pathToPage(window.location.pathname))
    window.addEventListener("popstate", onPopState)
    return () => window.removeEventListener("popstate", onPopState)
  }, [])

  return { page, navigate }
}
