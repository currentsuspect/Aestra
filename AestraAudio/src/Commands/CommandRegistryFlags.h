// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Flag parsing shared by the command factories: CommandRegistry.cpp and the verb families split out of it
// (PianoRollVerbs.cpp). Internal to AestraAudio's command layer.

#include <cctype>
#include <cmath>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Aestra {
namespace Audio {
namespace RegistryFlags {

// Mirror of CommandParser bool spellings — keep in sync with
// CommandParser::convertAndValidateValue (FlagType::Bool).
inline bool parseFlagBool(std::string_view s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower == "true" || lower == "1" || lower == "yes";
}

// Safe flag lookup — returns nullopt if key is missing
inline std::optional<std::string_view> requireFlag(const std::unordered_map<std::string, std::string>& flags, const char* key) {
    auto it = flags.find(key);
    if (it == flags.end()) return std::nullopt;
    return it->second;
}

// Safe parsing helpers that return std::nullopt on malformed input
// Reject leading/trailing whitespace, non-finite floats, and negative unsigned strings
inline std::optional<int> safeStoi(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    try {
        size_t pos = 0;
        int val = std::stoi(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<float> safeStof(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    try {
        size_t pos = 0;
        float val = std::stof(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        if (!std::isfinite(val)) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

inline std::optional<uint64_t> safeStoull(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    if (s.front() == '-') return std::nullopt; // std::stoull wraps negatives to huge values
    try {
        size_t pos = 0;
        uint64_t val = std::stoull(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

} // namespace RegistryFlags
} // namespace Audio
} // namespace Aestra
