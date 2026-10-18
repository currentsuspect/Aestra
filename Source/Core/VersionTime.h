// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>

namespace Aestra {

/// How times read next to versions and bounces. Local time, no UI types, so the pure models can use it.
namespace VersionTime {

inline std::tm local(uint64_t epochMs) {
    std::time_t t = static_cast<std::time_t>(epochMs / 1000);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    return tm;
}

/// "14:30"; empty for a time that was never set.
inline std::string clock(uint64_t epochMs) {
    if (epochMs == 0) return {};
    const std::tm tm = local(epochMs);
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d:%02d", tm.tm_hour, tm.tm_min);
    return buf;
}

/// "Today · 14:30", or "Oct 2 · 19:15" for earlier days; empty for a time that was never set.
inline std::string label(uint64_t epochMs, uint64_t nowMs) {
    if (epochMs == 0) return {};
    const std::tm then = local(epochMs), now = local(nowMs);
    char buf[48];
    if (then.tm_year == now.tm_year && then.tm_yday == now.tm_yday)
        std::snprintf(buf, sizeof buf, "Today \xc2\xb7 %02d:%02d", then.tm_hour, then.tm_min);
    else {
        static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
        std::snprintf(buf, sizeof buf, "%s %d \xc2\xb7 %02d:%02d", months[std::clamp(then.tm_mon, 0, 11)], then.tm_mday, then.tm_hour, then.tm_min);
    }
    return buf;
}

/// "today 14:02", "yesterday", "2 days ago", "Sep 27": how long ago a bounce was made, for a tile's second line.
inline std::string ago(uint64_t epochMs, uint64_t nowMs) {
    if (epochMs == 0) return {};
    const std::tm then = local(epochMs), now = local(nowMs);
    const int days = (now.tm_year - then.tm_year) * 365 + (now.tm_yday - then.tm_yday);
    char buf[48];
    if (days <= 0) { std::snprintf(buf, sizeof buf, "today %02d:%02d", then.tm_hour, then.tm_min); return buf; }
    if (days == 1) return "yesterday";
    if (days < 7) return std::to_string(days) + " days ago";
    static const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    std::snprintf(buf, sizeof buf, "%s %d", months[std::clamp(then.tm_mon, 0, 11)], then.tm_mday);
    return buf;
}

} // namespace VersionTime
} // namespace Aestra
