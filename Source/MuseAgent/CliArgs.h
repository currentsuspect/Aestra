// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Strict command-line value parsing for muse-agent, split out of main() so it
// can be tested without a process. Pure: strings in, values out, no IO.

#include <charconv>
#include <optional>
#include <string_view>

namespace Aestra {
namespace MuseAgent {

/**
 * @brief Parse a whole token as a base-10 integer within [min, max].
 *
 * Rejects what std::stoi / atoi quietly accept: trailing text ("900s", "5x"),
 * fractions ("41952.5"), a leading '+' or whitespace, an empty token, and
 * values outside the range (including overflow). Returns nullopt on any of
 * those so the caller can name the flag and exit with a usage error.
 */
inline std::optional<int> parseBoundedInt(std::string_view text, int min, int max) {
    if (text.empty()) return std::nullopt;
    int value = 0;
    const char* const begin = text.data();
    const char* const end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc() || ptr != end) return std::nullopt;
    if (value < min || value > max) return std::nullopt;
    return value;
}

} // namespace MuseAgent
} // namespace Aestra
