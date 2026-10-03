// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "BrowserLibrary.h"

#include "AestraPlatform.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <cmath>
#include <cstring>
#include <fstream>
#include <sstream>

namespace AestraUI::BrowserLibrary {

namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

char foldCase(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

std::string trim(const std::string& s) {
    const auto begin = s.find_first_not_of(" \t\r\n");
    if (begin == std::string::npos) return {};
    const auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(begin, end - begin + 1);
}

std::string compareKey(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    std::string s = (ec ? p.lexically_normal() : canonical).generic_string();
    while (s.size() > 1 && s.back() == '/') s.pop_back();
#if defined(_WIN32)
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int naturalCompare(std::string_view a, std::string_view b) {
    size_t i = 0;
    size_t j = 0;
    while (i < a.size() && j < b.size()) {
        if (isDigit(a[i]) && isDigit(b[j])) {
            // Compare whole digit runs by value: skip leading zeros, then the
            // longer run is the larger number, then the first differing digit.
            while (i < a.size() && a[i] == '0') ++i;
            while (j < b.size() && b[j] == '0') ++j;
            const size_t runA = i;
            const size_t runB = j;
            while (i < a.size() && isDigit(a[i])) ++i;
            while (j < b.size() && isDigit(b[j])) ++j;
            const size_t lenA = i - runA;
            const size_t lenB = j - runB;
            if (lenA != lenB) return lenA < lenB ? -1 : 1;
            const int cmp = a.substr(runA, lenA).compare(b.substr(runB, lenB));
            if (cmp != 0) return cmp < 0 ? -1 : 1;
            continue;
        }
        const char ca = foldCase(a[i]);
        const char cb = foldCase(b[j]);
        if (ca != cb) return static_cast<unsigned char>(ca) < static_cast<unsigned char>(cb) ? -1 : 1;
        ++i;
        ++j;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

std::vector<std::pair<std::string, std::string>> parseXdgUserDirs(const std::string& content,
                                                                  const std::string& homeDir) {
    std::vector<std::pair<std::string, std::string>> dirs;
    std::istringstream lines(content);
    std::string line;
    while (std::getline(lines, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string value = trim(line.substr(eq + 1));

        static constexpr std::string_view kPrefix = "XDG_";
        static constexpr std::string_view kSuffix = "_DIR";
        if (key.size() <= kPrefix.size() + kSuffix.size() || key.compare(0, kPrefix.size(), kPrefix) != 0 ||
            key.compare(key.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
            continue;
        }
        key = key.substr(kPrefix.size(), key.size() - kPrefix.size() - kSuffix.size());

        if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }
        static constexpr std::string_view kHomeVar = "$HOME";
        if (value.compare(0, kHomeVar.size(), kHomeVar) == 0) {
            value.replace(0, kHomeVar.size(), homeDir);
        }
        // The spec only allows $HOME-relative or absolute paths.
        if (value.empty() || !std::filesystem::path(value).is_absolute()) continue;

        dirs.emplace_back(std::move(key), std::move(value));
    }
    return dirs;
}

std::vector<SystemPlace> discoverSystemPlaces(const std::string& homeDir, const std::string& xdgUserDirs) {
    return discoverSystemPlacesFrom(homeDir, parseXdgUserDirs(xdgUserDirs, homeDir));
}

std::vector<SystemPlace> discoverSystemPlacesFrom(const std::string& homeDir,
                                                  const std::vector<std::pair<std::string, std::string>>& xdg) {
    std::vector<SystemPlace> places;
    if (homeDir.empty()) return places;

    const std::filesystem::path home(homeDir);
    const auto xdgPath = [&](const char* key, const char* fallbackName) -> std::filesystem::path {
        for (const auto& [k, v] : xdg) {
            if (k == key) return std::filesystem::path(v);
        }
        return home / fallbackName;
    };

    const std::vector<std::pair<std::string, std::filesystem::path>> candidates = {
        {"Home", home},
        {"Desktop", xdgPath("DESKTOP", "Desktop")},
        {"Downloads", xdgPath("DOWNLOAD", "Downloads")},
        {"Documents", xdgPath("DOCUMENTS", "Documents")},
        {"Music", xdgPath("MUSIC", "Music")},
    };

    const std::string homeKey = compareKey(home);
    std::vector<std::string> seen;
    for (const auto& [label, path] : candidates) {
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec) || ec) continue;
        const std::string key = compareKey(path);
        // XDG disables a folder by pointing it at $HOME; don't list Home twice.
        if (label != "Home" && key == homeKey) continue;
        if (std::find(seen.begin(), seen.end(), key) != seen.end()) continue;
        seen.push_back(key);
        places.push_back({label, path.string()});
    }
    return places;
}

std::vector<SystemPlace> discoverSystemPlaces() {
#if defined(_WIN32)
    const char* home = std::getenv("USERPROFILE");
    std::vector<std::pair<std::string, std::string>> known;
    if (auto* utils = Aestra::Platform::getUtils()) {
        using KF = Aestra::IPlatformUtils::KnownFolder;
        const std::pair<const char*, KF> folders[] = {
            {"DESKTOP", KF::Desktop}, {"DOWNLOAD", KF::Downloads}, {"DOCUMENTS", KF::Documents}, {"MUSIC", KF::Music}};
        for (const auto& [key, folder] : folders) {
            std::string path = utils->getKnownFolderPath(folder);
            if (!path.empty()) known.emplace_back(key, std::move(path));
        }
    }
    return discoverSystemPlacesFrom(home ? home : "", known);
#else
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};

    std::string userDirs;
#if !defined(__APPLE__)
    std::filesystem::path configHome;
    if (const char* xdgConfig = std::getenv("XDG_CONFIG_HOME"); xdgConfig && *xdgConfig) {
        configHome = xdgConfig;
    } else {
        configHome = std::filesystem::path(home) / ".config";
    }
    userDirs = readFile(configHome / "user-dirs.dirs");
#endif
    return discoverSystemPlaces(home, userDirs);
#endif
}

// ---------------------------------------------------------------------------
// Audio headers
// ---------------------------------------------------------------------------

namespace {

uint32_t le32(const unsigned char* p) {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t le16(const unsigned char* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}
uint32_t be32(const unsigned char* p) {
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
uint16_t be16(const unsigned char* p) {
    return static_cast<uint16_t>((p[0] << 8) | p[1]);
}

bool readExact(std::ifstream& in, unsigned char* buf, size_t n) {
    in.read(reinterpret_cast<char*>(buf), static_cast<std::streamsize>(n));
    return static_cast<size_t>(in.gcount()) == n;
}

// 80-bit IEEE 754 extended (AIFF sample rate).
double extended80(const unsigned char* p) {
    const int exponent = ((p[0] & 0x7F) << 8) | p[1];
    uint64_t mantissa = 0;
    for (int i = 0; i < 8; ++i) mantissa = (mantissa << 8) | p[2 + i];
    if (exponent == 0 && mantissa == 0) return 0.0;
    const double value = std::ldexp(static_cast<double>(mantissa), exponent - 16383 - 63);
    return (p[0] & 0x80) ? -value : value;
}

constexpr int kMaxChunks = 64; // bounded walk: a header scan, never a file scan

bool readWav(std::ifstream& in, AudioInfo& out) {
    unsigned char hdr[8];
    uint16_t blockAlign = 0;
    AudioInfo info;
    uint64_t dataBytes = 0;
    bool haveFmt = false;
    bool haveData = false;
    for (int n = 0; n < kMaxChunks && readExact(in, hdr, 8); ++n) {
        const uint32_t size = le32(hdr + 4);
        const std::streamoff next = static_cast<std::streamoff>(in.tellg()) + size + (size & 1u);
        if (std::memcmp(hdr, "fmt ", 4) == 0 && size >= 16) {
            unsigned char fmt[16];
            if (!readExact(in, fmt, 16)) return false;
            info.channels = le16(fmt + 2);
            info.sampleRate = le32(fmt + 4);
            blockAlign = le16(fmt + 12);
            haveFmt = true;
        } else if (std::memcmp(hdr, "data", 4) == 0) {
            dataBytes = size;
            haveData = true;
        } else if (std::memcmp(hdr, "acid", 4) == 0 && size >= 24) {
            unsigned char acid[24];
            if (!readExact(in, acid, 24)) return false;
            float tempo = 0.0f;
            const uint32_t bits = le32(acid + 20);
            std::memcpy(&tempo, &bits, sizeof(tempo));
            if (std::isfinite(tempo) && tempo > 20.0f && tempo < 400.0f) info.tempo = tempo;
        }
        in.clear();
        in.seekg(next);
        if (!in) break;
    }
    if (!haveFmt || info.sampleRate == 0 || info.channels == 0) return false;
    if (haveData && blockAlign > 0) {
        info.durationSec = static_cast<double>(dataBytes / blockAlign) / info.sampleRate;
    }
    out = info;
    return true;
}

bool readAiff(std::ifstream& in, AudioInfo& out) {
    unsigned char hdr[8];
    for (int n = 0; n < kMaxChunks && readExact(in, hdr, 8); ++n) {
        const uint32_t size = be32(hdr + 4);
        const std::streamoff next = static_cast<std::streamoff>(in.tellg()) + size + (size & 1u);
        if (std::memcmp(hdr, "COMM", 4) == 0 && size >= 18) {
            unsigned char comm[18];
            if (!readExact(in, comm, 18)) return false;
            AudioInfo info;
            info.channels = be16(comm);
            const uint32_t frames = be32(comm + 2);
            const double rate = extended80(comm + 8);
            if (info.channels == 0 || !(rate > 0.0) || rate > 1e7) return false;
            info.sampleRate = static_cast<uint32_t>(std::lround(rate));
            info.durationSec = frames / rate;
            out = info;
            return true;
        }
        in.clear();
        in.seekg(next);
        if (!in) break;
    }
    return false;
}

bool readFlac(std::ifstream& in, AudioInfo& out) {
    unsigned char block[4];
    if (!readExact(in, block, 4)) return false;
    if ((block[0] & 0x7F) != 0) return false; // STREAMINFO must come first
    unsigned char si[34];
    if (!readExact(in, si, 34)) return false;
    AudioInfo info;
    info.sampleRate = (uint32_t(si[10]) << 12) | (uint32_t(si[11]) << 4) | (si[12] >> 4);
    info.channels = static_cast<uint16_t>(((si[12] >> 1) & 0x07) + 1);
    const uint64_t totalSamples = (uint64_t(si[13] & 0x0F) << 32) | be32(si + 14);
    if (info.sampleRate == 0) return false;
    if (totalSamples > 0) info.durationSec = static_cast<double>(totalSamples) / info.sampleRate;
    out = info;
    return true;
}

bool isNoteLetter(char c) {
    return c >= 'A' && c <= 'G';
}

std::string lowerCopy(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// "Am", "F#min", "Ebmaj" -> normalised key; @p requireMarker rejects bare roots.
std::string normaliseKeyToken(const std::string& token, bool requireMarker) {
    if (token.empty() || !isNoteLetter(static_cast<char>(std::toupper(static_cast<unsigned char>(token[0]))))) return {};
    if (requireMarker && !isNoteLetter(token[0])) return {}; // filenames: root must be a capital
    std::string key(1, static_cast<char>(std::toupper(static_cast<unsigned char>(token[0]))));
    size_t i = 1;
    bool marker = false;
    if (i < token.size() && (token[i] == '#' || token[i] == 'b')) {
        key.push_back(token[i]);
        ++i;
        marker = true;
    }
    const std::string mode = lowerCopy(token.substr(i));
    if (mode == "m" || mode == "min" || mode == "minor") {
        key.push_back('m');
        marker = true;
    } else if (mode == "maj" || mode == "major") {
        marker = true;
    } else if (!mode.empty()) {
        return {};
    }
    if (requireMarker && !marker) return {};
    return key;
}

} // namespace

bool readAudioInfo(const std::string& path, AudioInfo& out) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) return false;
    unsigned char magic[12];
    if (!readExact(in, magic, 12)) return false;

    if (std::memcmp(magic, "RIFF", 4) == 0 && std::memcmp(magic + 8, "WAVE", 4) == 0) return readWav(in, out);
    if (std::memcmp(magic, "FORM", 4) == 0 &&
        (std::memcmp(magic + 8, "AIFF", 4) == 0 || std::memcmp(magic + 8, "AIFC", 4) == 0)) {
        return readAiff(in, out);
    }
    if (std::memcmp(magic, "fLaC", 4) == 0) {
        in.clear();
        in.seekg(4);
        return readFlac(in, out);
    }
    return false;
}

int parseBpmFromFilename(const std::string& name) {
    // Matches: "kick_120bpm", "loop 128 BPM", "90bpm_hat", "140_bpm_loop"
    for (size_t i = 0; i + 2 < name.size(); ++i) {
        if (!std::isdigit(static_cast<unsigned char>(name[i]))) continue;
        size_t start = i;
        while (i < name.size() && std::isdigit(static_cast<unsigned char>(name[i]))) ++i;
        size_t end = i;
        if (end - start < 2 || end - start > 3) continue;
        const int bpm = std::stoi(name.substr(start, end - start));
        if (bpm < 60 || bpm > 300) continue;
        // "bpm" nearby, before or after the number
        const std::string context =
            lowerCopy(name.substr(start > 4 ? start - 4 : 0, std::min(name.size() - start, end + 5)));
        if (context.find("bpm") != std::string::npos) return bpm;
    }
    return 0;
}

std::string parseKeyFromFilename(const std::string& name) {
    // Drop the extension, then look at each alphanumeric (+'#') token.
    const auto dot = name.find_last_of('.');
    const std::string stem = dot == std::string::npos ? name : name.substr(0, dot);
    std::string token;
    std::string found;
    auto flush = [&]() {
        if (found.empty() && !token.empty()) found = normaliseKeyToken(token, true);
        token.clear();
    };
    for (char c : stem) {
        if (std::isalnum(static_cast<unsigned char>(c)) || c == '#') {
            token.push_back(c);
        } else {
            flush();
        }
    }
    flush();
    return found;
}

SearchQuery parseSearchQuery(const std::string& raw) {
    SearchQuery q;
    std::istringstream words(raw);
    std::string word;
    std::string text;
    const auto parseRange = [](const std::string& v, double& lo, double& hi) {
        try {
            if (v.empty()) return false;
            if (v[0] == '<') {
                hi = std::stod(v.substr(1));
                return true;
            }
            if (v[0] == '>') {
                lo = std::stod(v.substr(1));
                return true;
            }
            const auto dash = v.find('-');
            if (dash != std::string::npos && dash > 0) {
                lo = std::stod(v.substr(0, dash));
                hi = std::stod(v.substr(dash + 1));
                if (lo > hi) std::swap(lo, hi);
                return true;
            }
            lo = hi = std::stod(v);
            return true;
        } catch (...) {
            return false;
        }
    };
    while (words >> word) {
        const auto colon = word.find(':');
        if (colon != std::string::npos && colon > 0) {
            const std::string field = lowerCopy(word.substr(0, colon));
            const std::string value = word.substr(colon + 1);
            if (field == "bpm") {
                double lo = -1.0;
                double hi = -1.0;
                if (parseRange(value, lo, hi)) {
                    q.bpmMin = lo < 0.0 ? 1 : static_cast<int>(std::floor(lo));
                    q.bpmMax = hi < 0.0 ? 999 : static_cast<int>(std::ceil(hi));
                    continue;
                }
            } else if (field == "len" || field == "length") {
                double lo = -1.0;
                double hi = -1.0;
                if (parseRange(value, lo, hi)) {
                    // A bare number means "up to": len:2 is a one-shot under two seconds.
                    if (lo == hi && value[0] != '<' && value[0] != '>') lo = -1.0;
                    q.lenMin = lo;
                    q.lenMax = hi;
                    continue;
                }
            } else if (field == "key") {
                const std::string key = normaliseKeyToken(value, false);
                if (!key.empty()) {
                    q.key = key;
                    continue;
                }
            }
        }
        if (!text.empty()) text.push_back(' ');
        text += word;
    }
    q.text = lowerCopy(text);
    return q;
}

int matchScore(const std::string& needle, const std::string& hay) {
    if (needle.empty()) return 0;
    // Rule 1: ".wav" -> extension match
    if (needle.front() == '.') {
        if (hay.length() < needle.length()) return kNoMatch;
        return hay.compare(hay.length() - needle.length(), needle.length(), needle) == 0 ? 1000 : kNoMatch;
    }
    // Rule 2: a dot anywhere -> substring, earlier is better
    if (needle.find('.') != std::string::npos) {
        const size_t pos = hay.find(needle);
        return pos == std::string::npos ? kNoMatch : 500 - static_cast<int>(pos);
    }
    // Rule 3: fuzzy subsequence. +10 at start, +5 at a word start, +5 per
    // contiguous step, -1 per gap inside the match, -len/10 so shorter wins.
    size_t n = 0;
    size_t h = 0;
    int gapPenalty = 0;
    int bonuses = 0;
    bool started = false;
    while (n < needle.length() && h < hay.length()) {
        if (needle[n] == hay[h]) {
            started = true;
            if (h == 0) bonuses += 10;
            if (h > 0) {
                const char prev = hay[h - 1];
                if (prev == '_' || prev == '-' || prev == ' ' || prev == '.') bonuses += 5;
            }
            if (n > 0 && h > 0 && needle[n - 1] == hay[h - 1]) bonuses += 5;
            ++n;
        } else if (started) {
            gapPenalty -= 1;
        }
        ++h;
    }
    if (n != needle.length()) return kNoMatch;
    return bonuses + gapPenalty - static_cast<int>(hay.length()) / 10;
}

} // namespace AestraUI::BrowserLibrary
