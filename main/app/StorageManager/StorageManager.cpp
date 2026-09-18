#include "StorageManager.h"
#include "StruxProvider.h"
#include "CommandManager.h"

#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstring>

StorageManager::StorageManager(AppProvider& app)
    : app_(app)
{
}

void StorageManager::Init()
{
    auto initAttempt = initState_.TryBeginInit();
    if (!initAttempt)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    Mount();

    app_.getStrux().getCommandManager().Register(this, commands_);

    initAttempt.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

void StorageManager::Mount()
{
    const esp_vfs_fat_mount_config_t cfg = {
        // A blank partition is the normal first-boot state, not a fault: the
        // table reserves the space and nothing has ever written it. Formatting
        // here is what makes a freshly flashed device usable without a
        // provisioning step.
        .format_if_mount_failed = true,
        // Enough for a render (the SVG) plus a concurrent upload or listing.
        .max_files = 8,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
        .use_one_fat = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(BASE_PATH, PARTITION, &cfg, &wl_);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Mount of partition '%s' failed: %s", PARTITION, esp_err_to_name(err));
        return;
    }
    mounted_ = true;

    // Created every boot rather than only after a format: a directory someone
    // deleted over the wire should come back, and mkdir on an existing path is
    // a no-op that costs nothing.
    static const char* const dirs[] = { "labels", "fonts", "media" };
    for (const char* d : dirs)
    {
        char path[64];
        snprintf(path, sizeof(path), "%s/%s", BASE_PATH, d);
        if (mkdir(path, 0777) != 0 && errno != EEXIST)
            ESP_LOGW(TAG, "Could not create /%s", d);
    }

    uint64_t total = 0, freeBytes = 0;
    esp_vfs_fat_info(BASE_PATH, &total, &freeBytes);
    ESP_LOGI(TAG, "Mounted at %s: %llu KB total, %llu KB free",
             BASE_PATH, total / 1024, freeBytes / 1024);
}

bool StorageManager::Resolve(const char* path, char* out, size_t cap)
{
    if (!path) return false;

    // "..", anywhere, is refused rather than normalised. There is no legitimate
    // use for it here and normalising is how path checks get subtly wrong.
    if (strstr(path, "..") != nullptr) return false;

    const char* sep = (path[0] == '/') ? "" : "/";
    int n = snprintf(out, cap, "%s%s%s", BASE_PATH, sep, path);
    return n > 0 && static_cast<size_t>(n) < cap;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError StorageManager::Cmd_Info(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    uint64_t total = 0, freeBytes = 0;
    const bool ok = mounted_ && esp_vfs_fat_info(BASE_PATH, &total, &freeBytes) == ESP_OK;

    auto resp = ctx.reply.object();
    resp.field("ok", ok);
    resp.field("mounted", mounted_);
    if (ok)
    {
        resp.field("total", static_cast<uint32_t>(total));
        resp.field("free", static_cast<uint32_t>(freeBytes));
        resp.field("used", static_cast<uint32_t>(total - freeBytes));
    }
    return RequestError::Ok;
}

RequestError StorageManager::Cmd_List(CommandContext& ctx)
{
    char path[192] = "/";
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("path", path,
                 "Directory to list, rooted at the filesystem: '/', '/labels', "
                 "'/fonts' or '/media'. Defaults to '/'.")));

    char full[256];
    if (!mounted_ || !Resolve(path, full, sizeof(full)))
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", mounted_ ? "bad path" : "not mounted");
        return RequestError::Ok;
    }

    DIR* dir = opendir(full);
    if (!dir)
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", "no such directory");
        return RequestError::Ok;
    }

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    resp.field("path", path);
    {
        auto entries = resp.array("entries");
        // Sized for the worst case the compiler can prove: a 255-char long
        // filename under a 255-char directory. Anything smaller makes
        // -Wformat-truncation right rather than makes it quiet.
        char child[520];
        struct dirent* e;
        while ((e = readdir(dir)) != nullptr)
        {
            snprintf(child, sizeof(child), "%s/%s", full, e->d_name);

            struct stat st;
            const bool statted = stat(child, &st) == 0;

            auto item = entries.object();
            item.field("name", e->d_name);
            item.field("dir", e->d_type == DT_DIR);
            item.field("size", statted ? static_cast<uint32_t>(st.st_size) : 0u);
        }
    }
    closedir(dir);
    return RequestError::Ok;
}

RequestError StorageManager::Cmd_Read(CommandContext& ctx)
{
    char path[192] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Required("path", path,
                 "File to read, rooted at the filesystem, e.g. "
                 "'/labels/test.svg'.")));

    char full[256];
    FILE* f = nullptr;
    if (mounted_ && Resolve(path, full, sizeof(full)))
        f = fopen(full, "rb");

    // The header is a record and the body is raw bytes after it, so the scope
    // must close before the newline that divides them - hence the braces. Same
    // shape as `web read` and `partition read`.
    if (!f)
    {
        {
            auto head = ctx.reply.object();
            head.field("ok", false);
            head.field("error", mounted_ ? "no such file" : "not mounted");
        }
        ctx.out.write("\n", 1);
        return RequestError::Ok;
    }

    struct stat st;
    const uint32_t size = (stat(full, &st) == 0) ? static_cast<uint32_t>(st.st_size) : 0u;

    {
        auto head = ctx.reply.object();
        head.field("ok", true);
        head.field("path", path);
        head.field("size", size);
    }
    ctx.out.write("\n", 1);

    // Read straight into the reply frame the transport is about to send, so a
    // large file needs no buffer of ours - the same handoff `partition read`
    // uses. Falls back to a small stack buffer for a transport that cannot lend.
    if (ctx.out.canLend())
    {
        for (;;)
        {
            size_t avail = 0;
            uint8_t* dst = ctx.out.lendOutput(avail);
            if (!dst) break;                       // client went away
            size_t n = fread(dst, 1, avail, f);
            if (n == 0) break;
            ctx.out.commitOutput(n);
        }
    }
    else
    {
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            ctx.out.write(buf, n);
    }

    fclose(f);
    return RequestError::Ok;
}

RequestError StorageManager::Cmd_Write(CommandContext& ctx)
{
    char path[192] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Required("path", path,
                 "File to create or replace, rooted at the filesystem, e.g. "
                 "'/labels/vanilla.svg'. The file's bytes follow the envelope "
                 "in this same session - they are not an argument.")));

    char full[256];
    if (!mounted_ || !Resolve(path, full, sizeof(full)))
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", mounted_ ? "bad path" : "not mounted");
        return RequestError::Ok;
    }

    // One level of parent, created on demand: writing "/labels/x.svg" should
    // work, and so should a caller inventing a directory of their own. Deeper
    // nesting is not a thing this filesystem needs.
    if (char* slash = strrchr(full, '/'))
    {
        if (slash > full)
        {
            *slash = '\0';
            mkdir(full, 0777);      // EEXIST is the common case and is fine
            *slash = '/';
        }
    }

    FILE* f = fopen(full, "wb");
    if (!f)
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", "cannot open for writing");
        return RequestError::Ok;
    }

    // Asked before the loop: past this point a 0 means end of body, and a
    // stream that lends nothing would look exactly like an empty file.
    if (!ctx.in.canLend())
    {
        fclose(f);
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", "transport cannot stream");
        return RequestError::Ok;
    }

    uint32_t written = 0;
    bool failed = false;
    const uint8_t* chunk = nullptr;
    size_t n;
    while ((n = ctx.in.lendInput(chunk)) > 0)
    {
        if (fwrite(chunk, 1, n, f) != n) { failed = true; break; }
        written += static_cast<uint32_t>(n);
    }

    // A transport that broke ends the read exactly as a complete body does, so
    // it has to be asked. A half-written label is worse than none: it would
    // parse as an SVG error later, far from here.
    const bool truncated = ctx.in.failed();
    fclose(f);

    if (failed || truncated)
    {
        unlink(full);
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", failed ? "write failed" : "transport failed mid-upload");
        return RequestError::Ok;
    }

    ESP_LOGI(TAG, "Wrote %s (%lu bytes)", path, (unsigned long)written);

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    resp.field("path", path);
    resp.field("written", written);
    return RequestError::Ok;
}

RequestError StorageManager::Cmd_Delete(CommandContext& ctx)
{
    char path[192] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Required("path", path, "File to delete, rooted at the filesystem.")));

    char full[256];
    if (!mounted_ || !Resolve(path, full, sizeof(full)))
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", mounted_ ? "bad path" : "not mounted");
        return RequestError::Ok;
    }

    struct stat st;
    if (stat(full, &st) != 0)
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", "no such file");
        return RequestError::Ok;
    }
    if (S_ISDIR(st.st_mode))
    {
        auto resp = ctx.reply.object();
        resp.field("ok", false);
        resp.field("error", "is a directory");
        return RequestError::Ok;
    }

    const bool ok = unlink(full) == 0;
    auto resp = ctx.reply.object();
    resp.field("ok", ok);
    if (!ok) resp.field("error", "delete failed");
    return RequestError::Ok;
}
