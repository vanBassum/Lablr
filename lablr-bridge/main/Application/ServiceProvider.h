#pragma once

class BleManager;
class CloudLinkManager;
class CommandManager;
class ConsoleManager;
class DeviceManager;
class NetworkManager;
class SettingsManager;
class UpdateManager;
class UsbHostManager;
class WebServerManager;

class ServiceProvider
{
public:
    virtual BleManager& getBleManager() = 0;
    virtual CloudLinkManager& getCloudLinkManager() = 0;
    virtual CommandManager& getCommandManager() = 0;
    virtual ConsoleManager& getConsoleManager() = 0;
    virtual DeviceManager& getDeviceManager() = 0;
    virtual NetworkManager& getNetworkManager() = 0;
    virtual SettingsManager& getSettingsManager() = 0;
    virtual UpdateManager& getUpdateManager() = 0;
    virtual UsbHostManager& getUsbHostManager() = 0;
    virtual WebServerManager& getWebServerManager() = 0;
};
