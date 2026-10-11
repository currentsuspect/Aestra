// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief FNV-1a over a file's bytes, as a 16-hex-digit string.
 *
 * Exists for one question: did this render come out the same as last time? Two
 * renders of an unchanged session must produce identical files, so a changed
 * digest means something in the path is non-deterministic — which is worth
 * knowing before a producer trusts a bounce.
 *
 * This is a byte hash, deliberately, not a measurement of the audio. Peak, RMS
 * and friends are already measured by AudioExporter::Result, so verify_render
 * reports those rather than re-deriving them from the samples. If a shared
 * render-measurement unit ever lands (AestraHeadless --check carries one),
 * this is the function to delete and delegate to it: a digest over bytes and a
 * summary of levels are different questions, and only one of them belongs here.
 *
 * Empty string on failure — a caller must not treat "could not read it" as
 * "read it and it was zero".
 */
std::string fnv1aFileDigest(const std::string& path);

} // namespace Audio
} // namespace Aestra
