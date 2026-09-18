#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include <wear_levelling.h>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// The label filesystem: the `storage` partition, mounted FAT with wear
// levelling, and the small command surface that lets a caller put files on it.
//
// An APPLICATION manager, deliberately. The framework already mounts nothing -
// the web UI stopped being a partition and became rodata in the app image - so
// a filesystem is this product's concern and nothing in strux/ knows it exists.
//
// Three directories, created at mount so they are always there to list:
//
//   /labels   label designs, one SVG per label. The document IS the label.
//   /fonts    TTF fonts. RenderManager registers every one of these with
//             ThorVG at boot, under its filename without the extension, which
//             is the name an SVG's font-family has to match.
//   /media    reserved for physical label-media definitions. Empty for now,
//             and named so the shape of what is coming is visible.
//
// Paths on the wire are rooted at the mount, not at the VFS: a caller says
// "/labels/x.svg" and never learns this is "/storage/labels/x.svg" on a FAT
// partition. That is the same courtesy `web read` does for the web assets.
//
// FILE CONTENTS NEVER TRAVEL AS ARGUMENTS. The envelope is capped at 512 bytes
// and a single value at 192, so an SVG could not fit even if it were the right
// shape - and it is not. Bodies stream, exactly as `partition write` does, and
// replies stream back behind a header record, exactly as `web read` does.
// ──────────────────────────────────────────────────────────────

class StorageManager
{
    static constexpr const char* TAG = "StorageManager";

public:
    /// Where the partition is mounted in the VFS. Callers of the commands never
    /// see this; it is public because RenderManager resolves paths too.
    static constexpr const char* BASE_PATH = "/storage";

    /// The partition label in partitions-16mb.csv.
    static constexpr const char* PARTITION = "storage";

    explicit StorageManager(AppProvider& app);

    StorageManager(const StorageManager&) = delete;
    StorageManager& operator=(const StorageManager&) = delete;
    StorageManager(StorageManager&&) = delete;
    StorageManager& operator=(StorageManager&&) = delete;

    void Init();

    bool IsMounted() const { return mounted_; }

    /// Turn a caller's path ("/labels/x.svg", or "labels/x.svg") into an
    /// absolute VFS path. False when the path escapes the mount or does not
    /// fit - the only path validation there is, and the reason no handler
    /// builds a path itself.
    static bool Resolve(const char* path, char* out, size_t cap);

private:
    AppProvider& app_;
    InitState initState_;

    bool mounted_ = false;
    wl_handle_t wl_ = WL_INVALID_HANDLE;

    void Mount();

    // ── Commands ──
    RequestError Cmd_Info(CommandContext& ctx);
    RequestError Cmd_List(CommandContext& ctx);
    RequestError Cmd_Read(CommandContext& ctx);
    RequestError Cmd_Write(CommandContext& ctx);
    RequestError Cmd_Delete(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "fs", "info",   &InvokeCommand<&StorageManager::Cmd_Info>,
          "Report the label filesystem: whether it is mounted, and its total, "
          "used and free bytes." },
        { "fs", "list",   &InvokeCommand<&StorageManager::Cmd_List>,
          "List one directory. Returns each entry's name, whether it is a "
          "directory, and its size in bytes. Start at '/' to see the top "
          "level: /labels, /fonts and /media." },
        { "fs", "read",   &InvokeCommand<&StorageManager::Cmd_Read>,
          "Read one file. The reply is a JSON header line (ok, size), a "
          "newline, then the raw file bytes - so an SVG comes back exactly as "
          "stored, which is what makes an existing label usable as a style "
          "reference." },
        { "fs", "write",  &InvokeCommand<&StorageManager::Cmd_Write>,
          "Create or replace one file. The bytes follow the request envelope "
          "in the same session, so this is a streaming upload and NOT an "
          "argument - file contents never fit in an argument. Writing an "
          "existing path replaces it." },
        { "fs", "delete", &InvokeCommand<&StorageManager::Cmd_Delete>,
          "Delete one file. Directories are refused; the three top-level "
          "directories are part of the device." },
    };
};
