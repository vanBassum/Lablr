#include "PrinterManager.h"
#include "StorageManager/StorageManager.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "SettingsManager.h"
#include "JsonHelpers.h"
#include "DotGeometry.h"

#include <esp_log.h>
#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static constexpr size_t PRINTER_FILE_MAX = 512;

PrinterManager::PrinterManager(AppProvider& app) : app_(app) {}

void PrinterManager::BuiltIn(Printer& out)
{
    out = Printer{};
    snprintf(out.id,    sizeof(out.id),    "%s", "dymo450");
    snprintf(out.name,  sizeof(out.name),  "%s", "DYMO LabelWriter 450");
    snprintf(out.model, sizeof(out.model), "%s", "DYMO LabelWriter 450");
    out.dpi      = 300;
    out.headDots = 672;
    // Measured on the 25 x 25 mm stock. These used to live on that medium as
    // negative offsets doing double duty; see the header.
    out.deadLeftUm = 1016;
    out.deadTopUm  = 3133;
    out.maxLines   = 4000;
    out.builtIn    = true;
}

void PrinterManager::Init()
{
    auto init = initState_.TryBeginInit();
    if (!init)
    {
        ESP_LOGW(TAG, "Already initialized or initializing");
        return;
    }

    app_.getStrux().getSettingsManager().Register({ &active_ });
    app_.getStrux().getCommandManager().Register(this, commands_);

    Printer p;
    Active(p);
    ESP_LOGI(TAG, "printer '%s' (%s): %lu dpi, %lu dot head, dead %ld,%ld um",
             p.id, p.builtIn ? "built-in" : "file",
             (unsigned long)p.dpi, (unsigned long)p.headDots,
             (long)p.deadLeftUm, (long)p.deadTopUm);

    init.SetReady();
}

// ──────────────────────────────────────────────────────────────
// Reading and writing a definition
// ──────────────────────────────────────────────────────────────

bool PrinterManager::ValidId(const char* id)
{
    if (!id || !id[0]) return false;
    size_t n = 0;
    for (const char* p = id; *p; ++p, ++n)
    {
        const char c = *p;
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                        (c >= '0' && c <= '9') || c == '_' || c == '-';
        if (!ok) return false;
    }
    return n < MAX_ID;
}

bool PrinterManager::PathFor(const char* id, char* out, size_t cap) const
{
    if (!ValidId(id)) return false;
    const int n = snprintf(out, cap, "%s%s/%s.json",
                           StorageManager::BASE_PATH, PRINTER_DIR, id);
    return n > 0 && static_cast<size_t>(n) < cap;
}

bool PrinterManager::Load(const char* id, Printer& out) const
{
    char full[288];
    if (!PathFor(id, full, sizeof(full))) return false;

    FILE* f = fopen(full, "rb");
    if (!f) return false;

    char json[PRINTER_FILE_MAX] = {};
    const size_t n = fread(json, 1, sizeof(json) - 1, f);
    fclose(f);
    json[n] = '\0';
    if (n == 0) return false;

    Printer def;
    BuiltIn(def);

    out = Printer{};
    snprintf(out.id, sizeof(out.id), "%s", id);
    if (!ExtractJsonString(json, "name", out.name, sizeof(out.name)))
        snprintf(out.name, sizeof(out.name), "%s", id);
    if (!ExtractJsonString(json, "model", out.model, sizeof(out.model)))
        snprintf(out.model, sizeof(out.model), "%s", def.model);

    // A file may say as little as it likes; what it leaves out is the built-in
    // machine's answer, so a file that only corrects the dead zone is valid and
    // is also the common case.
    out.dpi        = static_cast<uint32_t>(ExtractJsonInt(json, "dpi",        (int32_t)def.dpi));
    out.headDots   = static_cast<uint32_t>(ExtractJsonInt(json, "headDots",   (int32_t)def.headDots));
    out.deadLeftUm = ExtractJsonInt(json, "deadLeftUm", def.deadLeftUm);
    out.deadTopUm  = ExtractJsonInt(json, "deadTopUm",  def.deadTopUm);
    out.maxLines   = static_cast<uint32_t>(ExtractJsonInt(json, "maxLines",   (int32_t)def.maxLines));
    out.builtIn    = false;

    // A dead zone is an amount of paper. A negative one would grow the
    // printable area past the label, which is the one thing it can never do.
    if (out.deadLeftUm < 0) out.deadLeftUm = 0;
    if (out.deadTopUm  < 0) out.deadTopUm  = 0;

    return out.dpi > 0 && out.headDots > 0;
}

void PrinterManager::WriteJsonString(FILE* f, const char* value)
{
    // Same rule, and the same reason, as MediaManager::WriteJsonString: the
    // name is free-form off the wire, and a raw quote produces a file that
    // Load then misparses.
    fputc('"', f);
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p)
    {
        const unsigned char c = *p;
        if (c == '"' || c == '\\')       { fputc('\\', f); fputc(c, f); }
        else if (c >= 0x20 && c <= 0x7e) { fputc(c, f); }
        else                             { fputc('?', f); }
    }
    fputc('"', f);
}

bool PrinterManager::Save(const Printer& p) const
{
    char full[288];
    if (!PathFor(p.id, full, sizeof(full))) return false;

    FILE* f = fopen(full, "wb");
    if (!f) return false;

    fputs("{\"id\":", f);
    WriteJsonString(f, p.id);
    fputs(",\"name\":", f);
    WriteJsonString(f, p.name);
    fputs(",\"model\":", f);
    WriteJsonString(f, p.model);
    const int written = fprintf(f,
        ",\"dpi\":%lu,\"headDots\":%lu,"
        "\"deadLeftUm\":%ld,\"deadTopUm\":%ld,\"maxLines\":%lu}\n",
        (unsigned long)p.dpi, (unsigned long)p.headDots,
        (long)p.deadLeftUm, (long)p.deadTopUm, (unsigned long)p.maxLines);
    const bool streamOk = ferror(f) == 0;
    fclose(f);

    if (written <= 0 || !streamOk)
    {
        ESP_LOGE(TAG, "could not write %s", full);
        return false;
    }
    ESP_LOGI(TAG, "saved printer '%s': %lu dpi, %lu dots, dead %ld,%ld um",
             p.id, (unsigned long)p.dpi, (unsigned long)p.headDots,
             (long)p.deadLeftUm, (long)p.deadTopUm);
    return true;
}

void PrinterManager::Active(Printer& out) const
{
    char id[MAX_ID] = {};
    ActiveId(id, sizeof(id));

    if (id[0] && Load(id, out)) return;

    // No file, or one that will not parse. The built-in is what keeps a fresh
    // device printing, so this is the normal path and not an error path.
    BuiltIn(out);
    if (id[0] && strcmp(id, out.id) != 0)
        snprintf(out.id, sizeof(out.id), "%s", id);   // named but absent
}

// ──────────────────────────────────────────────────────────────
// Commands
// ──────────────────────────────────────────────────────────────

RequestError PrinterManager::Cmd_PrinterList(CommandContext& ctx)
{
    RETURN_IF_ERROR(ctx.readArgs());

    char activeId[MAX_ID] = {};
    ActiveId(activeId, sizeof(activeId));

    char dirPath[288];
    snprintf(dirPath, sizeof(dirPath), "%s%s",
             StorageManager::BASE_PATH, PRINTER_DIR);

    auto resp = ctx.reply.object();
    resp.field("active", activeId);
    {
        auto arr = resp.array("printers");

        bool sawBuiltIn = false;

        DIR* dir = opendir(dirPath);
        if (dir)
        {
            struct dirent* e;
            while ((e = readdir(dir)) != nullptr)
            {
                if (e->d_type == DT_DIR) continue;
                const char* dot = strrchr(e->d_name, '.');
                if (!dot || strcmp(dot, ".json") != 0) continue;

                char id[MAX_ID] = {};
                const size_t len = static_cast<size_t>(dot - e->d_name);
                if (len == 0 || len >= sizeof(id)) continue;
                memcpy(id, e->d_name, len);

                Printer p;
                if (!Load(id, p)) continue;
                if (strcmp(p.id, "dymo450") == 0) sawBuiltIn = true;

                auto item = arr.object();
                item.field("id", p.id);
                item.field("name", p.name);
                item.field("model", p.model);
                item.field("dpi", p.dpi);
                item.field("headDots", p.headDots);
                item.field("deadLeftUm", p.deadLeftUm);
                item.field("deadTopUm", p.deadTopUm);
                item.field("deadLeftDots", dots::FromUm(p.deadLeftUm, p.dpi));
                item.field("deadTopDots", dots::FromUm(p.deadTopUm, p.dpi));
                item.field("maxLines", p.maxLines);
                item.field("builtIn", false);
                item.field("active", strcmp(p.id, activeId) == 0);
            }
            closedir(dir);
        }

        // The compiled default is listed as an entry like any other, because a
        // caller asking what printers exist should not have to know that one of
        // them has no file.
        if (!sawBuiltIn)
        {
            Printer p;
            BuiltIn(p);
            auto item = arr.object();
            item.field("id", p.id);
            item.field("name", p.name);
            item.field("model", p.model);
            item.field("dpi", p.dpi);
            item.field("headDots", p.headDots);
            item.field("deadLeftUm", p.deadLeftUm);
            item.field("deadTopUm", p.deadTopUm);
            item.field("deadLeftDots", dots::FromUm(p.deadLeftUm, p.dpi));
            item.field("deadTopDots", dots::FromUm(p.deadTopUm, p.dpi));
            item.field("maxLines", p.maxLines);
            item.field("builtIn", true);
            item.field("active", strcmp(p.id, activeId) == 0);
        }
    }
    return RequestError::Ok;
}

RequestError PrinterManager::Cmd_PrinterGet(CommandContext& ctx)
{
    char id[MAX_ID] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Optional("id", id,
                 "Which printer to read. Defaults to the active one.")));

    Printer p;
    if (id[0])
    {
        if (!Load(id, p))
        {
            Printer def;
            BuiltIn(def);
            if (strcmp(id, def.id) == 0) p = def;
            else
            {
                auto bad = ctx.reply.object();
                bad.field("ok", false);
                bad.field("error", "no such printer - 'printer list' says what there is");
                return RequestError::Ok;
            }
        }
    }
    else
    {
        Active(p);
    }

    auto resp = ctx.reply.object();
    resp.field("ok", true);
    resp.field("id", p.id);
    resp.field("name", p.name);
    resp.field("model", p.model);
    resp.field("builtIn", p.builtIn);
    resp.field("dpi", p.dpi);
    resp.field("headDots", p.headDots);
    resp.field("maxLines", p.maxLines);
    resp.field("deadLeftUm", p.deadLeftUm);
    resp.field("deadTopUm", p.deadTopUm);
    resp.field("deadLeftDots", dots::FromUm(p.deadLeftUm, p.dpi));
    resp.field("deadTopDots", dots::FromUm(p.deadTopUm, p.dpi));
    return RequestError::Ok;
}

RequestError PrinterManager::Cmd_PrinterSet(CommandContext& ctx)
{
    static constexpr int32_t UNSET = INT32_MIN;

    char    id[MAX_ID]       = {};
    char    name[MAX_NAME]   = {};
    char    model[MAX_MODEL] = {};
    int32_t dpi        = UNSET;
    int32_t headDots   = UNSET;
    int32_t deadLeftUm = UNSET;
    int32_t deadTopUm  = UNSET;
    int32_t maxLines   = UNSET;

    RETURN_IF_ERROR(ctx.readArgs(
        Required("id", id,
                 "Identifier, and the filename in /printers. Letters, digits, "
                 "'_' and '-' only."),
        Optional("name", name,
                 "What a human calls it. Defaults to the id when creating."),
        Optional("model", model,
                 "The machine this describes, for a human reading the list."),
        Optional("dpi", dpi,
                 "Resolution in dots per inch. The LabelWriter 450 is 300, and "
                 "this is the number every micrometre on every medium is "
                 "converted through."),
        Optional("headDots", headDots,
                 "How many dots wide the print head is. 672 on the 450."),
        Optional("deadLeftUm", deadLeftUm,
                 "The strip along the LEFT edge, across the head, that this "
                 "machine cannot print on, in micrometres. MEASURED, never "
                 "guessed, and never negative. It shrinks the printable area of "
                 "every medium and is not how you move artwork - that is a "
                 "medium's alignXUm."),
        Optional("deadTopUm", deadTopUm,
                 "The strip along the LEADING edge that this machine cannot "
                 "print on, in micrometres - the printer starts emitting after "
                 "that much label has already passed the head. Measured, never "
                 "negative."),
        Optional("maxLines", maxLines,
                 "The longest job this machine will feed, in raster lines.")));

    Printer p;
    const bool existed = Load(id, p);
    if (!existed)
    {
        BuiltIn(p);                      // a new definition starts from the machine
        p.builtIn = false;
        snprintf(p.id, sizeof(p.id), "%s", id);
        snprintf(p.name, sizeof(p.name), "%s", id);
    }

    if (name[0])  snprintf(p.name,  sizeof(p.name),  "%s", name);
    if (model[0]) snprintf(p.model, sizeof(p.model), "%s", model);
    if (dpi        != UNSET) p.dpi        = static_cast<uint32_t>(dpi);
    if (headDots   != UNSET) p.headDots   = static_cast<uint32_t>(headDots);
    if (deadLeftUm != UNSET) p.deadLeftUm = deadLeftUm;
    if (deadTopUm  != UNSET) p.deadTopUm  = deadTopUm;
    if (maxLines   != UNSET) p.maxLines   = static_cast<uint32_t>(maxLines);

    const char* error = nullptr;
    if (!ValidId(id))                               error = "id must be letters, digits, '_' or '-'";
    else if (p.dpi == 0 || p.dpi > 2400)            error = "dpi must be 1 to 2400";
    else if (p.headDots == 0 || p.headDots > 8192)  error = "headDots must be 1 to 8192";
    else if (p.deadLeftUm < 0 || p.deadTopUm < 0)   error = "a dead zone is an amount of paper and cannot be negative";
    else if (p.maxLines == 0)                       error = "maxLines must be non-zero";
    else if (!Save(p))                              error = "could not write the printer file";

    auto resp = ctx.reply.object();
    resp.field("ok", error == nullptr);
    if (error) resp.field("error", error);
    else
    {
        resp.field("id", p.id);
        resp.field("created", !existed);
        resp.field("deadLeftDots", dots::FromUm(p.deadLeftUm, p.dpi));
        resp.field("deadTopDots", dots::FromUm(p.deadTopUm, p.dpi));
    }
    return RequestError::Ok;
}

RequestError PrinterManager::Cmd_PrinterDelete(CommandContext& ctx)
{
    char id[MAX_ID] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Required("id", id, "Which printer definition to forget.")));

    char full[288];
    const char* error = nullptr;
    if (!PathFor(id, full, sizeof(full))) error = "not a usable printer id";
    else if (remove(full) != 0)           error = "no such printer";

    auto resp = ctx.reply.object();
    resp.field("ok", error == nullptr);
    if (error) resp.field("error", error);
    else
    {
        Printer def;
        BuiltIn(def);
        resp.field("id", id);
        // Deleting the file for the built-in does not remove the printer, it
        // restores it, and a caller that is not told that will think it broke
        // something.
        resp.field("restoredToBuiltIn", strcmp(id, def.id) == 0);
    }
    return RequestError::Ok;
}

RequestError PrinterManager::Cmd_PrinterSelect(CommandContext& ctx)
{
    char id[MAX_ID] = {};
    RETURN_IF_ERROR(ctx.readArgs(
        Required("id", id,
                 "Which printer to use, as 'printer list' reports it.")));

    Printer p;
    Printer def;
    BuiltIn(def);

    const bool known = Load(id, p) || strcmp(id, def.id) == 0;

    const char* error = nullptr;
    if (!known)              error = "no such printer - 'printer list' says what there is";
    else if (!active_.Set(id)) error = "could not store the selection";

    auto resp = ctx.reply.object();
    resp.field("ok", error == nullptr);
    if (error) resp.field("error", error);
    else
    {
        Printer now;
        Active(now);
        resp.field("active", now.id);
        resp.field("dpi", now.dpi);
        resp.field("headDots", now.headDots);
    }
    return RequestError::Ok;
}
