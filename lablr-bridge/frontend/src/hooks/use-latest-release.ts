import { useEffect, useState } from "react"

export interface ReleaseInfo {
  version: string
  tag: string
  url: string
}

const REPO = "vanBassum/Strux"

export function useLatestRelease() {
  const [release, setRelease] = useState<ReleaseInfo | null>(null)

  useEffect(() => {
    fetch(`https://api.github.com/repos/${REPO}/releases/latest`)
      .then((r) => (r.ok ? r.json() : null))
      .then((data) => {
        if (!data?.tag_name) return

        setRelease({
          version: data.tag_name.replace(/^v/, ""),
          tag: data.tag_name,
          url: data.html_url,
        })
      })
      .catch(() => {})
  }, [])

  return release
}
