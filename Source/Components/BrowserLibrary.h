// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <climits>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AestraUI::BrowserLibrary {

/// A well-known folder the browser offers without the user having to add it.
struct SystemPlace {
    std::string label; ///< "Home", "Downloads", ...
    std::string path;  ///< Absolute, existing directory.
};

/// Case-insensitive, digit-aware ordering, so "Kick 2" sorts before "Kick 10" and
/// "apple" sits next to "Apple" rather than after every capitalised name.
/// Returns <0, 0 or >0. Names that differ only in case or leading zeros compare
/// equal; callers must tie-break on something stable (the full path).
int naturalCompare(std::string_view a, std::string_view b);

/// Parse the contents of an XDG `user-dirs.dirs` file into a list of
/// (key, path) pairs, e.g. {"DOWNLOAD", "/home/me/Downloads"}. `$HOME` is
/// expanded to @p homeDir. Malformed lines are skipped.
std::vector<std::pair<std::string, std::string>> parseXdgUserDirs(const std::string& content,
                                                                  const std::string& homeDir);

/// The standard places, in display order (Home, Desktop, Downloads, Documents,
/// Music), keeping only those that exist as directories. @p xdgUserDirs is the
/// user-dirs.dirs content (may be empty); it overrides the default folder names
/// on Linux, where "Downloads" can be localised or relocated. A place that
/// resolves to the home directory itself (XDG's way of disabling it) or repeats
/// an earlier place is dropped.
std::vector<SystemPlace> discoverSystemPlaces(const std::string& homeDir, const std::string& xdgUserDirs);

/// Same, with folder locations already resolved as (key, path) pairs using the
/// XDG keys DESKTOP / DOWNLOAD / DOCUMENTS / MUSIC — how Windows' known-folder
/// answers (redirected or localised folders) reach the same rules.
std::vector<SystemPlace> discoverSystemPlacesFrom(const std::string& homeDir,
                                                  const std::vector<std::pair<std::string, std::string>>& folders);

/// discoverSystemPlaces() for the current user, reading the environment and
/// the XDG config directory.
std::vector<SystemPlace> discoverSystemPlaces();

// ---------------------------------------------------------------------------
// Audio facts, read cheaply from file headers (no decoding).
// ---------------------------------------------------------------------------

struct AudioInfo {
    double durationSec = 0.0; ///< 0 when unknown
    uint32_t sampleRate = 0;
    uint16_t channels = 0;
    float tempo = 0.0f; ///< from a WAV 'acid' chunk; 0 when absent
};

/// Read length/rate/channels from a WAV (RIFF), AIFF/AIFC or FLAC header, plus
/// the ACID tempo a WAV loop may carry. Reads at most a few KB. False for any
/// other format or an unreadable/truncated header; @p out is then untouched.
bool readAudioInfo(const std::string& path, AudioInfo& out);

/// Tempo written in a filename: "loop_120bpm", "90 BPM hat". 0 when absent.
int parseBpmFromFilename(const std::string& name);

/// Musical key written in a filename, normalised ("Am", "F#", "Bbm"). Only
/// tokens that cannot be an ordinary letter count: a key needs an accidental
/// or a mode ("C#", "Am", "Dmin", "Ebmaj"); a bare "A" or "C" is ignored.
std::string parseKeyFromFilename(const std::string& name);

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

/// A search box entry split into the name text and metadata filters:
///   bpm:120   bpm:90-100   len:<2   len:>30   len:1-4   key:Am
/// Unknown "word:value" tokens stay part of the name text.
struct SearchQuery {
    std::string text; ///< lower-cased name needle
    int bpmMin = 0;
    int bpmMax = 0; ///< 0 = no BPM filter
    double lenMin = -1.0;
    double lenMax = -1.0; ///< seconds; <0 = unbounded
    std::string key;      ///< normalised, "" = any
    bool hasMetadataFilter() const { return bpmMax > 0 || lenMin >= 0.0 || lenMax >= 0.0 || !key.empty(); }
};
SearchQuery parseSearchQuery(const std::string& raw);

/// matchScore()'s "no match". Real scores can be negative (long names, gaps).
constexpr int kNoMatch = INT32_MIN;

/// Score @p nameLower against @p needleLower; kNoMatch, else higher = better.
/// ".wav" matches an extension, text with a dot matches a substring, anything
/// else is a fuzzy subsequence that rewards word starts and runs.
int matchScore(const std::string& needleLower, const std::string& nameLower);

} // namespace AestraUI::BrowserLibrary
