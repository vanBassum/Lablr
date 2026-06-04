import { StrictMode } from "react"
import { createRoot } from "react-dom/client"

import "./index.css"
import App from "./App.tsx"
import { ThemeProvider } from "@/components/theme-provider.tsx"
import { PrinterProvider } from "@/printer.tsx"

createRoot(document.getElementById("root")!).render(
  <StrictMode>
    <ThemeProvider>
      <PrinterProvider>
        <App />
      </PrinterProvider>
    </ThemeProvider>
  </StrictMode>
)
