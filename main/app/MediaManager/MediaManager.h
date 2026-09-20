#pragma once

#include "AppProvider.h"
#include "InitState.h"
#include "CommandEntry.h"
#include "DotGeometry.h"
#include <cstdint>
#include <cstddef>
#include <cstdio>

// ──────────────────────────────────────────────────────────────
// MediaManager — what a physical label IS, as data on the device.
//
// A medium is a file in /media, one JSON object per roll, created and edited at
// runtime. Nothing here is compiled in: "I bought 36 x 89 mm labels" is a
// `media set`, not a firmware release. That is the whole point of the layer.
//
// ── What belongs in a medium, and what does not ──
//
// A medium describes the PAPER. It does not describe the printer. This
// LabelWriter's 300 DPI and its 672-dot head are facts about the machine, they
// are the same for every roll anyone loads, and they live in PrintManager. A
// `dpi` field here would be a second place for one truth and would be wrong the
// first time someone typed 600 into it.
//
// So the schema is deliberately five things and no more:
//
//   name           what a human calls it
//   widthUm        across the head
//   heightUm       along the feed
//   offsetXUm      where the label's left edge is, in head coordinates
//   offsetYUm      where the label's top edge is, in raster coordinates
//
// Dimensions are MICROMETRES, integers. A label is specified in whole tenths of
// a millimetre by everyone who sells one, micrometres hold that exactly, and the
// alternative was putting the protocol's first floating-point argument into the
// wire format to express 25.4.
//
// ── The offsets, which are the calibrated part ──
//
// Both are positions, not corrections, and both are SIGNED:
//
//   offsetX  the head column the label's left edge sits at. The head is wider
//            than any label, so where the paper sits inside it is a property of
//            the roll and the guide, not of the design.
//
//   offsetY  the raster line the label's top edge sits at. Positive means the
//            printer starts emitting before the paper arrives, so the design is
//            preceded by blank lines. NEGATIVE means raster line 0 already lands
//            inside the label - the printer starts late - and the top of the
//            design is cropped by that much. The old C# configuration carried
//            -5 mm for every roll it knew, which is why this is signed at all.
//
// Given a medium, printing needs no geometry from the caller:
//
//   dots = micrometres * DPI / 25400
//
// and the design is rendered at the label's dot size and placed at (offsetX,
// offsetY). A caller that knows nothing about printers can name a roll.
// ──────────────────────────────────────────────────────────────

class MediaManager
{
    static constexpr const char* TAG = "MediaManager";

public:
    /// The directory StorageManager creates, as it appears on the wire.
    // Named MEDIA_DIR and not DIR: inside this class a member called DIR
    // shadows POSIX's own DIR type, and opendir stops compiling for a
    // reason that reads nothing like the cause.
    static constexpr const char* MEDIA_DIR = "/media";

    static constexpr size_t MAX_ID   = 32;
    static constexpr size_t MAX_NAME = 48;

    /// One roll. Plain data - the manager reads and writes it, nothing owns it.
    struct Medium
    {
        char    id[MAX_ID]     = {};
        char    name[MAX_NAME] = {};
        int32_t widthUm   = 0;
        int32_t heightUm  = 0;

        /// Where the artwork goes, and NOTHING else. Signed, measured by eye
        /// against a printed grid. Zero means "at the first dot the machine can
        /// reach", which is the honest state of an uncalibrated roll.
        int32_t alignXUm = 0;
        int32_t alignYUm = 0;

        /// What THIS roll loses beyond what the machine loses - paper sitting
        /// off-centre in the guide. Non-negative; the machine's own dead zone
        /// is on the printer and is not repeated here.
        int32_t marginLeftUm = 0;
        int32_t marginTopUm  = 0;

        /// Set when the file still carries the pre-split `offsetXUm`/`offsetYUm`,
        /// which meant placement AND crop at once. Those are then used as the
        /// finished placement, verbatim, so a calibrated roll does not move the
        /// day the firmware changes. Writing the medium again clears it.
        bool    legacyOffsets = false;
        int32_t offsetXUm = 0;
        int32_t offsetYUm = 0;

        /// This medium as the pure geometry model wants it.
        dots::LabelSpec spec() const
        {
            dots::LabelSpec s;
            s.widthUm         = widthUm;
            s.heightUm        = heightUm;
            s.marginLeftUm    = marginLeftUm;
            s.marginTopUm     = marginTopUm;
            s.alignXUm        = alignXUm;
            s.alignYUm        = alignYUm;
            s.legacy          = legacyOffsets;
            s.legacyOffsetXUm = offsetXUm;
            s.legacyOffsetYUm = offsetYUm;
            return s;
        }
    };

    explicit MediaManager(AppProvider& app);

    MediaManager(const MediaManager&) = delete;
    MediaManager& operator=(const MediaManager&) = delete;
    MediaManager(MediaManager&&) = delete;
    MediaManager& operator=(MediaManager&&) = delete;

    void Init();

    /// Read one medium by id. False when it is not there or will not parse.
    bool Load(const char* id, Medium& out) const;

    /// Micrometres to printer dots, rounded to nearest. The DPI is the
    /// printer's and is passed in, because this layer does not own one.
    static int32_t UmToDots(int32_t um, uint32_t dpi)
    {
        return dots::FromUm(um, dpi);
    }

    /// This medium's geometry on a given machine: which dots exist, which can
    /// carry ink, and where the artwork lands. One function so that `media
    /// get`, the preview and a real print cannot disagree.
    ///
    /// Replaces the old PrintableDots, which took the medium's single signed
    /// offset and read a negative one as unreachable paper - the conflation
    /// this whole split exists to remove. The dead zone belongs to the printer
    /// and arrives here from it.
    void Geometry(const Medium& m, dots::LabelGeometry& out) const;

private:
    AppProvider& app_;
    InitState    initState_;

    /// The VFS path of a medium's file, or false if the id is not usable as a
    /// filename. Ids are restricted rather than escaped: a medium id ends up in
    /// a path, and refusing '/' and '.' is cheaper to reason about than
    /// normalising them.
    static bool PathFor(const char* id, char* out, size_t cap);
    static bool ValidId(const char* id);

    /// Write one JSON string literal, quotes included, with the escaping a
    /// free-form name needs.
    static void WriteJsonString(FILE* f, const char* value);

    bool Save(const Medium& m) const;

    // ── Commands ──
    RequestError Cmd_MediaList(CommandContext& ctx);
    RequestError Cmd_MediaGet(CommandContext& ctx);
    RequestError Cmd_MediaSet(CommandContext& ctx);
    RequestError Cmd_MediaDelete(CommandContext& ctx);

    inline static CommandEntry commands_[] = {
        { "media", "list",   &InvokeCommand<&MediaManager::Cmd_MediaList>,
          "List the label stock this device knows about. A medium describes the "
          "PAPER - size and where it sits under the print head - and never the "
          "printer, whose resolution and head width are fixed hardware facts." },
        { "media", "get",    &InvokeCommand<&MediaManager::Cmd_MediaGet>,
          "Read one medium, including the dot geometry it works out to on this "
          "printer, so a caller can see what 'print svg -media <id>' will do "
          "before doing it." },
        { "media", "set",    &InvokeCommand<&MediaManager::Cmd_MediaSet>,
          "Create or update a medium. This is how new label stock is added - "
          "from dimensions off the box, at runtime, with no firmware change. "
          "Updating an existing one leaves out what you do not want to change. "
          "The DIMENSIONS come off the box; the OFFSETS do not follow from them "
          "and must be measured with 'print calibrate'. Leave them out rather "
          "than guess - zero is honest, a guess is wrong and looks deliberate." },
        { "media", "delete", &InvokeCommand<&MediaManager::Cmd_MediaDelete>,
          "Forget a medium. The label designs in /labels are untouched; they are "
          "not tied to any particular stock." },
    };
};
