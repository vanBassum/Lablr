#include "StaticFileHandler.h"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <esp_log.h>

static constexpr const char* TAG = "StaticFileHandler";

void StaticFileHandler::RegisterRoute(httpd_handle_t server, const char* basePath)
{
    // Store basePath as user_ctx so the static handler can access it
    const httpd_uri_t route = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = Handle,
        .user_ctx = const_cast<char*>(basePath),
        .is_websocket = false,
        .handle_ws_control_frames = false,
        .supported_subprotocol = nullptr,
    };
    httpd_register_uri_handler(server, &route);
}

const char* StaticFileHandler::GetContentType(const char* ext)
{
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".ico") == 0) return "image/x-icon";
    if (strcmp(ext, ".svg") == 0) return "image/svg+xml";
    return "application/octet-stream";
}

bool StaticFileHandler::IsSafePath(const char* uri)
{
    return strstr(uri, "..") == nullptr;
}

bool StaticFileHandler::Resolve(const char* basePath, const char* uri, Resolved& out)
{
    if (!IsSafePath(uri))
    {
        ESP_LOGW(TAG, "Rejected path traversal attempt: %s", uri);
        return false;
    }

    // Strip query string
    char clean[256];
    if (const char* query = strchr(uri, '?'))
    {
        size_t len = static_cast<size_t>(query - uri);
        if (len >= sizeof(clean)) len = sizeof(clean) - 1;
        memcpy(clean, uri, len);
        clean[len] = '\0';
        uri = clean;
    }

    if (uri[0] == '\0' || strcmp(uri, "/") == 0) uri = "/index.html";

    // Callers over the wire may omit the leading slash.
    const char* sep = (uri[0] == '/') ? "" : "/";

    out.contentType = "application/octet-stream";
    if (const char* ext = strrchr(uri, '.')) out.contentType = GetContentType(ext);

    // The build gzips everything into www/, so .gz is the common case, not the
    // exception. `gzipped` must reach the client as Content-Encoding, or it
    // receives gzip bytes labelled as JavaScript.
    struct stat st;
    snprintf(out.path, sizeof(out.path), "%s%s%s.gz", basePath, sep, uri);
    if (stat(out.path, &st) == 0)
    {
        out.gzipped = true;
        return true;
    }

    snprintf(out.path, sizeof(out.path), "%s%s%s", basePath, sep, uri);
    if (stat(out.path, &st) == 0)
    {
        out.gzipped = false;
        return true;
    }

    return false;
}

esp_err_t StaticFileHandler::Handle(httpd_req_t* req)
{
    const char* basePath = static_cast<const char*>(req->user_ctx);

    if (!IsSafePath(req->uri))
    {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_OK;
    }

    Resolved file;
    if (!Resolve(basePath, req->uri, file))
    {
        // SPA fallback lives here, in the route layer — not in Resolve(), which
        // stays "give me this exact file or nothing".
        if (!Resolve(basePath, "/index.html", file))
        {
            httpd_resp_send_404(req);
            return ESP_OK;
        }
        file.contentType = "text/html";
    }

    FILE* f = fopen(file.path, "rb");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    httpd_resp_set_type(req, file.contentType);
    if (file.gzipped)
    {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    // Caching, and it has to be told rather than left to the browser.
    //
    // This server sent no Cache-Control, no ETag and no Last-Modified, which does
    // not mean "do not cache" — it means the browser may guess, and Chrome guesses
    // yes. That is wrong in the worst way for the URLs here whose names are
    // deliberately STABLE: `/index.html` does not change when its contents do, so a
    // www partition updated by OTA kept being served from disk cache and the new UI
    // simply did not appear. Nothing was broken and nothing said so.
    //
    // Two rules, decided by whether the name identifies the bytes:
    //   /assets/<name>-<hash>.js  content-hashed by the build, so a change is a new
    //                             URL and the old one can be kept forever.
    //   everything else           index.html above all, whose name is stable, so it
    //                             must be revalidated every load.
    //
    // `no-cache` rather than `no-store`: the browser may still keep the bytes, it
    // just may not use them without asking. There is nothing to revalidate WITH yet
    // — no ETag — so today that is a plain refetch, which is what an ESP32 serving a
    // page a handful of times a day should do. An ETag is the optimisation, not the
    // fix.
    const bool hashedAsset = strncmp(req->uri, "/assets/", 8) == 0;
    httpd_resp_set_hdr(
        req,
        "Cache-Control",
        hashedAsset ? "public, max-age=31536000, immutable" : "no-cache");

    char readBuf[512];
    size_t n;
    while ((n = fread(readBuf, 1, sizeof(readBuf), f)) > 0)
    {
        httpd_resp_send_chunk(req, readBuf, n);
    }
    fclose(f);

    httpd_resp_send_chunk(req, nullptr, 0);
    return ESP_OK;
}
