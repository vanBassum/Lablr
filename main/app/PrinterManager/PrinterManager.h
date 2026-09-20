#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "TypedSettings.h"
#include <cstdint>
#include <cstddef>

// ──────────────────────────────────────────────────────────────
// What the MACHINE is, as against what the paper is.
//
// Three things used to be tangled into one pair of numbers on a medium, and
// pulling them apart is what this manager exists for:
//
//   the PRINTER   resolution, head width, and the strip at each edge the
//                 mechanism physically cannot reach. Same for every roll
//                 anyone loads. That is here.
//   the MEDIUM    how big the label is, plus any FURTHER margin this
//                 particular stock loses by sitting off-centre in the guide.
//                 That is MediaManager.
//   the ALIGNMENT where the artwork is placed. A preference, calibrated by
//                 eye, and it must never change how much of the label can be
//                 printed. Also MediaManager, as alignXUm/alignYUm.
//
//   printable = media size - printer dead zone - medium margin
//
// and alignment appears nowhere in it. Conflating the first and the third is
// what the old single `offsetYUm` did: nudging the artwork down silently
// reported a smaller printable label, because one signed number was being read
// as both "put it here" and "this much is unreachable".
//
// ── Why a directory, when there is one printer ──
//
// /printers holds one JSON file per printer, exactly as /media holds one per
// roll, so a second model is a file rather than a firmware change. Today there
// is one entry and it is COMPILED IN: a device with an empty /printers still
// prints, because Active() falls back to the built-in DYMO LabelWriter 450.
// Nothing has to be seeded for the device to work, and writing a file is how
// you override rather than how you begin.
//
// ── Where the built-in dead zone came from ──
//
// The 25 x 25 mm stock was measured at -1016 um across the head and -3133 um
// along the feed, and until now those lived on the medium as offsets that both
// placed the artwork AND cropped it. Seeding the printer with exactly those
// numbers is what makes the split invisible: square25 still reports 283 x 258
// printable dots of its 295 x 295, and still prints in the same place.
//
// Whether the feed figure is really a constant of the machine rather than of
// that roll is the open question (see docs/next-up.md). This model is the
// claim that it is - and it is now a claim that can be tested, because a second
// roll that needs a different figure says so in its own marginTopUm instead of
// silently disagreeing with the first.
// ──────────────────────────────────────────────────────────────

class PrinterManager
{
    static constexpr const char* TAG = "PrinterManager";

public:
    /// The directory StorageManager creates, as it appears on the wire.
    static constexpr const char* PRINTER_DIR = "/printers";

    static constexpr size_t MAX_ID    = 32;
    static constexpr size_t MAX_NAME  = 48;
    static constexpr size_t MAX_MODEL = 48;

    /// One machine. Plain data, like MediaManager::Medium.
    struct Printer
    {
        char     id[MAX_ID]       = {};
        char     name[MAX_NAME]   = {};
        char     model[MAX_MODEL] = {};
        uint32_t dpi        = 0;
        uint32_t headDots   = 0;
        /// The strip the mechanism cannot reach, in micrometres. Non-negative:
        /// it is an amount of paper, not a direction.
        int32_t  deadLeftUm = 0;
        int32_t  deadTopUm  = 0;
        uint32_t maxLines   = 0;
        /// True when this came from the compiled default rather than a file,
        /// which is what `printer list` shows so nobody hunts for the file.
        bool     builtIn    = false;
    };

    /// The one printer this firmware has a driver for. A device with no files
    /// uses this, so the product works out of the box and /printers is an
    /// override rather than a prerequisite.
    static void BuiltIn(Printer& out);

    explicit PrinterManager(AppProvider& app);

    PrinterManager(const PrinterManager&) = delete;
    PrinterManager& operator=(const PrinterManager&) = delete;
    PrinterManager(PrinterManager&&) = delete;
    PrinterManager& operator=(PrinterManager&&) = delete;

    void Init();

    /// Read one printer by id. False when it is not there or will not parse.
    bool Load(const char* id, Printer& out) const;
    bool Save(const Printer& p) const;

    /// The printer everything else should use: the file named by the
    /// `print.printer` setting, or the built-in when there is no such file.
    /// Never fails - a device that cannot read its printer still has one.
    void Active(Printer& out) const;

    /// The active printer's id, as the setting holds it.
    static void ActiveId(char* out, size_t cap) { active_.Get(out, cap); }

private:
    AppProvider& app_;
    InitState    initState_;

    inline static StringSetting active_{ "print.printer", "Active printer",
                                         "dymo450" };

    static bool ValidId(const char* id);
    bool PathFor(const char* id, char* out, size_t cap) const;
    static void WriteJsonString(FILE* f, const char* value);

    RequestError Cmd_PrinterList(CommandContext& ctx);
    RequestError Cmd_PrinterGet(CommandContext& ctx);
    RequestError Cmd_PrinterSet(CommandContext& ctx);
    RequestError Cmd_PrinterDelete(CommandContext& ctx);
    RequestError Cmd_PrinterSelect(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "printer", "list",   &InvokeCommand<&PrinterManager::Cmd_PrinterList>,
          "List the printers this device knows about. A printer describes the "
          "MACHINE - resolution, head width, and the strip at each edge it "
          "physically cannot print on - and never the paper, whose size and "
          "margins belong to a medium. One entry is built in and needs no file; "
          "'active' marks the one in use." },
        { "printer", "get",    &InvokeCommand<&PrinterManager::Cmd_PrinterGet>,
          "Read one printer, in micrometres and in dots. The dead zone here is "
          "what makes a label's printable area smaller than the label; see "
          "'media get', which subtracts it." },
        { "printer", "set",    &InvokeCommand<&PrinterManager::Cmd_PrinterSet>,
          "Create or update a printer. The DEAD ZONE is measured, not guessed: "
          "print a calibration grid and read how much of the label's leading "
          "and left edges never receives ink. It belongs to the machine, so "
          "measure it once - a roll that loses MORE than this gets the extra on "
          "its own medium, as marginLeftUm/marginTopUm." },
        { "printer", "delete", &InvokeCommand<&PrinterManager::Cmd_PrinterDelete>,
          "Forget a printer definition. The built-in one cannot be deleted; "
          "deleting the file for it simply returns it to its compiled values." },
        { "printer", "select", &InvokeCommand<&PrinterManager::Cmd_PrinterSelect>,
          "Choose which printer definition is in use. Persists across reboots. "
          "Everything that converts micrometres to dots - every medium, every "
          "print - follows this." },
    };
};
