/**
 * Minimal YAML parser for our config files.
 * Handles basic YAML: key-value pairs, nested objects, arrays, numbers, booleans, strings.
 */

export function parseYaml(content: string): any {
  const lines = content.split("\n")
  const { value } = parseLines(lines, 0, 0)
  return value
}

function parseLines(lines: string[], startIndex: number, baseIndent: number): { value: any; index: number } {
  const result: any = {}
  let i = startIndex

  while (i < lines.length) {
    const line = lines[i]
    const trimmed = line.trim()

    // Skip empty lines and comments
    if (!trimmed || trimmed.startsWith("#")) {
      i++
      continue
    }

    // Calculate indentation
    const indent = getIndent(line)

    // Return if indentation is less than base (we've exited this block)
    if (indent < baseIndent) {
      break
    }

    // Skip lines with greater indentation (will be handled by recursive calls)
    if (indent > baseIndent) {
      i++
      continue
    }

    // Parse key-value
    const colonIndex = trimmed.indexOf(":")
    if (colonIndex === -1) {
      i++
      continue
    }

    const key = trimmed.substring(0, colonIndex).trim()
    const valueStr = trimmed.substring(colonIndex + 1).trim()

    if (!valueStr) {
      // Check if next line is an array
      if (i + 1 < lines.length && lines[i + 1].trim().startsWith("-")) {
        i++
        const arrayResult = parseArray(lines, i, baseIndent + 2)
        result[key] = arrayResult.value
        i = arrayResult.index
      } else {
        // Nested object
        i++
        const nestedResult = parseLines(lines, i, baseIndent + 2)
        result[key] = nestedResult.value
        i = nestedResult.index
      }
    } else if (valueStr.startsWith("-")) {
      // Inline array item (shouldn't happen in our YAML structure)
      i++
    } else {
      // Simple value
      result[key] = parseValue(valueStr)
      i++
    }
  }

  return { value: result, index: i }
}

function parseArray(lines: string[], startIndex: number, baseIndent: number): { value: any[]; index: number } {
  const result: any[] = []
  let i = startIndex

  while (i < lines.length) {
    const line = lines[i]
    const trimmed = line.trim()

    // Skip empty lines and comments
    if (!trimmed || trimmed.startsWith("#")) {
      i++
      continue
    }

    const indent = getIndent(line)

    // Return if indentation is less than base
    if (indent < baseIndent - 2) {
      break
    }

    // Handle array item marker
    if (indent === baseIndent - 2 && trimmed.startsWith("-")) {
      const valueStr = trimmed.substring(1).trim()

      if (!valueStr) {
        // Next line has the object content
        i++
        const nestedResult = parseLines(lines, i, baseIndent)
        result.push(nestedResult.value)
        i = nestedResult.index
      } else {
        // Simple value on same line
        result.push(parseValue(valueStr))
        i++
      }
    } else if (indent > baseIndent - 2) {
      // This shouldn't happen if we're parsing correctly
      i++
    } else {
      // Indentation is less than array level, exit
      break
    }
  }

  return { value: result, index: i }
}

function parseInlineObject(str: string): any {
  // Parse {key: value, key2: value2} style objects
  const content = str.slice(1, -1)
  const obj: any = {}
  const parts = content.split(",")

  for (const part of parts) {
    const [key, value] = part.split(":").map((s) => s.trim())
    obj[key] = parseValue(value)
  }

  return obj
}

function getIndent(line: string): number {
  const match = line.match(/^(\s*)/)
  return match ? match[1].length : 0
}

function parseValue(str: string): any {
  if (!str) return null

  // Numbers
  if (/^-?\d+(\.\d+)?$/.test(str)) {
    return str.includes(".") ? parseFloat(str) : parseInt(str, 10)
  }

  // Booleans
  if (str === "true") return true
  if (str === "false") return false

  // Null
  if (str === "null") return null

  // Strings (remove quotes if present)
  if ((str.startsWith('"') && str.endsWith('"')) || (str.startsWith("'") && str.endsWith("'"))) {
    return str.slice(1, -1)
  }

  return str
}
