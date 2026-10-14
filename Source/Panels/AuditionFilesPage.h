// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../Core/AuditionFiles.h"

#include <functional>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief The files column of Audition's Files page (FD-26, V81-A): folders and audio only, set like the rest of the
 * hub. Audio rows drag onto the queue beside it, play with a double click, and can be kept as a reference.
 */
class AuditionFilesPage : public AestraUI::NUIComponent {
public:
    AuditionFilesPage();

    void onRender(AestraUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;

    void setOnPlay(std::function<void(const std::string&)> cb) { m_onPlay = std::move(cb); }
    void setOnAddReference(std::function<void(const std::string&)> cb) { m_onAddReference = std::move(cb); }
    void setProjectDir(const std::string& dir);
    void setDirectory(const std::string& dir);
    const std::string& directory() const { return m_dir; }
    const std::vector<AuditionFiles::Entry>& entries() const { return m_entries; }
    bool dragActive() const { return m_dragging; }

    // For tests and hit-testing.
    bool getRowCenter(size_t row, AestraUI::NUIPoint& out) const;

private:
    struct Layout {
        AestraUI::NUIRect header, up, path, placesArea, list;
        std::vector<AestraUI::NUIRect> places;
    };
    Layout layout() const;
    float rowHeight() const { return 36.0f; }
    int rowAt(const AestraUI::NUIPoint& p, const Layout& l) const;
    AestraUI::NUIRect playButton(const AestraUI::NUIRect& row) const;
    AestraUI::NUIRect refButton(const AestraUI::NUIRect& row) const;
    void clampScroll();

    std::string m_dir, m_projectDir, m_home;
    std::vector<AuditionFiles::Entry> m_entries;
    std::vector<AuditionFiles::Place> m_places;
    float m_scroll{0.0f};
    int m_hover{-1}, m_selected{-1}, m_pressed{-1};
    std::string m_hoverControl;
    AestraUI::NUIPoint m_pressPos;
    bool m_dragging{false};
    std::function<void(const std::string&)> m_onPlay, m_onAddReference;
};

} // namespace Aestra
