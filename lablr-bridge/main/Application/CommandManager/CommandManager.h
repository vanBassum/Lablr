#pragma once

#include "ServiceProvider.h"
#include "InitState.h"

class JsonWriter;

class CommandManager {
    static constexpr const char* TAG = "CommandManager";

public:
    explicit CommandManager(ServiceProvider& serviceProvider);

    CommandManager(const CommandManager&) = delete;
    CommandManager& operator=(const CommandManager&) = delete;
    CommandManager(CommandManager&&) = delete;
    CommandManager& operator=(CommandManager&&) = delete;

    void Init();

    /// Execute a command by type name. Writes response fields into the JsonWriter.
    /// The caller is responsible for the outer object and transport-specific fields (e.g. "id").
    /// Returns true if the command was recognized.
    bool Execute(const char* type, const char* json, JsonWriter& resp);

private:
    ServiceProvider& serviceProvider_;
    InitState initState_;

    using CommandFunc = void (CommandManager::*)(const char* json, JsonWriter& resp);

    struct CommandEntry {
        const char* type;
        CommandFunc func;
        bool requiresAuth;
    };

    bool CheckAuth(const char* json, JsonWriter& resp);

    static const CommandEntry commands_[];

    void Cmd_Ping(const char* json, JsonWriter& resp);
    void Cmd_Info(const char* json, JsonWriter& resp);
    void Cmd_UpdateStatus(const char* json, JsonWriter& resp);
    void Cmd_GetSettings(const char* json, JsonWriter& resp);
    void Cmd_SetSetting(const char* json, JsonWriter& resp);
    void Cmd_SaveSettings(const char* json, JsonWriter& resp);
    void Cmd_Reboot(const char* json, JsonWriter& resp);
    void Cmd_WifiScan(const char* json, JsonWriter& resp);
    void Cmd_GetLogs(const char* json, JsonWriter& resp);
    void Cmd_Partitions(const char* json, JsonWriter& resp);
};
