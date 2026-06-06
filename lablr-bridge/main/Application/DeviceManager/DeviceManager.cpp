#include "DeviceManager.h"
#include "esp_log.h"

DeviceManager::DeviceManager(ServiceProvider &ctx)
    : serviceProvider_(ctx)
{
}

void DeviceManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    led_.Init();
    rgbLed_.Init();   // blank the over-bright on-board WS2812

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}
