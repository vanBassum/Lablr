#pragma once

#include <cstddef>
#include <cstdio>
#include <cstring>

// ──────────────────────────────────────────────────────────────
// Joining a path from the wire onto a mount point, safely.
//
// This lives in lib/ because it names no layer: it is string arithmetic over a
// base and a relative path, and it knows nothing about FAT, about labels or
// about who is asking.
//
// It is also the only thing standing between a path a client chose and the rest
// of the VFS, which is why it is here rather than inline in a command handler -
// a function with no dependencies can be compiled and tested on a PC, and this
// one is (test/host/test_pure.cpp). The check it performs is worth stating
// exactly: ".." ANYWHERE is refused rather than normalised. There is no
// legitimate use for it on this filesystem, and normalising is how path checks
// get subtly wrong.
// ──────────────────────────────────────────────────────────────

namespace path
{

/// Join `rel` under `base` into `out`. False when the result would not fit, or
/// when `rel` contains "..", or when either input is null.
///
/// `rel` may or may not lead with '/'; the separator is supplied either way, so
/// "labels/x.svg" and "/labels/x.svg" land in the same place.
inline bool ResolveUnder(const char* base, const char* rel, char* out, size_t cap)
{
    if (!base || !rel || !out || cap == 0) return false;

    if (strstr(rel, "..") != nullptr) return false;

    const char* sep = (rel[0] == '/') ? "" : "/";
    const int n = snprintf(out, cap, "%s%s%s", base, sep, rel);
    return n > 0 && static_cast<size_t>(n) < cap;
}

}  // namespace path
