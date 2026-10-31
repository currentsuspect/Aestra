// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Size limits for provider HTTP traffic. Pure helpers, split out so the
// policy can be tested without a socket.

#include <cstddef>
#include <string>

namespace Aestra {
namespace MuseAgent {

/** A completion response is a few KB to a few hundred KB; this is generous. */
constexpr size_t kDefaultMaxResponseBytes = 8u * 1024u * 1024u;

/** How much of a remote body may be echoed into an error message. */
constexpr size_t kMaxErrorDetailBytes = 2048;

/**
 * @brief Append @p length bytes to @p out unless that would push it past @p cap.
 * @return false (and leave @p out untouched) if the chunk does not fit.
 */
inline bool appendWithinCap(std::string& out, const char* data, size_t length, size_t cap) {
    if (length > cap || out.size() > cap - length) return false;
    out.append(data, length);
    return true;
}

/** @brief A bounded prefix of @p text, marked as truncated with the original size. */
inline std::string truncateForMessage(const std::string& text,
                                      size_t maxBytes = kMaxErrorDetailBytes) {
    if (text.size() <= maxBytes) return text;
    return text.substr(0, maxBytes) + "... [truncated, " + std::to_string(text.size()) +
           " bytes total]";
}

} // namespace MuseAgent
} // namespace Aestra
