// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ArsenalSoundStrip.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace Aestra {

/**
 * @brief Kits and the empty state (V81-AR1, FD-27 point 6): an Arsenal with no sounds offers to start from a
 * folder of them. Selecting the folder opens it in the File Browser; no units, samples or patterns are created by selection.
 *
 * `plan` remains available as a pure import planner for future explicit kit-building workflows. `ArsenalKitCard` is the empty-state card.
 */
struct KitSlot {
    std::string name;
    std::string path;
};

struct ArsenalKit {
    static constexpr size_t kMaxUnits = 16;

    /// Audio files directly inside @p dir (full paths). Subfolders are not walked.
    static std::vector<std::string> filesIn(const std::string& dir) {
        std::vector<std::string> out;
        std::error_code ec;
        if (dir.empty() || !std::filesystem::is_directory(dir, ec)) return out;
        for (const auto& entry : std::filesystem::directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            std::error_code e2;
            if (entry.is_regular_file(e2) && ArsenalSoundStrip::isAudioFile(entry.path().filename().string())) out.push_back(entry.path().string());
        }
        return out;
    }

    /// The units a kit makes: audio files only, sorted by name (case-insensitive), at most kMaxUnits, each named
    /// after its file. Two files with the same name keep their order and the later one gets a " 2", " 3"...
    static std::vector<KitSlot> plan(std::vector<std::string> files) {
        files.erase(std::remove_if(files.begin(), files.end(), [](const std::string& f) { return !ArsenalSoundStrip::isAudioFile(std::filesystem::path(f).filename().string()); }), files.end());
        std::sort(files.begin(), files.end(), [](const std::string& a, const std::string& b) { return lower(a) < lower(b); });
        if (files.size() > kMaxUnits) files.resize(kMaxUnits);
        std::vector<KitSlot> out;
        for (const auto& f : files) {
            std::string base = ArsenalSoundStrip::displayName(f), name = base;
            for (int n = 2; std::any_of(out.begin(), out.end(), [&](const KitSlot& s) { return s.name == name; }); ++n) name = base + " " + std::to_string(n);
            out.push_back({name, f});
        }
        return out;
    }

private:
    static std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }
};

/// The card the empty Arsenal shows under its rows: a heading, one line, and a button that chooses the folder.
/// The panel reserves kReserve below the rows while it is shown, so the panel grows to fit it instead of clipping it.
class ArsenalKitCard {
public:
    static constexpr float kW = 340.0f, kH = 130.0f, kReserve = kH + 16.0f;

    /// @p slot: where the card goes (its top is under the last row); @p shown: no unit has a sound yet.
    void place(bool shown, const AestraUI::NUIRect& slot) { m_shown = shown; m_slot = slot; }
    bool shown() const { return m_shown; }
    AestraUI::NUIRect cardRect() const { return {m_slot.x + (m_slot.width - kW) * 0.5f, m_slot.y + 8.0f, kW, kH}; }
    AestraUI::NUIRect buttonRect() const {
        const auto c = cardRect();
        return {c.x + 20.0f, c.bottom() - 48.0f, c.width - 40.0f, 32.0f};
    }
    bool hitButton(const AestraUI::NUIPoint& p) const { return m_shown && buttonRect().contains(p); }

    void draw(AestraUI::NUIRenderer& renderer) const {
        if (!m_shown || m_slot.width < kW) return;
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const auto& tp = theme.getCurrentTheme();
        const auto c = cardRect();
        renderer.fillRoundedRect(c, tp.radiusM, theme.getColor("backgroundSecondary").withAlpha(0.97f));
        renderer.strokeRoundedRect(c, tp.radiusM, 1.0f, theme.getColor("borderSubtle").withAlpha(0.5f));
        renderer.drawTextCentered("Start with a kit", {c.x + 12.0f, c.y + 10.0f, c.width - 24.0f, 24.0f},
                                  tp.fontSizeL, theme.getColor("textPrimary").withAlpha(0.96f));
        // Two centred lines under the heading, each inside the card's padding. The second line used to be left over
        // from when a kit made units ("named after it, ready to play"); browsing adds nothing now, so say that.
        const AestraUI::NUIColor body = theme.getColor("textSecondary").withAlpha(0.85f);
        renderer.drawTextCentered("Browse a folder of sounds without changing your Arsenal.",
                                  {c.x + 20.0f, c.y + 40.0f, c.width - 40.0f, 18.0f}, tp.fontSizeXS, body);
        renderer.drawTextCentered("Nothing is added until you choose a sound.",
                                  {c.x + 20.0f, c.y + 58.0f, c.width - 40.0f, 18.0f}, tp.fontSizeXS, body);
        const auto b = buttonRect();
        renderer.fillRoundedRect(b, tp.radiusS, theme.getColor("accentPrimary").withAlpha(0.22f));
        renderer.strokeRoundedRect(b, tp.radiusS, 1.0f, theme.getColor("accentPrimary").withAlpha(0.8f));
        renderer.drawTextCentered("Browse a folder", b, tp.fontSizeS, theme.getColor("accentPrimary"));
    }

private:
    bool m_shown{false};
    AestraUI::NUIRect m_slot{};
};

} // namespace Aestra
