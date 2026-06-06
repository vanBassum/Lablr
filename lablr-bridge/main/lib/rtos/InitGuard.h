#pragma once
#include <assert.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "InitState.h"  // For the macro's



class [[deprecated("InitGuard is legacy. Use InitState (TryBeginInit/InitAttempt) instead.")]] InitGuard
{
public:

    InitGuard(const InitGuard&) = delete;
    InitGuard& operator=(const InitGuard&) = delete;

    InitGuard() {
        _group = xEventGroupCreate();
        assert(_group && "Failed to create event group");
    }

    ~InitGuard() {
        vEventGroupDelete(_group);
    }

    void SetReady() {
        xEventGroupSetBits(_group, READY_BIT);
    }

    bool IsReady() const {
        return WaitForReady(0);
    }

    void SetNotReady() {
        xEventGroupClearBits(_group, READY_BIT);
    }

    bool WaitForReady(TickType_t timeout) const {
        return (xEventGroupWaitBits(_group, READY_BIT, false, true, timeout) & READY_BIT) != 0;
    }

private:
    volatile EventGroupHandle_t _group = nullptr;
    static inline constexpr EventBits_t READY_BIT = (1 << 0);
};
