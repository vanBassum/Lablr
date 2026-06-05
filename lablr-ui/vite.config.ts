import path from "path"
import tailwindcss from "@tailwindcss/vite"
import react from "@vitejs/plugin-react"
import { defineConfig } from "vite"

// https://vite.dev/config/
export default defineConfig(() => ({
  // Served at root, same-origin, by lablr-api (wwwroot).
  base: "/",
  plugins: [react(), tailwindcss()],
  resolve: {
    alias: {
      "@": path.resolve(__dirname, "./src"),
    },
  },
  server: {
    host: "0.0.0.0",
    // Proxy API + pictograms to the backend so dev is same-origin as prod
    // (no CORS, no cross-origin canvas tainting).
    proxy: {
      "/api": "http://localhost:5110",
      "/pictograms": "http://localhost:5110",
    },
  },
}))
