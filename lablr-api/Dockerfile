# Build context is the repo root (see docker-compose.yml) so both apps are available.

# --- Build the PWA ---
FROM node:22-alpine AS ui
RUN corepack enable
WORKDIR /ui
COPY lablr-ui/package.json lablr-ui/pnpm-lock.yaml ./
RUN pnpm install --frozen-lockfile
COPY lablr-ui/ ./
RUN pnpm build

# --- Build the API ---
FROM mcr.microsoft.com/dotnet/sdk:9.0 AS build
WORKDIR /src
COPY lablr-api/*.csproj ./
RUN dotnet restore
COPY lablr-api/ ./
RUN dotnet publish -c Release -o /app/publish --no-restore

# --- Runtime: API serves /api, /pictograms, and the PWA (wwwroot) same-origin ---
FROM mcr.microsoft.com/dotnet/aspnet:9.0 AS runtime
WORKDIR /app
COPY --from=build /app/publish ./
COPY --from=ui /ui/dist ./wwwroot
ENV ASPNETCORE_HTTP_PORTS=8080
EXPOSE 8080
ENTRYPOINT ["dotnet", "Lablr.Api.dll"]
