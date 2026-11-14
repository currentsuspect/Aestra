// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The recovery card's words (SPEC-017 criterion 7, #1309): what it says with and without a version.

#include "../../Source/Core/RecoveryInfo.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

using Aestra::RecoveryInfo::build;

void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

TakeManager::Manifest manifestWithVersion(uint64_t savedAtMs) {
    TakeManager::Manifest manifest;
    manifest.ok = true;
    TakeManager::TakeEntry version;
    version.id = "take_1";
    version.name = "Version 4";
    version.branch = "Main";
    version.createdAtMs = savedAtMs - 1000;
    version.updatedAtMs = savedAtMs;
    version.active = true;
    manifest.takes.push_back(version);
    manifest.activeTakeId = version.id;
    return manifest;
}

} // namespace

int main() {
    constexpr uint64_t kMinute = 60000;
    constexpr uint64_t kNow = 1'700'000'000'000ull;

    {   // #1309: a project saved to disk but never versioned says it was saved, not that it never was.
        const TakeManager::Manifest none;
        const auto info = build(none, {}, kNow - 2 * kMinute, kNow);
        require(!info.hasVersion, "no version is reported");
        require(contains(info.detail, "Last saved to disk 2 minutes ago"), "it says when the file was last saved");
        require(contains(info.detail, "no version"), "it still says there is no version");
        require(!contains(info.detail, "No version of this project has been saved yet"), "it no longer claims nothing was saved");
    }

    {   // Nothing on disk and nothing versioned: the honest sentence stays.
        const TakeManager::Manifest none;
        const auto info = build(none, {}, 0, kNow);
        require(info.detail == "No version of this project has been saved yet.", "an unsaved project reads as unsaved");
    }

    {   // A version with autosaves after it: dots, and a gap in plain words.
        const auto manifest = manifestWithVersion(kNow - 10 * kMinute);
        const std::vector<uint64_t> autosaves{kNow - 8 * kMinute, kNow - 5 * kMinute, kNow - 2 * kMinute};
        const auto info = build(manifest, autosaves, kNow - 10 * kMinute, kNow);
        require(info.hasVersion && info.lastSavedName == "Version 4" && info.branch == "Main mix", "the version is named by branch");
        require(info.autosaveMs.size() == 3, "three autosaves after the version");
        require(info.detail == "The newest autosave is 8 minutes after your last saved version.", "the gap is in plain words");
    }

    {   // Only autosaves newer than the saved version are shown, capped at kMaxDots.
        const auto manifest = manifestWithVersion(kNow - 60 * kMinute);
        const std::vector<uint64_t> autosaves{kNow - 90 * kMinute, kNow - 40 * kMinute, kNow - 30 * kMinute,
                                              kNow - 20 * kMinute, kNow - 10 * kMinute};
        const auto info = build(manifest, autosaves, kNow - 60 * kMinute, kNow);
        require(info.autosaveMs.size() == Aestra::RecoveryInfo::kMaxDots, "at most kMaxDots dots");
        require(info.autosaveMs.front() == kNow - 30 * kMinute && info.autosaveMs.back() == kNow - 10 * kMinute,
                "the newest autosaves are the ones shown");
    }

    {   // A version newer than every autosave.
        const auto manifest = manifestWithVersion(kNow - kMinute);
        const auto info = build(manifest, {kNow - 5 * kMinute}, kNow - kMinute, kNow);
        require(info.autosaveMs.empty(), "no dots when the version is the newest");
        require(info.detail == "Nothing newer than your last saved version was autosaved.", "the version is up to date");
    }

    std::cout << "RecoveryInfoTest passed\n";
    return 0;
}
