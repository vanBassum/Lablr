#pragma once
#include "ServiceProvider.h"
#include "BleManager/BleManager.h"
#include "CloudLinkManager/CloudLinkManager.h"
#include "CommandManager/CommandManager.h"
#include "ConsoleManager/ConsoleManager.h"
#include "DeviceManager/DeviceManager.h"
#include "NetworkManager/NetworkManager.h"
#include "SettingsManager/SettingsManager.h"
#include "UpdateManager/UpdateManager.h"
#include "UsbHostManager/UsbHostManager.h"
#include "WebServerManager/WebServerManager.h"

class ApplicationContext : public ServiceProvider
{
public:
    ApplicationContext() = default;
    ~ApplicationContext() = default;
    ApplicationContext(const ApplicationContext&) = delete;
    ApplicationContext& operator=(const ApplicationContext&) = delete;

    BleManager& getBleManager() override { return m_bleManager; }
    CloudLinkManager& getCloudLinkManager() override { return m_cloudLinkManager; }
    CommandManager& getCommandManager() override { return m_commandManager; }
    ConsoleManager& getConsoleManager() override { return m_consoleManager; }
    DeviceManager& getDeviceManager() override { return m_deviceManager; }
    NetworkManager& getNetworkManager() override { return m_networkManager; }
    SettingsManager& getSettingsManager() override { return m_settingsManager; }
    UpdateManager& getUpdateManager() override { return m_updateManager; }
    UsbHostManager& getUsbHostManager() override { return m_usbHostManager; }
    WebServerManager& getWebServerManager() override { return m_webServerManager; }

private:
    ConsoleManager m_consoleManager{*this};
    SettingsManager m_settingsManager{*this};
    NetworkManager m_networkManager{*this};
    CommandManager m_commandManager{*this};
    // UsbHost before Ble: the BLE gateway forwards received bytes into the USB host.
    UsbHostManager m_usbHostManager{*this};
    BleManager m_bleManager{*this};
    CloudLinkManager m_cloudLinkManager{*this};
    DeviceManager m_deviceManager{*this};
    UpdateManager m_updateManager{*this};
    WebServerManager m_webServerManager{*this};
};
