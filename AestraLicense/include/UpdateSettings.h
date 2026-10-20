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
