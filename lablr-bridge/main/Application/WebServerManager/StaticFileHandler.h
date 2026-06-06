#pragma once

#include <esp_http_server.h>

class StaticFileHandler {
public:
    void RegisterRoute(httpd_handle_t server, const char* basePath);

private:
    static esp_err_t Handle(httpd_req_t* req);
    static const char* GetContentType(const char* ext);
};
