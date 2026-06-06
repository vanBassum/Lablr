# Headless renderer. Build context is the repo root (so lablr-ui/src is available
# to bundle the shared render code).

# --- Build: bundle server.ts + the shared render code into one file ---
FROM node:20-bookworm-slim AS build
WORKDIR /app/lablr-render
COPY lablr-render/package.json ./
RUN npm install
COPY lablr-ui ../lablr-ui
COPY lablr-render/ ./
RUN npm run build

# --- Runtime: Node + node-canvas's native libs (cairo/pango/jpeg/gif/rsvg) + a font ---
FROM node:20-bookworm-slim AS runtime
WORKDIR /app
RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       libcairo2 libpango-1.0-0 libpangocairo-1.0-0 libjpeg62-turbo libgif7 librsvg2-2 \
       fontconfig fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
# node-canvas prebuilt binary + its deps (copied from the build stage's install)
COPY --from=build /app/lablr-render/node_modules ./node_modules
COPY --from=build /app/lablr-render/dist ./dist
ENV PORT=8090
EXPOSE 8090
CMD ["node", "dist/server.mjs"]
