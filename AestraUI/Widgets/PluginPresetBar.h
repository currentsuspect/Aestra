// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "AestraPanelWindow.h"
#include "NUITypes.h"

#include "Presets/PresetSession.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace AestraUI {

class NUIRenderer;

/**
 * @brief The preset bar in every plugin window's title bar (FD-72, board A to C).
 *
 *   ‹ [• name   source ▾] ›  Save  [A|B]
 *
 * Click the name to browse: each row is heard live on the track as the pointer or the arrow keys reach it; a click
 * or Enter keeps it (one undo), Esc or a click outside puts back what was there. Save opens a small form whose name,
 * tags and place are already filled in, so Enter saves. A|B compares two versions of the plugin's settings.
 *
 * Drawing and hit testing only; what each action does to the plugin is Presets::PresetSession.
 */
class PluginPresetBar : public PanelTitleAccessory {
public:
    explicit PluginPresetBar(std::unique_ptr<Aestra::Audio::Presets::PresetSession> session);

    void render(NUIRenderer& renderer, const NUIRect& titleBar) override;
    void renderOverlay(NUIRenderer& renderer, const NUIRect& window) override;
    bool onMouseEvent(const NUIMouseEvent& event) override;
    bool onKeyEvent(const NUIKeyEvent& event) override;
    bool wantsKeys() const override { return m_popover != Popover::None; }
    float minimumWidth() const override { return kBarMinWidth; }

    Aestra::Audio::Presets::PresetSession& session() { return *m_session; }

    // ---- for tests and the screenshot pass ------------------------------------------------------------
    enum class Popover { None, Browse, Save };
    Popover popover() const { return m_popover; }
    void openBrowse();
    void openSave();
    void closePopover(bool keep);
    const std::string& searchText() const { return m_search; }
    const std::string& saveName() const { return m_saveName; }
    /// Indices into session().presets() that the browser shows for the current search and filter.
    std::vector<size_t> visibleRows() const;
    int filter() const { return m_filter; } ///< 0 All, 1 Factory, 2 Yours, 3 This project
    void setFilter(int f);
    int chipCount() const; ///< "This project" shows only when the project can keep presets
    /// Where things were drawn last frame, for tests: 0 ‹, 1 name, 2 ›, 3 Save, 4 A, 5 B.
    NUIRect controlRect(int which) const;
    NUIRect popoverRect() const { return m_popoverRect; }
    NUIRect rowRect(int visibleIndex) const { return browseRowRect(m_popoverRect, visibleIndex); }

    static constexpr float kBarMinWidth = 420.0f;
    static constexpr float kNameWidth = 230.0f;
    static constexpr float kRowH = 30.0f;
    static constexpr float kBrowseW = 440.0f;
    static constexpr int kMaxRows = 8;

private:
    struct Layout {
        NUIRect prev, name, next, save, ab, a, b;
    };
    Layout layoutFor(const NUIRect& titleBar) const;
    NUIRect browseRect(const NUIRect& window) const;
    NUIRect saveRect(const NUIRect& window) const;
    NUIRect browseRowRect(const NUIRect& popover, int visibleIndex) const;
    NUIRect filterChipRect(const NUIRect& popover, int chip) const;
    NUIRect savePlaceRect(const NUIRect& popover, int which) const;
    NUIRect saveButtonRect(const NUIRect& popover, int which) const; ///< 0 Cancel, 1 Save (or Update), 2 Save as new

    void moveSelection(int delta);
    void hear(size_t presetIndex);
    void doSave(bool update);

    std::unique_ptr<Aestra::Audio::Presets::PresetSession> m_session;
    Popover m_popover = Popover::None;
    Layout m_layout;
    NUIRect m_titleBar;
    NUIRect m_window;
    NUIRect m_popoverRect;

    // browse
    std::string m_search;
    int m_filter = 0;
    int m_scroll = 0;
    std::optional<size_t> m_hoverRow; ///< preset index under the pointer
    bool m_namePressed = false;

    // save
    std::string m_saveName;
    bool m_saveNameFresh = true; ///< the first key typed replaces the suggestion
    int m_savePlace = 0;         ///< 0 Yours, 1 This project
    std::string m_saveError;

    int m_hoverControl = -1;
};

} // namespace AestraUI
