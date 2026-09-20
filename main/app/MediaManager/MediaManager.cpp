#include "MediaManager.h"
#include "StruxProvider.h"
#include "CommandManager.h"
#include "StorageManager.h"
#include "PrintManager.h"
#include "PrinterManager/PrinterManager.h"
#include "DotGeometry.h"
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

    out.widthUm  = ExtractJsonInt(json, "widthUm",  0);
    out.heightUm = ExtractJsonInt(json, "heightUm", 0);

    out.alignXUm     = ExtractJsonInt(json, "alignXUm",     0);
    out.alignYUm     = ExtractJsonInt(json, "alignYUm",     0);
    out.marginLeftUm = ExtractJsonInt(json, "marginLeftUm", 0);
    out.marginTopUm  = ExtractJsonInt(json, "marginTopUm",  0);

    // A file written before the printer and the paper were told apart carries
    // offsetXUm/offsetYUm and none of the four above. Those offsets meant
    // placement AND crop at once, so they are kept as the finished placement
    // rather than reinterpreted - a roll somebody calibrated by printing labels
    // must not move because the firmware learned a new vocabulary. Saving the
    // medium again writes the new form and the flag goes.
    const bool hasNew = FindJsonField(json, "alignXUm")     || FindJsonField(json, "alignYUm") ||
                        FindJsonField(json, "marginLeftUm") || FindJsonField(json, "marginTopUm");
    const bool hasOld = FindJsonField(json, "offsetXUm")    || FindJsonField(json, "offsetYUm");

    out.legacyOffsets = !hasNew && hasOld;
    out.offsetXUm = ExtractJsonInt(json, "offsetXUm", 0);
    out.offsetYUm = ExtractJsonInt(json, "offsetYUm", 0);

    // A margin is an amount of paper; a negative one would grow the label.
    if (out.marginLeftUm < 0) out.marginLeftUm = 0;
    if (out.marginTopUm  < 0) out.marginTopUm  = 0;

    // A medium with no size is not a medium. Refusing here means every caller
    // downstream can divide by it.
    return out.widthUm > 0 && out.heightUm > 0;
}

void MediaManager::WriteJsonString(FILE* f, const char* value)
{
    // The id is validated to [A-Za-z0-9_-] and needs none of this; the NAME is
    // a free-form string off the wire and needs all of it. Both go through here
    // so there is one rule rather than two, and no second place to forget.
    //
    // A quote or a backslash written raw produced a file that Load then
    // misparsed - widthUm came back 0, the medium was refused as sizeless, and
    // it stayed unprintable until somebody deleted it. Anything outside
    // printable ASCII becomes '?' rather than being escaped: the file must be
    // valid UTF-8 because `media list` copies the name straight onto the wire,
    // and a name off the wire carries no encoding guarantee. Same call, and the
    // same reason, as CopyStringDesc in UsbHostManager.
    fputc('"', f);
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(value); *p; ++p)
    {
        const unsigned char c = *p;
        if (c == '"' || c == '\\')      { fputc('\\', f); fputc(c, f); }
        else if (c >= 0x20 && c <= 0x7e) { fputc(c, f); }
        else                             { fputc('?', f); }
    }
    fputc('"', f);
}

bool MediaManager::Save(const Medium& m) const
{
    char full[288];
    if (!PathFor(m.id, full, sizeof(full))) return false;

    FILE* f = fopen(full, "wb");
    if (!f) return false;

    // Written by hand rather than through JsonWriter: that class writes to a
    // Stream for the log broadcast, and a file is not one. Six fields do not
    // justify an adapter - but the two strings do need escaping, which is what
    // WriteJsonString is for.
    fputs("{\"id\":", f);
    WriteJsonString(f, m.id);
    fputs(",\"name\":", f);
    WriteJsonString(f, m.name);
    // Always the new form: writing a medium is what migrates it, and a file
    // that carried the old offsets loses them here rather than keeping two
    // notions of placement side by side.
    const int written = fprintf(f,
        ",\"widthUm\":%ld,\"heightUm\":%ld,"
        "\"alignXUm\":%ld,\"alignYUm\":%ld,"
        "\"marginLeftUm\":%ld,\"marginTopUm\":%ld}\n",
        (long)m.widthUm, (long)m.heightUm,
        (long)m.alignXUm, (long)m.alignYUm,
        (long)m.marginLeftUm, (long)m.marginTopUm);
    const bool streamOk = ferror(f) == 0;
    fclose(f);

    if (written <= 0 || !streamOk)
    {
        ESP_LOGE(TAG, "could not write %s", full);
        return false;
    }
    ESP_LOGI(TAG, "saved '%s': %ld x %ld um, align %ld,%ld um, margin %ld,%ld um",
             m.id, (long)m.widthUm, (long)m.heightUm,
             (long)m.alignXUm, (long)m.alignYUm,
             (long)m.marginLeftUm, (long)m.marginTopUm);
    return true;
}

void MediaManager::Geometry(const Medium& m, dots::LabelGeometry& out) const
{
    PrinterManager::Printer p;
    app_.getPrinterManager().Active(p);
    dots::Resolve(m.spec(), p.deadLeftUm, p.deadTopUm, p.dpi, out);
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

    // The machine, reported once rather than per medium: every dot figure
    // below is converted through it, and the dead zone is the printer's.
    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    const uint32_t dpi = pr.dpi;
    resp.field("dpi", dpi);
    resp.field("headDots", pr.headDots);
    resp.field("printer", pr.id);
    resp.field("deadLeftDots", dots::FromUm(pr.deadLeftUm, dpi));
    resp.field("deadTopDots", dots::FromUm(pr.deadTopUm, dpi));

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

            dots::LabelGeometry g;
            Geometry(m, g);

            auto item = arr.object();
            item.field("id", m.id);
            item.field("name", m.name);
            item.field("widthUm", m.widthUm);
            item.field("heightUm", m.heightUm);
            item.field("alignXUm", m.alignXUm);
            item.field("alignYUm", m.alignYUm);
            item.field("marginLeftUm", m.marginLeftUm);
            item.field("marginTopUm", m.marginTopUm);
            item.field("legacyOffsets", m.legacyOffsets);
            item.field("widthDots", g.widthDots);
            item.field("heightDots", g.heightDots);
            item.field("printableWidthDots",  g.printableWidthDots);
            item.field("printableHeightDots", g.printableHeightDots);
            item.field("placeXDots", g.placeXDots);
            item.field("placeYDots", g.placeYDots);
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

    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    const uint32_t dpi = pr.dpi;

    dots::LabelGeometry g;
    Geometry(m, g);

    resp.field("ok", true);
    resp.field("id", m.id);
    resp.field("name", m.name);
    resp.field("widthUm", m.widthUm);
    resp.field("heightUm", m.heightUm);

    // Placement, which is a preference.
    resp.field("alignXUm", m.alignXUm);
    resp.field("alignYUm", m.alignYUm);

    // Unreachable paper, which is not. The machine's share is on the printer;
    // only this roll's own extra is here.
    resp.field("marginLeftUm", m.marginLeftUm);
    resp.field("marginTopUm", m.marginTopUm);

    // True while this medium still carries the pre-split offsets, which are
    // being honoured as its placement. Write it once with 'media set' to move
    // it over; nothing about where it prints changes when you do.
    resp.field("legacyOffsets", m.legacyOffsets);
    if (m.legacyOffsets)
    {
        resp.field("offsetXUm", m.offsetXUm);
        resp.field("offsetYUm", m.offsetYUm);
    }

    // What it works out to on THIS printer. The dots are derived, never stored:
    // the paper does not change when the printer does.
    resp.field("printer", pr.id);
    resp.field("dpi", dpi);
    resp.field("headDots", pr.headDots);
    resp.field("widthDots", g.widthDots);
    resp.field("heightDots", g.heightDots);

    // The dead zone, split by whose fault it is.
    resp.field("deadLeftDots", g.deadLeftDots);
    resp.field("deadTopDots", g.deadTopDots);
    resp.field("marginLeftDots", g.marginLeftDots);
    resp.field("marginTopDots", g.marginTopDots);

    // Where the artwork's top-left lands in head coordinates. Negative means
    // that much of it falls on paper the machine cannot reach.
    resp.field("placeXDots", g.placeXDots);
    resp.field("placeYDots", g.placeYDots);

    // What a design can actually use: size minus the machine's dead zone minus
    // this roll's own margin. Alignment is not in it, and moving the artwork
    // will not change it.
    resp.field("printableWidthDots",  g.printableWidthDots);
    resp.field("printableHeightDots", g.printableHeightDots);
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
    int32_t widthUm      = UNSET;
    int32_t heightUm     = UNSET;
    int32_t alignXUm     = UNSET;
    int32_t alignYUm     = UNSET;
    int32_t marginLeftUm = UNSET;
    int32_t marginTopUm  = UNSET;
    int32_t offsetXUm    = UNSET;     // accepted for callers written earlier
    int32_t offsetYUm    = UNSET;

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
        Optional("alignXUm",  alignXUm,
                 "ALIGNMENT across the head, in micrometres: how far to move "
                 "the artwork from where it would otherwise land. Signed, and "
                 "it does NOT change how much of the label can be printed. "
                 "Zero puts the design at the first dot the machine can reach, "
                 "which is the right value until a printed grid says otherwise."),
        Optional("alignYUm",  alignYUm,
                 "ALIGNMENT along the feed, in micrometres. Positive moves the "
                 "artwork further down the label. Signed, and it does not "
                 "change the printable area either."),
        Optional("marginLeftUm", marginLeftUm,
                 "Extra unreachable paper along this roll's LEFT edge, in "
                 "micrometres, BEYOND what the machine itself cannot reach. "
                 "Non-negative. Most rolls want 0: the printer's own dead zone "
                 "is on the printer ('printer get') and is already subtracted. "
                 "Use this only when a particular stock sits further into the "
                 "guide than the rest."),
        Optional("marginTopUm", marginTopUm,
                 "Extra unreachable paper along this roll's LEADING edge, "
                 "beyond the machine's own. Non-negative, and 0 for most rolls."),
        Optional("offsetXUm", offsetXUm,
                 "DEPRECATED, and kept so older callers still work: the "
                 "pre-split offset that meant placement and crop at once. It is "
                 "taken as alignXUm. Prefer alignXUm and marginLeftUm, which "
                 "say which of the two you meant."),
        Optional("offsetYUm", offsetYUm,
                 "DEPRECATED. Taken as alignYUm; prefer alignYUm and "
                 "marginTopUm.")
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
    if (name[0])              snprintf(m.name, sizeof(m.name), "%s", name);
    if (widthUm      != UNSET) m.widthUm      = widthUm;
    if (heightUm     != UNSET) m.heightUm     = heightUm;
    if (alignXUm     != UNSET) m.alignXUm     = alignXUm;
    if (alignYUm     != UNSET) m.alignYUm     = alignYUm;
    if (marginLeftUm != UNSET) m.marginLeftUm = marginLeftUm;
    if (marginTopUm  != UNSET) m.marginTopUm  = marginTopUm;

    // An old offset is CONVERTED, never copied. It named the finished
    // placement - the head column the label's edge sat at - whereas alignment
    // is measured from the first dot the machine can reach. Those differ by
    // exactly the dead zone, so
    //
    //     align = oldOffset + printerDead + mediumMargin
    //
    // and -1016 um of old offset on a machine that loses 1016 um becomes an
    // alignment of zero: the artwork was never being nudged, it was being
    // cropped. Copying the number across instead would subtract the dead zone
    // twice and shift every calibrated roll by that much.
    PrinterManager::Printer pr;
    app_.getPrinterManager().Active(pr);
    const int32_t lostXUm = pr.deadLeftUm + m.marginLeftUm;
    const int32_t lostYUm = pr.deadTopUm  + m.marginTopUm;

    if (alignXUm == UNSET && offsetXUm != UNSET) m.alignXUm = offsetXUm + lostXUm;
    if (alignYUm == UNSET && offsetYUm != UNSET) m.alignYUm = offsetYUm + lostYUm;

    // Writing a medium is what migrates it: from here on its placement is
    // alignXUm/alignYUm. Without this a legacy file edited through this command
    // would go on honouring offsets the caller can no longer see or change.
    if (m.legacyOffsets)
    {
        if (alignXUm == UNSET && offsetXUm == UNSET) m.alignXUm = m.offsetXUm + lostXUm;
        if (alignYUm == UNSET && offsetYUm == UNSET) m.alignYUm = m.offsetYUm + lostYUm;
        m.legacyOffsets = false;
        m.offsetXUm = m.offsetYUm = 0;
    }

    if (m.marginLeftUm < 0) m.marginLeftUm = 0;
    if (m.marginTopUm  < 0) m.marginTopUm  = 0;

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

    PrinterManager::Printer prSet;
    app_.getPrinterManager().Active(prSet);
    const uint32_t dpi = prSet.dpi;
    resp.field("ok", true);
    resp.field("created", !existed);
    resp.field("id", m.id);
    resp.field("name", m.name);
    resp.field("widthUm", m.widthUm);
    resp.field("heightUm", m.heightUm);
    resp.field("alignXUm", m.alignXUm);
    resp.field("alignYUm", m.alignYUm);
    resp.field("marginLeftUm", m.marginLeftUm);
    resp.field("marginTopUm", m.marginTopUm);

    // Echoing the geometry back is what lets a caller see, in one round trip,
    // that an alignment it just changed moved the artwork and left the
    // printable area alone.
    dots::LabelGeometry g;
    Geometry(m, g);
    resp.field("dpi", dpi);
    resp.field("widthDots", g.widthDots);
    resp.field("heightDots", g.heightDots);
    resp.field("placeXDots", g.placeXDots);
    resp.field("placeYDots", g.placeYDots);
    resp.field("printableWidthDots",  g.printableWidthDots);
    resp.field("printableHeightDots", g.printableHeightDots);
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
