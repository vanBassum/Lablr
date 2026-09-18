#pragma once

#include "Stream.h"
#include <cstdint>

// A rendered label as a PNG, written straight to a stream.
//
// It exists because of what a bitmap is worth to a caller that is not this
// product's own frontend. `render svg`'s native reply is ARGB8888S - one 32-bit
// word per pixel, exactly what the printer and the browser canvas want, and
// meaningless to anything else. A model driving the device through the relay's
// MCP surface can be SHOWN a PNG and cannot be shown that, so the preview it
// gets back is either a picture or a wall of base64.
//
// ── Why it streams, and how ──
//
// Nothing here buffers the image. PNG allows the pixel data to be split across
// any number of IDAT chunks, and miniz hands compressed output to a callback as
// it produces it, so each callback becomes one IDAT chunk written immediately to
// the stream. The input side is the same: one scanline is built at a time and fed
// to the compressor, so the row buffer is 3 bytes per pixel of ONE row and there
// is no second copy of the image anywhere.
//
// The cost of streaming is that the total length is not known when the header is
// written - which is why `contentLength` is optional in the reply convention (see
// lib/protocol/ReplyBody.h). The body runs to the end of the reply, so nothing
// needs it.
//
// The compressor's own state is the one large allocation (miniz's tdefl_compressor
// is a few hundred KB, mostly its dictionary) and it goes in PSRAM, never in the
// internal heap the radios need.
//
// The deflate implementation is the one in ROM. It costs no flash and adds no
// dependency: ESP32-S3's mask ROM exports miniz's tdefl_* entry points, so this
// links against silicon.

/// Encode `pixels` (ARGB8888S, width*height words) as an 8-bit RGB PNG and write
/// it to `out`. Alpha is dropped: a label is composited onto its background
/// before it gets here, and three bytes a pixel is a quarter less to compress.
///
/// Returns null on success, or a static reason string.
const char* WritePng(Stream& out, const uint32_t* pixels,
                     uint32_t width, uint32_t height);
