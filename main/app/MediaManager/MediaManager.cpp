#include "MediaManager.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "StorageManager.h"
#include "PrintManager.h"
#include "JsonHelpers.h"
#include "esp_log.h"
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <climits>

// A medium's file is a few hundred bytes of JSON at most.
static constexpr size_t MEDIA_FILE_MAX = 512;

MediaManager::MediaManager(AppProvider& app)
    : app_(app)
{
}

void MediaManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    app_.getStrux().getCommandManager().Register(this, commands_);

    init.SetReady();
    ESP_LOGI(TAG, "Initialized");
}

// ──────────────────────────────────────────────────────────────
// Ids and paths
// ──────────────────────────────────────────────────────────────

bool MediaManager::ValidId(const char* id)
{
    if (!id || !*id) return false;
    size_t n = 0;
    for (const char* p = id; *p; ++p, ++n)
    {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok) return false;
    }
    return n < MAX_ID;
}

bool MediaManager::PathFor(const char* id, char* out, size_t cap)
{
    if (!ValidId(id)) return false;
    char wire[MAX_ID + 24];
    snprintf(wire, sizeof(wire), "%s/%s.json", MEDIA_DIR, id);
    return StorageManager::Resolve(wire, out, cap);
}

// ──────────────────────────────────────────────────────────────
// Reading and writing a definition
// ──────────────────────────────────────────────────────────────

bool MediaManager::Load(const char* id, Medium& out) const
{
    char full[288];
    if (!PathFor(id, full, sizeof(full))) return false;

    FILE* f = fopen(full, "rb");
    if (!f) return false;

    char json[MEDIA_FILE_MAX] = {};
    const size_t n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';
    if (n == 0) return false;

    out = Medium{};
    snprintf(out.id, sizeof(out.id), "%s", id);
    if (!ExtractJsonString(json, "name", out.name, sizeof(out.name)))
        snprintf(out.name, sizeof(out.name), "%s", id);

    out.widthUm   = ExtractJsonInt(json, "widthUm",   0);
    out.heightUm  = ExtractJsonInt(json, "heightUm",  0);
    out.offsetXUm = ExtractJsonInt(json, "offsetXUm", 0);
    out.offsetYUm = ExtractJsonInt(json, "offsetYUm", 0);

    // A medium with no size is not a medium. Refusing here means every caller
    // downstream can divide by it.
    return out.widthUm > 0 && out.heightUm > 0;
}

bool MediaManager::Save(const Medium& m) const
{
    char full[288];
    if (!PathFor(m.id, full, sizeof(full))) return false;

    FILE* f = fopen(full, "wb");
    if (!f) return false;

    // Written by hand rather than through JsonWriter: that class writes to a
    // Stream for the log broadcast, and a file is not one. Six fields do not
    // justify an adapter.
    const int written = fprintf(f,
        "{\"id\":\"%s\",\"name\":\"%s\","
        "\"widthUm\":%ld,\"heightUm\":%ld,"
        "\"offsetXUm\":%ld,\"offsetYUm\":%ld}\n",
        m.id, m.name,
        (long)m.widthUm, (long)m.heightUm,
        (long)m.offsetXUm, (long)m.offsetYUm);
    fclose(f);

    if (written <= 0)
    {
        ESP_LOGE(TAG, "could not write %s", full);
        return false;
    }
    ESP_LOGI(TAG, "saved '%s': %ld x %ld um, offset %ld,%ld um",
             m.id, (long)m.widthUm, (long)m.heightUm,
             (long)m.offsetXUm, (long)m.offsetYUm);
    return true;
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError MediaManager::Cmd_MediaList(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    auto resp = ctx.reply.object();

    if (!app_.getStorageManager().IsMounted())
    {
        resp.field("ok", false);
        resp.field("error", "not mounted");
        return RequestError::Ok;
    }

    resp.field("ok", true);

    const uint32_t dpi = PrintManager::DPI;
    resp.field("dpi", dpi);
    resp.field("headDots", PrintManager::HEAD_DOTS);

    char dirPath[288];
    StorageManager::Resolve(MEDIA_DIR, dirPath, sizeof(dirPath));

    auto arr = resp.array("media");
    DIR* d = opendir(dirPath);
    if (d)
    {
        while (const dirent* e = readdir(d))
        {
            const char* dot = strrchr(e->d_name, '.');
            if (!dot || strcmp(dot, ".json") != 0) continue;

            char id[MAX_ID] = {};
            const size_t idLen = static_cast<size_t>(dot - e->d_name);
            if (idLen == 0 || idLen >= sizeof(id)) continue;
            memcpy(id, e->d_name, idLen);

            Medium m;
            if (!Load(id, m)) continue;

            auto item = arr.object();
            item.field("id", m.id);
            item.field("name", m.name);
            item.field("widthUm", m.widthUm);
            item.field("heightUm", m.heightUm);
            item.field("offsetXUm", m.offsetXUm);
            item.field("offsetYUm", m.offsetYUm);
            item.field("widthDots", UmToDots(m.widthUm, dpi));
            item.field("heightDots", UmToDots(m.heightUm, dpi));
        }
        closedir(d);
    }
    return RequestError::Ok;
}

RequestError MediaManager::Cmd_MediaGet(CommandContext& ctx)
{
    char id[MAX_ID] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Required("id", id, "Which medium, as 'media list' reports it.")));

    Medium m;
    auto resp = ctx.reply.object();

    if (!app_.getStorageManager().IsMounted())
    {
        resp.field("ok", false);
        resp.field("error", "not mounted");
        return RequestError::Ok;
    }
    if (!Load(id, m))
    {
        resp.field("ok", false);
        resp.field("error", "no such medium");
        return RequestError::Ok;
    }

    const uint32_t dpi = PrintManager::DPI;
    resp.field("ok", true);
    resp.field("id", m.id);
    resp.field("name", m.name);
    resp.field("widthUm", m.widthUm);
    resp.field("heightUm", m.heightUm);
    resp.field("offsetXUm", m.offsetXUm);
    resp.field("offsetYUm", m.offsetYUm);

    // What it works out to on THIS printer. The dots are derived, never stored:
    // the paper does not change when the printer does.
    resp.field("dpi", dpi);
    resp.field("headDots", PrintManager::HEAD_DOTS);
    resp.field("widthDots", UmToDots(m.widthUm, dpi));
    resp.field("heightDots", UmToDots(m.heightUm, dpi));
    resp.field("offsetXDots", UmToDots(m.offsetXUm, dpi));
    resp.field("offsetYDots", UmToDots(m.offsetYUm, dpi));
    return RequestError::Ok;
}

RequestError MediaManager::Cmd_MediaSet(CommandContext& ctx)
{
    // Arguments are declared in one go (the parser has no second pass), so the
    // stored medium cannot be loaded first to act as the defaults. INT32_MIN is
    // the "not supplied" marker instead - and it has to be a marker rather than
    // 0, because 0 is a perfectly real offset and calibration must be able to
    // put one back to it.
    static constexpr int32_t UNSET = INT32_MIN;

    char    id[MAX_ID]     = {};
    char    name[MAX_NAME] = {};
    int32_t widthUm   = UNSET;
    int32_t heightUm  = UNSET;
    int32_t offsetXUm = UNSET;
    int32_t offsetYUm = UNSET;

    RETURN_IF_ERROR(ctx.readArgs(
        Required("id",        id,
                 "Short identifier, letters/digits/dash/underscore only - it is "
                 "the filename in /media. Reusing one updates that medium."),
        Optional("name",      name,
                 "What a human calls this stock, e.g. 'Square 25 x 25 mm'. "
                 "Defaults to the id when creating."),
        Optional("widthUm",   widthUm,
                 "Label width ACROSS the print head, in micrometres. 25 mm is "
                 "25000. Required when creating."),
        Optional("heightUm",  heightUm,
                 "Label length ALONG the feed, in micrometres. Required when "
                 "creating."),
        Optional("offsetXUm", offsetXUm,
                 "Where the label's left edge sits across the head, in "
                 "micrometres from the head's own left edge. Calibration, not a "
                 "design choice - measure it, do not guess it."),
        Optional("offsetYUm", offsetYUm,
                 "Where the label's top edge sits relative to the first raster "
                 "line, in micrometres. NEGATIVE when the printer starts "
                 "printing after the label's edge has passed, which crops that "
                 "much off the top of the design. Calibration.")
    ));

    auto resp = ctx.reply.object();

    if (!app_.getStorageManager().IsMounted())
    {
        resp.field("ok", false);
        resp.field("error", "not mounted");
        return RequestError::Ok;
    }
    if (!ValidId(id))
    {
        resp.field("ok", false);
        resp.field("error", "id must be letters, digits, dash or underscore");
        return RequestError::Ok;
    }

    // Merge over what is already stored, so a one-field edit is a one-argument
    // call. That matters most for calibration, which changes one offset at a
    // time and must not silently reset a size.
    Medium m;
    const bool existed = Load(id, m);
    if (!existed)
    {
        m = Medium{};
        snprintf(m.id, sizeof(m.id), "%s", id);
    }
    if (name[0])            snprintf(m.name, sizeof(m.name), "%s", name);
    if (widthUm   != UNSET) m.widthUm   = widthUm;
    if (heightUm  != UNSET) m.heightUm  = heightUm;
    if (offsetXUm != UNSET) m.offsetXUm = offsetXUm;
    if (offsetYUm != UNSET) m.offsetYUm = offsetYUm;

    if (!m.name[0]) snprintf(m.name, sizeof(m.name), "%s", id);

    if (m.widthUm <= 0 || m.heightUm <= 0)
    {
        resp.field("ok", false);
        resp.field("error", "widthUm and heightUm are required and must be positive");
        return RequestError::Ok;
    }

    if (!Save(m))
    {
        resp.field("ok", false);
        resp.field("error", "could not write the definition");
        return RequestError::Ok;
    }

    const uint32_t dpi = PrintManager::DPI;
    resp.field("ok", true);
    resp.field("created", !existed);
    resp.field("id", m.id);
    resp.field("name", m.name);
    resp.field("widthUm", m.widthUm);
    resp.field("heightUm", m.heightUm);
    resp.field("offsetXUm", m.offsetXUm);
    resp.field("offsetYUm", m.offsetYUm);
    resp.field("widthDots", UmToDots(m.widthUm, dpi));
    resp.field("heightDots", UmToDots(m.heightUm, dpi));
    resp.field("offsetXDots", UmToDots(m.offsetXUm, dpi));
    resp.field("offsetYDots", UmToDots(m.offsetYUm, dpi));
    return RequestError::Ok;
}

RequestError MediaManager::Cmd_MediaDelete(CommandContext& ctx)
{
    char id[MAX_ID] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Required("id", id, "Which medium to forget.")));

    auto resp = ctx.reply.object();

    char full[288];
    if (!app_.getStorageManager().IsMounted())
    {
        resp.field("ok", false);
        resp.field("error", "not mounted");
    }
    else if (!PathFor(id, full, sizeof(full)))
    {
        resp.field("ok", false);
        resp.field("error", "bad id");
    }
    else if (remove(full) != 0)
    {
        resp.field("ok", false);
        resp.field("error", "no such medium");
    }
    else
    {
        ESP_LOGI(TAG, "deleted '%s'", id);
        resp.field("ok", true);
        resp.field("id", id);
    }
    return RequestError::Ok;
}
