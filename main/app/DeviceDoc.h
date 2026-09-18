#pragma once

// ──────────────────────────────────────────────────────────────
// What this product is, and how it is meant to be driven - in the product's own
// words, in the application layer, beside the managers the words are about.
//
// It lives here and not in the relay for one reason: whatever answers "how do I
// use this device" has to be the firmware that is actually running. A copy kept
// server-side is a copy that describes last month's build, and the one caller
// who cannot notice is the one most likely to act on it.
//
// Registered into the framework from AppContext::Init() (see
// SystemManager::SetDocumentation). Strux never reaches up for it.
//
//   DESCRIPTION   one line. It rides in the relay hello, so it is in a device
//                 list before anything asks the device a question.
//   INSTRUCTIONS  free-form, served on request (`system describe`). Prose and
//                 NOT a schema: every command already declares its own name,
//                 arguments and types through `help describe`. This is for what
//                 never fits in a declaration - which commands belong together,
//                 what order they go in, and what will not work.
//
// ASCII ONLY in these literals. GCC's execution charset follows the build
// host's locale, so a UTF-8 dash here leaves this machine as one cp1252 byte -
// invalid UTF-8 in the JSON reply, and invisible in review.
// ──────────────────────────────────────────────────────────────
namespace DeviceDoc {

inline constexpr const char* DESCRIPTION =
    "Lablr - an ESP32-S3 label device that renders SVG label designs stored on "
    "its own filesystem.";

inline constexpr const char* INSTRUCTIONS =
    "Lablr renders labels. A label IS an SVG file: there is no template "
    "language, no layout DSL and no field substitution. A label that says "
    "VANILLE contains the text VANILLE, and its dates, quantities and "
    "percentages are in the document too. Making a new label means writing a "
    "new SVG and storing it here.\n"
    "\n"
    "Discovering what this device can do\n"
    "  Every command declares itself. `help describe` returns every category, "
    "every command and each command's arguments with types and descriptions, in "
    "one reply. Nothing about a command is written down twice, so what it "
    "returns is always this exact build.\n"
    "\n"
    "The filesystem\n"
    "  A FAT partition with three directories, reached through the `fs` "
    "commands. Paths are rooted at the filesystem, not at the device: "
    "'/labels/vanilla.svg'.\n"
    "    /labels  the label designs, one SVG each.\n"
    "    /fonts   TrueType fonts that labels may use.\n"
    "    /media   reserved for physical label-media definitions. Empty for now; "
    "sizes are given to the render command explicitly instead.\n"
    "  `fs list` shows a directory, `fs read` returns a file, `fs write` "
    "creates or replaces one, `fs delete` removes one, and `fs info` reports "
    "free space.\n"
    "\n"
    "File contents are NOT arguments\n"
    "  An argument list is capped at 512 bytes and a single value at 192, so an "
    "SVG does not fit in one and is not meant to. `fs write` takes the bytes as "
    "the request BODY, after the envelope line, in the same session. `fs read` "
    "and `render svg` reply the same way in reverse: a JSON header line, a "
    "newline, then raw bytes.\n"
    "\n"
    "Writing a label\n"
    "  Read an existing label with `fs read` first and use it as a style "
    "reference - it shows the size, the margins and the fonts that are known to "
    "work on this device. Then write yours with `fs write`, render it, look at "
    "it, and adjust. A label is ordinary SVG: shapes, paths, strokes, fills and "
    "text.\n"
    "\n"
    "Fonts, which are the part that surprises people\n"
    "  `render fonts` lists the fonts this device has registered. An SVG's "
    "font-family must match one of those names EXACTLY. A name comes from the "
    "filename in /fonts with the extension removed, so /fonts/DejaVuSans.ttf is "
    "font-family=\"DejaVuSans\". Nothing normalises case or spacing, there is no "
    "fallback font, and text whose font-family matches nothing renders as "
    "NOTHING AT ALL - the shapes around it still appear, so the label looks "
    "right except that the words are missing. If text does not show up, check "
    "`render fonts` first. Adding a font is `fs write` to /fonts followed by a "
    "reboot; fonts are read once at startup.\n"
    "\n"
    "Rendering a preview\n"
    "  `render svg -path /labels/x.svg -width 400 -height 200` renders WITHOUT "
    "printing and returns the bitmap. The reply header gives width, height, "
    "format, stride and bytes, then the pixels follow after a newline. The "
    "format is ARGB8888S: one 32-bit little-endian word per pixel, so the bytes "
    "are blue, green, red, alpha, and alpha is not premultiplied. The SVG is "
    "scaled to fit the requested box with its aspect ratio preserved and "
    "centred; the header's 'scale' says by how much. The background defaults to "
    "opaque white because a label is printed on white paper.\n"
    "  Rendering is how you check your work. There is no other way to see what "
    "a label looks like, and a label that parses is not necessarily a label "
    "that reads well.\n"
    "\n"
    "Media - what the paper is\n"
    "  /labels holds SVG DESIGNS. /media holds definitions of physical LABEL "
    "STOCK: how big a roll is, and where it sits under the print head. The two are "
    "independent - a design is not tied to any particular stock, and a medium "
    "knows nothing about any design.\n"
    "  `media list` shows what stock this device knows, `media get` reads one, "
    "`media set` creates or updates one and `media delete` forgets one. Media are "
    "DATA, written at runtime: when a user says they bought labels that are 36 by "
    "89 millimetres, that is one `media set` call and not a firmware change. Sizes "
    "are in MICROMETRES, so 36 mm is 36000.\n"
    "  A medium describes the paper and NEVER the printer. Resolution and print "
    "head width belong to the machine - they are the same for every roll anyone "
    "loads - and `print status` reports them. Do not put a dpi field on a medium "
    "and do not ask a user for one.\n"
    "  offsetXUm and offsetYUm are CALIBRATION: measured on paper, not preferences. "
    "They say where the label sits relative to the head. offsetY is negative when "
    "the printer starts printing after the leading edge of the label has passed, "
    "which crops that much off the top of a design. `print calibrate` prints the "
    "ruled grid these are read off. Never invent them - a new medium with zero "
    "offsets is honest and prints slightly off, while a guessed offset is wrong "
    "and looks deliberate.\n"
    ""
    "Designs and media are loosely coupled\n"
    "  Printing an SVG on a medium scales the design to the dot size of that "
    "medium, preserving aspect ratio, the same way `render svg` fits a box. So a "
    "design CAN be printed on stock it was not drawn for.\n"
    "  That is worth doing when the shapes are similar. When the aspect ratios "
    "differ substantially - a square design on a long thin roll - scaling leaves "
    "large empty margins and tiny text, and the right answer is a NEW SVG laid out "
    "for that shape rather than the old one squeezed into it. Render it and look "
    "before deciding.\n"
    ""
    "Printing\n"
    "  `print svg -path /labels/x.svg -media <id>` renders the design and prints "
    "it. The medium supplies the size and the calibration, so that is normally the "
    "whole call - do not compute dots.\n"
    "  -width and -height in printer dots, and -offsetX and -offsetY, override the "
    "medium. They exist FOR CALIBRATION: printing one design at several offsets to "
    "find the right one. A settled value belongs in the medium via `media set`, "
    "not in every print call.\n"
    "  Check `print status` first. When nothing is attached, printing fails and "
    "says so; do not report a success you did not get. A reply carrying a "
    "`warning` field means the label came out BLANK, which is almost always an SVG "
    "naming a font-family the device does not have - compare it against "
    "`render fonts`.\n"
    "  `print test` prints a striped pattern and `print calibrate` prints a "
    "measuring grid. Neither uses the filesystem, the renderer or a medium, so "
    "they tell a printer problem from a label problem.\n"
    ""
    "Other things worth knowing\n"
    "  * `system reboot` answers first and then restarts about half a second "
    "later, so every connection drops and anything in flight is lost. It is "
    "also what makes a newly uploaded font take effect.\n"
    "  * The `partition` commands write flash and are for firmware updates. "
    "They are not the label filesystem; `fs` is.\n"
    "  * Numbers in arguments are unsigned 32-bit. There are no floating-point "
    "arguments anywhere in the protocol, which is why a render size is in whole "
    "pixels.";

} // namespace DeviceDoc
