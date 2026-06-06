#include "StaticFileHandler.h"

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

esp_err_t StaticFileHandler::Handle(httpd_req_t* req)
{
    const char* basePath = static_cast<const char*>(req->user_ctx);
    char filepath[600];
    const char* uri = req->uri;

    // Strip query string
    const char* query = strchr(uri, '?');
    char uri_clean[256];
    if (query)
    {
        size_t len = query - uri;
        if (len >= sizeof(uri_clean)) len = sizeof(uri_clean) - 1;
        memcpy(uri_clean, uri, len);
        uri_clean[len] = '\0';
        uri = uri_clean;
    }

    // Reject path traversal attempts
    if (strstr(uri, "..") != nullptr)
    {
        ESP_LOGW(TAG, "Rejected path traversal attempt: %s", uri);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid path");
        return ESP_OK;
    }

    if (strcmp(uri, "/") == 0)
    {
        uri = "/index.html";
    }

    // Determine content type from extension
    const char* type = "application/octet-stream";
    const char* ext = strrchr(uri, '.');
    if (ext)
    {
        type = GetContentType(ext);
    }

    // Try .gz version first
    bool is_gzipped = false;
    snprintf(filepath, sizeof(filepath), "%s%s.gz", basePath, uri);

    struct stat st;
    if (stat(filepath, &st) == 0)
    {
        is_gzipped = true;
    }
    else
    {
        snprintf(filepath, sizeof(filepath), "%s%s", basePath, uri);
        if (stat(filepath, &st) != 0)
        {
            // SPA fallback: serve index.html for non-file routes
            snprintf(filepath, sizeof(filepath), "%s/index.html.gz", basePath);
            if (stat(filepath, &st) == 0)
            {
                is_gzipped = true;
                type = "text/html";
            }
            else
            {
                snprintf(filepath, sizeof(filepath), "%s/index.html", basePath);
                if (stat(filepath, &st) != 0)
                {
                    httpd_resp_send_404(req);
                    return ESP_OK;
                }
                type = "text/html";
            }
        }
    }

    FILE* f = fopen(filepath, "rb");
    if (!f)
    {
        httpd_resp_send_404(req);
        return ESP_OK;
    }

    httpd_resp_set_type(req, type);
    if (is_gzipped)
    {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

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
