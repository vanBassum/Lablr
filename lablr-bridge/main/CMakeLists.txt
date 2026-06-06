set(SOURCE_FILES_LIST
    "main.cpp"
    "Application/NetworkManager/NetworkManager.cpp"
    "Application/NetworkManager/NetworkInterface.cpp"
    "Application/NetworkManager/WiFiInterface.cpp"
    "Application/NetworkManager/DnsConfiguration.cpp"
    "Application/WebServerManager/WebServerManager.cpp"
    "Application/WebServerManager/StaticFileHandler.cpp"
    "Application/WebServerManager/WebSocketHandler.cpp"
    "Application/CommandManager/CommandManager.cpp"
    "Application/UpdateManager/UpdateManager.cpp"
    "Application/SettingsManager/SettingsManager.cpp"
    "Application/ConsoleManager/ConsoleManager.cpp"
    "Application/DeviceManager/DeviceManager.cpp"
    "Application/BleManager/BleManager.cpp"
    "Application/UsbHostManager/UsbHostManager.cpp"
    "lib/system/DateTime.cpp"
    "lib/system/TimeSpan.cpp"
)

set(INCLUDE_DIRS_LIST
    .
    "Application"
    "Application/NetworkManager"
    "Application/WebServerManager"
    "Application/CommandManager"
    "Application/UpdateManager"
    "Application/SettingsManager"
    "Application/ConsoleManager"
    "Application/DeviceManager"
    "Application/BleManager"
    "Application/UsbHostManager"
    "hardware"
    "lib"
    "lib/common"
    "lib/rtos"
    "lib/json"
    "lib/system"
)

set(COMPONENT_REQUIRES
    esp_timer
    esp_netif
    esp_event
    esp_wifi
    nvs_flash
    esp_http_server
    esp_app_format
    app_update
    esp_partition
    fatfs
    wear_levelling
    esp_driver_gpio
    mdns
    bt                  # NimBLE host — phone <-> bridge link
    espressif__usb      # USB Host Library — bridge <-> Dymo (registry component in IDF 6.0)
    espressif__led_strip # WS2812 driver — blank the over-bright on-board RGB LED
)

# Register component with ESP-IDF
idf_component_register(
    SRCS ${SOURCE_FILES_LIST}
    INCLUDE_DIRS ${INCLUDE_DIRS_LIST}
    REQUIRES ${COMPONENT_REQUIRES}
)
