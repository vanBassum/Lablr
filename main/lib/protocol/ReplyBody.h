#pragma once

#include "ReplyWriter.h"
#include "Stream.h"
#include <cstdint>
#include <cstring>

// How a reply carries RAW BYTES as well as structure, and how a caller far away
// is told what those bytes are.
//
// The shape is not new — `web read` has always answered with a header record, a
// newline, and then the file:
//
//     {"ok":true,"status":200,"contentType":"text/html"}\n<bytes…>
//
// What was missing is that it was a shape three handlers happened to share rather
// than something the framework named, so nothing above the device could act on it
// generically. A reader had to know which command it had called to know whether a
// reply had a body at all, and what the body was — which is exactly the knowledge
// the relay's MCP surface must not have (it dispatches on media type, never on a
// command's name).
//
// So the convention is written down here, in one place, and it is the whole
// mechanism:
//
//   * A reply with a body opens with ONE header record, closed by a newline.
//   * The header declares `contentType`, an ordinary IANA media type.
//   * Everything after that first newline is the body, verbatim.
//   * A reply WITHOUT `contentType` has no body. It is JSON and nothing else,
//     which is what every ordinary command already answers and why this is
//     backwards compatible in both directions: old firmware declares nothing and
//     is read as text, and a reader that has never heard of `contentType` sees
//     the replies it always saw.
//
// Usage — note the braces, which are load-bearing. The header scope must CLOSE
// before the newline, because a reply carrying a body is not one document:
//
//     {
//         auto head = ctx.reply.object();
//         head.field("ok", true);
//         protocol::declareBody(head, "image/png", size);
//     }
//     protocol::endHeader(ctx.out);
//     ctx.out.write(bytes, size);
//
// The body still streams. Nothing here buffers it, holds it or looks at it —
// these are two fields and a newline, and the handoff below them is the same
// zero-copy lend it always was.
namespace protocol
{
    /// The media type of the bytes after the header line. Its PRESENCE is what
    /// says a reply has a body at all, so a handler that writes raw bytes owes a
    /// call to this, and one that does not must never make it.
    inline constexpr const char* CONTENT_TYPE_FIELD = "contentType";

    /// How many body bytes follow. Optional: a reader can count them itself,
    /// since the body runs to the end of the reply. Worth declaring when it is
    /// known up front, because it lets a caller size a buffer before the bytes
    /// arrive, and lets one that got fewer say so.
    inline constexpr const char* CONTENT_LENGTH_FIELD = "contentLength";

    /// Declare the body that follows this header record.
    inline void declareBody(ReplyObject& head, const char* contentType)
    {
        head.field(CONTENT_TYPE_FIELD, contentType);
    }

    /// Declare the body and its length.
    inline void declareBody(ReplyObject& head, const char* contentType, uint32_t bytes)
    {
        head.field(CONTENT_TYPE_FIELD, contentType);
        head.field(CONTENT_LENGTH_FIELD, bytes);
    }

    /// End the header line. Call once the header scope has closed; the body's
    /// bytes go straight to `out` after it.
    inline void endHeader(Stream& out) { out.write("\n", 1); }

    /// A media type guessed from a file name's extension, for handlers serving
    /// files they did not create and hold no type for.
    ///
    /// Deliberately small and deliberately honest: the fallback is
    /// `application/octet-stream`, which is what "bytes I cannot describe" is
    /// called, and a caller downstream treats it as binary rather than guessing
    /// again. Extending the table is how a new kind of file becomes convenient to
    /// a reader; nothing breaks while it is missing.
    inline const char* mediaTypeForPath(const char* path)
    {
        const char* dot = strrchr(path, '.');
        if (!dot) return "application/octet-stream";

        struct Row { const char* ext; const char* type; };
        static constexpr Row table[] = {
            { ".svg",  "image/svg+xml" },
            { ".png",  "image/png" },
            { ".jpg",  "image/jpeg" },
            { ".jpeg", "image/jpeg" },
            { ".gif",  "image/gif" },
            { ".bmp",  "image/bmp" },
            { ".json", "application/json" },
            { ".txt",  "text/plain" },
            { ".csv",  "text/csv" },
            { ".xml",  "text/xml" },
            { ".html", "text/html" },
            { ".css",  "text/css" },
            { ".js",   "text/javascript" },
            { ".ttf",  "font/ttf" },
            { ".otf",  "font/otf" },
        };

        for (const auto& row : table)
            if (strcasecmp(dot, row.ext) == 0) return row.type;

        return "application/octet-stream";
    }
}
