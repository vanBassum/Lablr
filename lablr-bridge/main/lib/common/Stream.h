#pragma once

#include <cstddef>
#include <freertos/FreeRTOS.h>

class Stream
{
public:
    virtual ~Stream() = default;

    virtual size_t write(const void* data, size_t size, TickType_t timeout = portMAX_DELAY) = 0;
    virtual size_t read(void* buffer, size_t size, TickType_t timeout = portMAX_DELAY) = 0;

    virtual size_t available() const { return 0; }
    virtual bool flush() { return true; }
};
