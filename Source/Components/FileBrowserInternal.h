// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// FileBrowser's private helpers and layout constants, shared by the translation
// units the class is split across (FileBrowser.cpp, FileBrowserNavigation.cpp,
// FileBrowserLibrary.cpp, FileBrowserScan.cpp). Not part of the public interface:
// include it only from those files.

#include "FileBrowser.h"
#include "BrowserLibraryIndex.h"
#include "NUIScrollbar.h"
#include "NUIContextMenu.h"
#include "NUIThemeSystem.h"
#include "NUIDragDrop.h"
#include "Graphics/NUIRenderer.h"
#include "Graphics/OpenGL/NUIRenderCache.h"
#include "NUITextInput.h"
#include "../AestraCore/include/AestraLog.h"
#include "AudioFileValidator.h"
#include "MiniAudioDecoder.h"
#include "../AestraPlat/include/AestraPlatform.h"
#include "Platform/NUIPlatformBridge.h"
#include "../AestraCore/include/AestraUnifiedProfiler.h"
#include "../AestraCore/include/AestraJSON.h"
#include "../AestraPlat/include/AestraJSONFile.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <iostream>
#include <fstream>
#include <unordered_map>

#ifdef _WIN32
#include <Windows.h>
#endif



namespace AestraUI {
namespace FileBrowserInternal {


constexpr float kPreviewPanelHeight = 72.0f;
constexpr float kCompactNavWidth = 52.0f;
constexpr float kCompactNavStartWidth = 320.0f;
constexpr float kExpandedNavStartWidth = 440.0f;
constexpr float BROWSER_SEARCH_ROW_H = 56.0f;
constexpr float BROWSER_TOP_PAD = 7.0f;
constexpr float BROWSER_CONTENT_GAP = 8.0f;
constexpr float BROWSER_NAV_ROW_H = 30.0f;
constexpr float BROWSER_LIST_HEADER_H = 34.0f;
constexpr float BROWSER_LIST_ROW_H = 30.0f;

// Where builds before the v3 library file kept browser settings. Read once, to
// import places dropped onto the nav pane; never written again.
inline std::string legacySettingsPath() {
    const char* home = std::getenv("HOME");
    if (!home || !*home) return {};
    return (std::filesystem::path(home) / ".config" / "aestra" / "browser_settings.json").string();
}

inline AestraUI::NUIComponent* getRootComponent(AestraUI::NUIComponent* component) {
    AestraUI::NUIComponent* root = component;
    while (root && root->getParent()) {
        root = root->getParent();
    }
    return root;
}

inline float computeNavigationWidth(float browserWidth) {
    if (browserWidth <= kCompactNavStartWidth) {
        return std::min(browserWidth, kCompactNavWidth);
    }

    const float expandedWidth = std::clamp(browserWidth * 0.34f, 118.0f, 188.0f);
    if (browserWidth >= kExpandedNavStartWidth) {
        return expandedWidth;
    }

    const float expandedAtBreakpoint = kExpandedNavStartWidth * 0.34f;
    const float progress = (browserWidth - kCompactNavStartWidth) / (kExpandedNavStartWidth - kCompactNavStartWidth);
    return kCompactNavWidth + progress * (expandedAtBreakpoint - kCompactNavWidth);
}

inline void detachPopupMenu(const std::shared_ptr<AestraUI::NUIContextMenu>& menu) {
    if (!menu) return;
    if (auto* parent = menu->getParent()) {
        parent->removeChild(menu);
    }
}

inline void attachAndShowPopupMenu(AestraUI::NUIComponent* owner,
                            const std::shared_ptr<AestraUI::NUIContextMenu>& menu,
                            const AestraUI::NUIPoint& position) {
    if (!owner || !menu) return;
    AestraUI::NUIComponent* root = getRootComponent(owner);
    if (!root) root = owner;
    root->addChild(menu);
    menu->showAt(position);
    root->repaint();
}

// Greedy word wrap for placeholder hint copy, which is longer than the list
// strip at narrow browser widths.
inline std::vector<std::string> wrapHintLines(NUIRenderer& renderer, const std::string& hint, float fontSize,
                                              float maxWidth) {
    std::vector<std::string> lines;
    std::string remaining = hint;
    while (!remaining.empty()) {
        std::string line = remaining;
        std::string rest;
        while (renderer.measureText(line, fontSize).width > maxWidth) {
            const size_t space = line.find_last_of(' ');
            if (space == std::string::npos) break; // single unbreakable word
            rest = line.substr(space + 1) + (rest.empty() ? "" : " " + rest);
            line = line.substr(0, space);
        }
        lines.push_back(line);
        remaining = rest;
    }
    return lines;
}

// Greedy wrap leaves a short orphan last line ("...appear in / the browser").
// Once the line count is known, re-wrap near the average width so the lines
// come out visually balanced; keep the greedy result if that would add a line.
inline std::vector<std::string> wrapHintBalanced(NUIRenderer& renderer, const std::string& hint, float fontSize,
                                                 float maxWidth) {
    auto lines = wrapHintLines(renderer, hint, fontSize, maxWidth);
    if (lines.size() < 2) return lines;
    const float total = renderer.measureText(hint, fontSize).width;
    const float target = (total / static_cast<float>(lines.size())) * 1.2f;
    auto balanced = wrapHintLines(renderer, hint, fontSize, std::clamp(target, maxWidth * 0.5f, maxWidth));
    return balanced.size() == lines.size() ? balanced : lines;
}

inline std::string ellipsizeMiddle(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    constexpr const char* kEllipsis = "...";

    if (text.empty()) return text;
    if (maxWidth <= 0.0f) return "";

    // Check full string first (common case)
    if (renderer.measureText(text, fontSize).width <= maxWidth) {
        return text;
    }

    // Measure prefix (first 60%) and suffix (last 40%) to maintain context
    size_t prefixLen = static_cast<size_t>(text.size() * 0.6);
    size_t suffixLen = text.size() - prefixLen;

    std::string prefix = text.substr(0, prefixLen);
    std::string suffix = text.substr(text.size() - suffixLen);

    // Shrink suffix first, then prefix
    while (!suffix.empty() && renderer.measureText(prefix + kEllipsis + suffix, fontSize).width > maxWidth) {
        suffix = suffix.substr(1);
    }
    while (!prefix.empty() && renderer.measureText(prefix + kEllipsis + suffix, fontSize).width > maxWidth) {
        prefix = prefix.substr(0, prefix.size() - 1);
    }

    return prefix + kEllipsis + suffix;
}


// When every file in a folder starts with the same "<pack> - " prefix (sample
// packs name stems this way), the prefix repeats on every row and pushes the
// part that differs into the ellipsis. The folder already names the pack, so
// rows drop the shared prefix. Display only: search, drag, tooltips and paths
// keep the full name. Returns parent path -> prefix length to drop.
template <typename View>
inline std::unordered_map<std::string, size_t> sharedDisplayPrefixes(const View& view) {
    std::unordered_map<std::string, std::vector<const std::string*>> byParent;
    for (const auto* item : view) {
        if (!item || item->isDirectory || item->isPlaceholder) continue;
        const size_t slash = item->path.find_last_of("/\\");
        byParent[slash == std::string::npos ? std::string() : item->path.substr(0, slash)].push_back(&item->name);
    }
    std::unordered_map<std::string, size_t> out;
    for (const auto& [parent, names] : byParent) {
        if (names.size() < 3) continue;  // two files sharing words is coincidence, not a pack
        std::string prefix = *names.front();
        for (const auto* n : names) {
            size_t k = 0;
            while (k < prefix.size() && k < n->size() && prefix[k] == (*n)[k]) ++k;
            prefix.resize(k);
        }
        // Cut back to a deliberate separator so a prefix never ends mid-word.
        size_t cut = std::string::npos;
        for (const char* sep : {" - ", " \xE2\x80\x93 ", "_"}) {
            const size_t at = prefix.rfind(sep);
            if (at != std::string::npos && at > 0) {
                const size_t end = at + std::char_traits<char>::length(sep);
                if (cut == std::string::npos || end > cut) cut = end;
            }
        }
        if (cut == std::string::npos) continue;
        bool keepsAName = true;
        for (const auto* n : names) keepsAName = keepsAName && n->size() > cut;
        if (keepsAName) out[parent] = cut;
    }
    return out;
}

inline std::string ellipsizeEnd(NUIRenderer& renderer, const std::string& text, float fontSize, float maxWidth) {
    if (text.empty()) return text;
    if (maxWidth <= 0.0f) return "";

    if (renderer.measureText(text, fontSize).width <= maxWidth) return text;

    constexpr const char* kEllipsis = "...";
    const float ellipsisW = renderer.measureText(kEllipsis, fontSize).width;
    if (ellipsisW >= maxWidth) return text;

    static constexpr size_t MIN_VISIBLE = 24;
    const size_t startChars = std::min(MIN_VISIBLE, text.size());
    std::string minCandidate = text.substr(0, startChars) + kEllipsis;
    if (renderer.measureText(minCandidate, fontSize).width > maxWidth) return minCandidate;

    int low = static_cast<int>(startChars);
    int high = static_cast<int>(text.size());
    std::string best = minCandidate;

    while (low <= high) {
        int mid = low + (high - low) / 2;
        std::string candidate = text.substr(0, mid) + kEllipsis;
        if (renderer.measureText(candidate, fontSize).width <= maxWidth) {
            best = candidate;
            low = mid + 1;
        } else {
            high = mid - 1;
        }
    }

    return best;
}

inline std::filesystem::path canonicalOrNormalized(const std::filesystem::path& p) {
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(p, ec);
    return ec ? p.lexically_normal() : canonical;
}

inline std::string normalizedPathForCompare(const std::filesystem::path& p) {
    std::string s = canonicalOrNormalized(p).generic_string();
#if defined(_WIN32)
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}

inline std::string mapKeyForPath(const std::string& path) {
    if (path.empty()) return "";
    std::string s = std::filesystem::path(path).lexically_normal().generic_string();
#if defined(_WIN32)
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return s;
}

inline bool isPathUnderRoot(const std::filesystem::path& candidatePath, const std::filesystem::path& rootPath) {
    const std::string candidate = normalizedPathForCompare(candidatePath);
    std::string root = normalizedPathForCompare(rootPath);
    if (root.empty()) return true;

    // Allow exact match.
    if (candidate == root) return true;

    // Ensure `root/` prefix match.
    if (root.back() != '/') root.push_back('/');
    if (candidate.size() < root.size()) return false;
    return candidate.compare(0, root.size(), root) == 0;
}

// Resolve @p requestedPath to the nearest existing directory (walking up through
// parents that were deleted or unmounted). @p fallbackRoot is used only when
// nothing along that chain exists — it is a starting point, not a boundary:
// the browser may show any folder the user can read.
inline std::string resolveExistingDirectoryPath(const std::string& requestedPath, const std::string& fallbackRoot) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path root = fallbackRoot.empty() ? fs::path() : fs::path(fallbackRoot);
    fs::path candidate = requestedPath.empty() ? root : fs::path(requestedPath);

    while (!candidate.empty()) {
        if (fs::exists(candidate, ec) && fs::is_directory(candidate, ec)) {
            return canonicalOrNormalized(candidate).string();
        }

        const fs::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) {
            break;
        }
        candidate = parent;
    }

    if (!root.empty() && fs::exists(root, ec) && fs::is_directory(root, ec)) {
        return canonicalOrNormalized(root).string();
    }

    if (!root.empty()) {
        return {};
    }

    return requestedPath;
}


inline std::string trimName(const std::string& raw) {
    const auto b = raw.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = raw.find_last_not_of(" \t\r\n");
    return raw.substr(b, e - b + 1);
}

} // namespace FileBrowserInternal
} // namespace AestraUI
