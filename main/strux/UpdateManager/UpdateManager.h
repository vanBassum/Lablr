#pragma once

#include "StruxProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include <esp_ota_ops.h>

class Stream;

class UpdateManager {
    static constexpr const char* TAG = "UpdateManager";

public:
    explicit UpdateManager(StruxProvider& strux);

    UpdateManager(const UpdateManager&) = delete;
    UpdateManager& operator=(const UpdateManager&) = delete;

    void Init();

    // Everything else is commands: the manager's entire external
    // surface is its command table below.

private:
    StruxProvider& strux_;
    InitState initState_;

    // One mechanism for ANY partition, addressed by label — see PartitionWriter.
    // An upload is one streamed command (writePartition): the handler drains its
    // input stream straight into a PartitionWriter, finalize runs at end-of-stream.
    // There is no cross-request session state; the transport carries the whole
    // image within one command.

    const char* GetRunningPartition() const;
    const char* GetNextPartition() const;

    // ── Partition inspection ──────────────────────────────────

    struct PartitionInfo
    {
        char     label[17];     // esp_partition_t::label is char[17] — 16 + NUL
        char     type[8];       // "app" or "data"
        char     subtype[16];   // "ota_0" / "fat" / "nvs" / "0xNN" …
        uint32_t offset;
        uint32_t size;
        bool     running;
        bool     nextOta;
        bool     uploadable;    // safe to overwrite via upload
        char     version[32];   // app partitions only; empty otherwise
    };

    /// Enumerate all partitions into `out`. Returns count written.
    int GetPartitions(PartitionInfo* out, int maxCount) const;

    // ── Commands (registered with CommandManager in Init) ──
    RequestError Cmd_UpdateStatus(CommandContext& ctx);
    RequestError Cmd_Partitions(CommandContext& ctx);
    /// Streamed upload: header line + body. `offset` is optional and decides which
    /// of two modes this is:
    ///
    ///   absent  — one shot. Erase as we go from zero and activate at the end; the
    ///             whole image in a single command, which is what the web UI sends.
    ///   present — one piece of a caller-driven upload. Writes exactly where told,
    ///             erases nothing, activates nothing. The sender calls
    ///             clearPartition first and activatePartition after the last piece,
    ///             and may leave gaps between pieces for other traffic.
    ///
    /// The second mode exists because a single command that runs for tens of seconds
    /// monopolises the transport, which is what the relay's in-flight timeout trips
    /// over. Many short commands need no concurrency support to coexist with others.
    RequestError Cmd_WritePartition(CommandContext& ctx);
    RequestError Cmd_DownloadPartition(CommandContext& ctx);

    /// Erase a partition whole, so a chunked upload starts from a known state.
    RequestError Cmd_ClearPartition(CommandContext& ctx);

    /// Validate an app image and make it the next boot slot. No-op for data.
    RequestError Cmd_ActivatePartition(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "partition", "status",   &InvokeCommand<&UpdateManager::Cmd_UpdateStatus>,
          "Report the running firmware version, which app slot it booted from, and "
          "which slot the next update would be written to." },
        { "partition", "list",     &InvokeCommand<&UpdateManager::Cmd_Partitions>,
          "List the flash partitions with type, offset, size, and whether each is "
          "running, is the next OTA slot, or may be written to." },
        { "partition", "write",    &InvokeCommand<&UpdateManager::Cmd_WritePartition>,
          "Write an image to a partition. The bytes follow the request envelope in "
          "the same session, so this is a streaming upload rather than an argument. "
          "Destructive: it overwrites what the device boots or serves." },
        { "partition", "clear",    &InvokeCommand<&UpdateManager::Cmd_ClearPartition>,
          "Erase a partition. Destructive and immediate - the running app slot is "
          "refused, anything else is erased." },
        { "partition", "activate", &InvokeCommand<&UpdateManager::Cmd_ActivatePartition>,
          "Mark an app partition as the one to boot next. Takes effect at the next "
          "reboot; an image that does not validate is refused." },
        { "partition", "read",     &InvokeCommand<&UpdateManager::Cmd_DownloadPartition>,
          "Read a partition back. The reply is the raw partition bytes and nothing "
          "else - no header record - streamed until the session closes, which can "
          "be megabytes. The size is what 'partition list' reports." },
    };
};
