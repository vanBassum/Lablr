#include "PngStream.h"

#include "miniz.h"
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <cstring>

namespace
{

constexpr const char* TAG = "PngStream";

/// CRC-32 as PNG uses it, which is the ordinary zlib polynomial.
///
/// Written out rather than taken from ROM because the ROM entry point's
/// pre/post-inversion convention is not something the header states, and a CRC
/// that is wrong by an inversion produces a file that every decoder rejects with
/// no clue as to why. Bitwise is fast enough: the whole image passes through it
/// once, at a few cycles a byte, against a compressor doing far more per byte.
///
/// Chaining works - crc32(crc32(0, a), b) is the CRC of a followed by b - because
/// the inversion at the end of one call is undone at the start of the next.
uint32_t crc32(uint32_t crc, const uint8_t* data, size_t len)
{
    crc = ~crc;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (~(crc & 1u) + 1u));
    }
    return ~crc;
}

/// PNG is big-endian throughout, unlike everything else on this device.
void writeBE32(uint8_t* p, uint32_t v)
{
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

/// One PNG chunk: length, type, data, CRC over type+data.
void writeChunk(Stream& out, const char* type, const uint8_t* data, size_t len)
{
    uint8_t header[8];
    writeBE32(header, static_cast<uint32_t>(len));
    memcpy(header + 4, type, 4);
    out.write(header, sizeof(header));
    if (len) out.write(data, len);

    uint32_t crc = crc32(0, reinterpret_cast<const uint8_t*>(type), 4);
    if (len) crc = crc32(crc, data, len);

    uint8_t tail[4];
    writeBE32(tail, crc);
    out.write(tail, sizeof(tail));
}

/// What the compressor hands back, turned into IDAT chunks as it arrives. This
/// is the whole reason the encoder needs no output buffer.
struct Sink
{
    Stream* out;
    bool    failed;
};

mz_bool putBuf(const void* buf, int len, void* user)
{
    auto* sink = static_cast<Sink*>(user);
    if (sink->failed) return MZ_FALSE;

    if (len > 0)
        writeChunk(*sink->out, "IDAT", static_cast<const uint8_t*>(buf),
                   static_cast<size_t>(len));

    // A transport that broke mid-image must stop the compressor, not be written
    // to for the rest of the picture.
    if (sink->out->failed())
    {
        sink->failed = true;
        return MZ_FALSE;
    }
    return MZ_TRUE;
}

} // namespace

const char* WritePng(Stream& out, const uint32_t* pixels,
                     uint32_t width, uint32_t height)
{
    if (!pixels || width == 0 || height == 0) return "nothing to encode";

    // One row of RGB plus its filter byte. The only per-image buffer besides the
    // compressor, and it is bytes-per-pixel times ONE row.
    const size_t rowBytes = 1u + static_cast<size_t>(width) * 3u;
    uint8_t* row = static_cast<uint8_t*>(
        heap_caps_malloc(rowBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!row) return "no PSRAM for png row";

    // miniz's compressor state is large and is never allocated by miniz itself
    // here - the ROM build has MINIZ_NO_MALLOC, so providing it is the caller's
    // job. PSRAM, because it is hundreds of KB and the internal heap is the
    // scarce kind.
    auto* comp = static_cast<tdefl_compressor*>(
        heap_caps_malloc(sizeof(tdefl_compressor), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!comp)
    {
        heap_caps_free(row);
        return "no PSRAM for png compressor";
    }

    const char* failure = nullptr;
    Sink sink{ &out, false };

    // ── Signature and IHDR ──
    static const uint8_t signature[8] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    out.write(signature, sizeof(signature));

    uint8_t ihdr[13];
    writeBE32(ihdr + 0, width);
    writeBE32(ihdr + 4, height);
    ihdr[8]  = 8;    // bits per channel
    ihdr[9]  = 2;    // colour type 2 = RGB, no alpha
    ihdr[10] = 0;    // deflate, the only compression PNG defines
    ihdr[11] = 0;    // adaptive filtering, the only filter method PNG defines
    ihdr[12] = 0;    // not interlaced
    writeChunk(out, "IHDR", ihdr, sizeof(ihdr));

    // ── IDAT, one chunk per compressor callback ──
    //
    // TDEFL_WRITE_ZLIB_HEADER because PNG's IDAT stream is zlib-wrapped, not raw
    // deflate: miniz then emits the 2-byte header and the trailing Adler-32
    // itself, which is the half of the format easiest to get subtly wrong.
    // Cast because miniz declares its flags in two separate unnamed enums, and
    // C++20 made OR-ing across those an error rather than the intended use.
    const int deflateFlags = static_cast<int>(TDEFL_WRITE_ZLIB_HEADER)
                           | static_cast<int>(TDEFL_DEFAULT_MAX_PROBES);
    if (tdefl_init(comp, putBuf, &sink, deflateFlags) != TDEFL_STATUS_OKAY)
    {
        failure = "png compressor init failed";
    }

    for (uint32_t y = 0; !failure && y < height; ++y)
    {
        // Filter type 0 (None) for every row. A label is flat colour over large
        // areas, which deflate already handles well; Paeth would buy a few
        // percent for a per-pixel cost on a device that has better uses for it.
        row[0] = 0;
        const uint32_t* src = pixels + static_cast<size_t>(y) * width;
        size_t o = 1;
        for (uint32_t x = 0; x < width; ++x)
        {
            // ARGB8888S is 0xAARRGGBB in a 32-bit word. Alpha is dropped.
            const uint32_t px = src[x];
            row[o++] = static_cast<uint8_t>(px >> 16);
            row[o++] = static_cast<uint8_t>(px >> 8);
            row[o++] = static_cast<uint8_t>(px);
        }

        if (tdefl_compress_buffer(comp, row, rowBytes, TDEFL_NO_FLUSH) != TDEFL_STATUS_OKAY)
            failure = sink.failed ? "transport failed mid-png" : "png compression failed";
    }

    if (!failure &&
        tdefl_compress_buffer(comp, nullptr, 0, TDEFL_FINISH) != TDEFL_STATUS_DONE)
        failure = sink.failed ? "transport failed mid-png" : "png compression failed";

    // ── IEND ──
    //
    // Written even after a compression failure: the reply is already part-way
    // out and cannot be recalled, so a terminated file that a decoder rejects
    // cleanly beats a truncated one it hangs on.
    if (!sink.failed)
        writeChunk(out, "IEND", nullptr, 0);

    heap_caps_free(comp);
    heap_caps_free(row);

    if (failure) ESP_LOGE(TAG, "%s (%ux%u)", failure, (unsigned)width, (unsigned)height);
    return failure;
}
