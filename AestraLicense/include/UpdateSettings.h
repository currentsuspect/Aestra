// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// What the user chose about updates, as it is saved (updates.json in the app-data folder). Header-only and free of
// platform code so the rules are testable; reading and writing the file is the app's job.
//
//   channel      "stable" (default) or "early"
//   founderKey   the early channel's key, given to founders privately
//   autoCheck    whether to look for updates by itself (default on); "Check now" always works
//   lastNotified the newest version the user has already been told about, so a version is announced once, not every day

#include "AestraJSON.h"
#include "UpdateClient.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace Aestra {
namespace License {

struct UpdateSettings {
    std::string channel = "stable";
    std::string founderKey;
    bool autoCheck = true;
    std::string lastNotified;
};

/// Whether a check result should tell the user something: a newer build, not announced yet, automatic checks on.
/// (A manual "Check now" shows its own answer in Settings; this is only about the unprompted notification.)
inline bool shouldAnnounceUpdate(UpdateCheckStatus status, const std::string& offeredVersion, const UpdateSettings& settings) {
    return status == UpdateCheckStatus::Available && settings.autoCheck &&
           (settings.lastNotified.empty() || isNewerVersion(offeredVersion, settings.lastNotified));
}

/// What the Settings > Updates page says about the state of things: a headline, one line of detail, and how to colour it.
struct UpdateStatusText {
    enum class Tone { Neutral, Good, Warn };
    std::string headline;
    std::string detail;
    Tone tone = Tone::Neutral;
};

inline std::string updateSizeText(uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

inline UpdateStatusText describeUpdateStatus(bool checking, bool hasResult, const UpdateCheckResult& result,
                                             const UpdateSettings& settings, const std::string& currentVersion) {
    using Tone = UpdateStatusText::Tone;
    if (checking) return {"Checking for updates...", "", Tone::Neutral};
    if (!hasResult) {
        return {"Not checked yet",
                settings.autoCheck ? "Aestra looks shortly after it starts, then once a day." : "Automatic checks are off. Press Check now.",
                Tone::Neutral};
    }
    const std::string channel = settings.channel == "early" ? "early access" : "stable";
    switch (result.status) {
    case UpdateCheckStatus::UpToDate:
        return {"You're up to date", "Aestra " + currentVersion + ", " + channel + " channel.", Tone::Good};
    case UpdateCheckStatus::Available: {
        std::string detail = "You have " + currentVersion + ".";
        if (result.manifest.size > 0) detail += " Download: " + updateSizeText(result.manifest.size) + ".";
        return {"Aestra " + result.manifest.version + " is available", detail, Tone::Good};
    }
    case UpdateCheckStatus::Unauthorized:
        if (settings.channel == "early" && settings.founderKey.empty())
            return {"Enter your founder key", "Early access is for founders. Paste the key you were sent.", Tone::Warn};
        return {"The founder key was not accepted", "Check that it was pasted whole, or ask for a new one.", Tone::Warn};
    case UpdateCheckStatus::NetworkError:
        return {"Couldn't reach the update server", "Aestra will try again in about an hour. " + result.detail, Tone::Warn};
    case UpdateCheckStatus::BadFeed:
        return {"The update information could not be trusted", "Nothing was downloaded or installed. " + result.detail, Tone::Warn};
    case UpdateCheckStatus::BadConfig:
    default:
        return {"Updates are not set up correctly", result.detail, Tone::Warn};
    }
}

inline UpdateSettings parseUpdateSettings(const std::string& text) {
    UpdateSettings s;
    try {
        const JSON j = JSON::parse(text);
        if (!j.isObject()) return s;
        if (j["channel"].isString() && (j["channel"].asString() == "early" || j["channel"].asString() == "stable"))
            s.channel = j["channel"].asString();
        if (j["founderKey"].isString() && j["founderKey"].asString().size() <= 128) s.founderKey = j["founderKey"].asString();
        if (j["autoCheck"].isBool()) s.autoCheck = j["autoCheck"].asBool();
        if (j["lastNotified"].isString() && j["lastNotified"].asString().size() <= 64) s.lastNotified = j["lastNotified"].asString();
    } catch (...) {
        return UpdateSettings(); // a damaged file means defaults, never a crash
    }
    return s;
}

inline std::string serializeUpdateSettings(const UpdateSettings& s) {
    JSON j = JSON::object();
    j["channel"] = s.channel;
    j["founderKey"] = s.founderKey;
    j["autoCheck"] = s.autoCheck;
    j["lastNotified"] = s.lastNotified;
    return j.toString(2);
}

} // namespace License
} // namespace Aestra
