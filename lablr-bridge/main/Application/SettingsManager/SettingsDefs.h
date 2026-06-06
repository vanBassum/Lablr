#pragma once

#include "SettingsManager.h"

// ──────────────────────────────────────────────────────────────
// Setting definitions — add new settings here
// ──────────────────────────────────────────────────────────────

inline constexpr SettingDef SETTINGS_DEFS[] = {
    // WiFi — used only for the config/status web UI and OTA, not for printing.
    { "wifi.ssid",      SettingType::String, "WiFi SSID",      "" },
    { "wifi.password",  SettingType::String, "WiFi Password",  "" },

    // Device — name is also the BLE advertised name and the mDNS hostname.
    { "device.name",    SettingType::String, "Device Name",    "lablr-bridge" },
    { "device.pin",     SettingType::String, "Device PIN",     "" },
};

inline constexpr int SETTINGS_DEFS_COUNT = sizeof(SETTINGS_DEFS) / sizeof(SETTINGS_DEFS[0]);
