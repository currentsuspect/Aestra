// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ArsenalSoundStrip.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Aestra {

/**
 * @brief A sound set (V81-AR2, FD-46) is a named library collection: files and folders from anywhere on disk. A row
 * that uses one walks its sounds with the arrows and the drawer instead of only its own folder.
 *
 * Members are references. A folder contributes the audio files in it (one level, the way the library lists a
 * folder); a missing member is reported, never silently dropped, so the drawer can say "2 missing".
 */
struct ArsenalSoundSet {
    static constexpr size_t kMaxSounds = 1000;

    struct Resolved {
        std::vector<std::string> files;    ///< Every audio file the set reaches, sorted by name, each once.
        std::vector<std::string> missing;  ///< Members that no longer exist on disk.
    };

    static Resolved resolve(const std::vector<std::string>& members) {
        namespace fs = std::filesystem;
        Resolved out;
        for (const auto& member : members) {
            std::error_code ec;
            if (fs::is_directory(member, ec)) {
                for (const auto& f : ArsenalSoundStrip::siblings((fs::path(member) / "x").string())) out.files.push_back(f); // the folder's audio, sorted
            } else if (fs::is_regular_file(member, ec)) {
                if (ArsenalSoundStrip::isAudioFile(fs::path(member).filename().string())) out.files.push_back(member);
            } else {
                out.missing.push_back(member);
            }
        }
        const auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
        std::sort(out.files.begin(), out.files.end(), [&](const std::string& a, const std::string& b) {
            const auto na = lower(fs::path(a).filename().string()), nb = lower(fs::path(b).filename().string());
            return na != nb ? na < nb : a < b;
        });
        out.files.erase(std::unique(out.files.begin(), out.files.end()), out.files.end());
        if (out.files.size() > kMaxSounds) out.files.resize(kMaxSounds);
        return out;
    }
};

} // namespace Aestra
