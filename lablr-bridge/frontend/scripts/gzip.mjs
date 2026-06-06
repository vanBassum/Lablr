import { readdir, readFile, writeFile, unlink } from "fs/promises"
import { join } from "path"
import { gzipSync } from "zlib"

const wwwDir = join(import.meta.dirname, "../../www")

async function gzipDir(dir) {
  const entries = await readdir(dir, { withFileTypes: true })
  for (const entry of entries) {
    const fullPath = join(dir, entry.name)
    if (entry.isDirectory()) {
      await gzipDir(fullPath)
    } else if (!entry.name.endsWith(".gz")) {
      const data = await readFile(fullPath)
      const compressed = gzipSync(data, { level: 9 })
      await writeFile(fullPath + ".gz", compressed)
      await unlink(fullPath)
      const pct = ((1 - compressed.length / data.length) * 100).toFixed(0)
      console.log(`${entry.name} → ${entry.name}.gz (${data.length} → ${compressed.length}, -${pct}%)`)
    }
  }
}

await gzipDir(wwwDir)
