#include "BoardContext.h"
#include "esp_log.h"

void BoardContext::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    // Nothing to bring up yet: the only role this board binds is the LED, and
    // it is a MockLed with no hardware behind it (see BoardConfig.h). Drivers
    // and bus hosts get their Init() calls here as they are added.

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}
