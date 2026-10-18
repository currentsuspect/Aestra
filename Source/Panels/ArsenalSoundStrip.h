// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ArsenalClip.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Aestra {

/**
 * @brief The sound strip at the top of an open sampler row (V81-AR1, FD-27 point 5): step to the previous or next
 * sound in the same folder and hear it in the pattern at once. Swapping is gap-free (V81-E3), so notes already
 * sounding finish on the old sound and the next hit is the new one.
 *
 * The neighbour rule is pure (`siblings`, `stepped`): the audio files beside the current one, sorted by name, and
 * a wrap-around step through them.
 */
class ArsenalSoundStrip {
public:
    static constexpr float kHeight = 30.0f;
    static constexpr float kButtonW = 28.0f;
    static constexpr size_t kMaxSiblings = 500;

    enum class Hit { None, Prev, Next };

    static bool isAudioFile(const std::string& name) {
        const auto dot = name.find_last_of('.');
        if (dot == std::string::npos) return false;
        std::string ext = name.substr(dot + 1);
        for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return ext == "wav" || ext == "flac" || ext == "mp3" || ext == "ogg" || ext == "aif" || ext == "aiff";
    }

    /// Audio files in the same folder as @p path (full paths), sorted by name, case-insensitively. Capped.
    static std::vector<std::string> siblings(const std::string& path) {
        std::vector<std::string> out;
        std::error_code ec;
        const std::filesystem::path dir = std::filesystem::path(path).parent_path();
        if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return out;
        for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (out.size() >= kMaxSiblings) break;
            std::error_code e2;
            if (entry.is_regular_file(e2) && isAudioFile(entry.path().filename().string())) out.push_back(entry.path().string());
        }
        std::sort(out.begin(), out.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        return out;
    }

    /// Where @p path sits in @p list, or -1.
    static int indexOf(const std::vector<std::string>& list, const std::string& path) {
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i] == path) return static_cast<int>(i);
        return -1;
    }

    /// The entry @p delta places from @p index, wrapping; empty when there is nothing to step to.
    /// In a sound set the current sound may sit outside the list (the set was chosen after the sound): @p enterFromOutside
    /// then steps into the set from its start (Next) or its end (Prev) instead of nowhere.
    static std::string stepped(const std::vector<std::string>& list, int index, int delta, bool enterFromOutside = false) {
        if (list.empty()) return {};
        if (index < 0) return enterFromOutside ? list[delta > 0 ? 0 : list.size() - 1] : std::string();
        if (list.size() < 2) return {};
        const int n = static_cast<int>(list.size());
        return list[static_cast<size_t>(((index + delta) % n + n) % n)];
    }

    static std::string displayName(const std::string& path) {
        return std::filesystem::path(path).stem().string();
    }

    AestraUI::NUIRect prevRect(const AestraUI::NUIRect& r) const { return {r.x + 70.0f, r.y + 3.0f, kButtonW, r.height - 6.0f}; }
    AestraUI::NUIRect nextRect(const AestraUI::NUIRect& r) const { return {r.right() - kButtonW - 8.0f, r.y + 3.0f, kButtonW, r.height - 6.0f}; }

    Hit hitTest(const AestraUI::NUIRect& r, const AestraUI::NUIPoint& p) const {
        if (prevRect(r).contains(p)) return Hit::Prev;
        if (nextRect(r).contains(p)) return Hit::Next;
        return Hit::None;
    }

    /// Refresh the folder listing when the unit's sound changes (a scan only on a new path).
    void follow(const std::string& path) {
        if (m_custom) { m_path = path; m_index = indexOf(m_list, path); return; } // a set: the list is the set's, not the folder's
        if (path == m_path) return;
        m_path = path;
        m_list = path.empty() ? std::vector<std::string>{} : siblings(path);
        m_index = indexOf(m_list, path);
    }
    /// Walk @p files (a sound set) instead of the sound's own folder, until useFolder().
    void useList(const std::string& setName, std::vector<std::string> files) { m_setName = setName; m_list = std::move(files); m_custom = true; m_index = indexOf(m_list, m_path); }
    /// Back to the sound's own folder.
    void useFolder() { m_custom = false; m_setName.clear(); m_path.clear(); m_list.clear(); m_index = -1; }
    const std::string& setName() const { return m_setName; }
    bool isSet() const { return m_custom; }
    const std::vector<std::string>& list() const { return m_list; }
    int index() const { return m_index; }
    std::string neighbour(int delta) const { return stepped(m_list, m_index, delta, m_custom); }

    void draw(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& r, const AestraUI::NUIRect& outer = {}) const {
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const auto& tp = theme.getCurrentTheme();
        renderer.fillRoundedRect(r, tp.radiusS, theme.getColor("backgroundPrimary").withAlpha(0.9f));
        renderer.drawText(m_custom ? "SET" : "SOUND", {r.x + 12.0f, r.y + r.height * 0.5f - 5.0f}, tp.fontSizeMicro, theme.getColor("textSecondary").withAlpha(0.8f));
        const bool canStep = m_list.size() > 1 || (m_custom && !m_list.empty() && m_index < 0);
        const auto btnFill = theme.getColor("surfaceTertiary").withAlpha(canStep ? 0.9f : 0.4f);
        const auto txt = theme.getColor("textPrimary").withAlpha(canStep ? 0.95f : 0.35f);
        for (const auto& [rect, glyph] : {std::pair{prevRect(r), "<"}, std::pair{nextRect(r), ">"}}) {
            renderer.fillRoundedRect(rect, tp.radiusXS, btnFill);
            renderer.strokeRoundedRect(rect, tp.radiusXS, 1.0f, theme.getColor("borderSubtle").withAlpha(0.5f));
            renderer.drawTextCentered(glyph, rect, tp.fontSizeS, txt);
        }
        const AestraUI::NUIRect nameRect{prevRect(r).right() + 8.0f, r.y, nextRect(r).x - prevRect(r).right() - 16.0f, r.height};
        ArsenalClip::within(renderer, nameRect, outer);
        const std::string name = m_path.empty() ? "No sound loaded" : displayName(m_path);
        renderer.drawTextCentered(name + ((m_custom ? "   [" + m_setName + "]" : std::string()) + (canStep && m_index >= 0 ? "   " + std::to_string(m_index + 1) + "/" + std::to_string(m_list.size()) : std::string())), nameRect, tp.fontSizeXS,
                                  theme.getColor("textPrimary").withAlpha(0.92f));
        ArsenalClip::restore(renderer, outer);
    }

private:
    static std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
    std::string m_path;
    std::vector<std::string> m_list;
    int m_index{-1};
    bool m_custom{false};
    std::string m_setName;
};

} // namespace Aestra
