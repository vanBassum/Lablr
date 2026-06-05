export interface Draft {
  id: string
  fields: Record<string, string>
}

export interface Template {
  id: string
  mediaId: string
  printerId: string
  orientation: "portrait" | "landscape"
  fields: Record<string, { required: boolean }>
}

export interface Media {
  id: string
  widthMm: number
  heightMm: number
}

export interface Printer {
  id: string
  name: string
  dpi: number
}
