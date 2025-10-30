// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../NomadAudio/include/TrackManager.h"
#include "TrackUIComponent.h"
#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIScrollbar.h"
#include "../NomadUI/Core/NUIButton.h"
#include "../NomadUI/Core/NUIIcon.h"
#include "../NomadUI/Widgets/NUIPianoRollWidgets.h"
#include <memory>
#include <vector>

namespace Nomad {
namespace Audio {

/**
 * @brief UI wrapper for TrackManager
 *
 * Provides visual track management interface with:
 * - Track layout and scrolling
 * - Add/remove track functionality
 * - Visual timeline integration
 */
class TrackManagerUI : public NomadUI::NUIComponent {
public:
    TrackManagerUI(std::shared_ptr<TrackManager> trackManager);
    ~TrackManagerUI() override;

    std::shared_ptr<TrackManager> getTrackManager() const { return m_trackManager; }

    // Track Management
    void addTrack(const std::string& name = "");
    void refreshTracks();

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    void onResize(int width, int height) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    
    // ðŸ”¥ VIEWPORT CULLING: Override to only render visible tracks
    void renderChildren(NomadUI::NUIRenderer& renderer);

private:
    std::shared_ptr<TrackManager> m_trackManager;
    std::vector<std::shared_ptr<TrackUIComponent>> m_trackUIComponents;

    // UI Layout
    int m_trackHeight{80};
    int m_trackSpacing{5};
    float m_scrollOffset{0.0f};
    
    // Timeline/Ruler settings
    float m_pixelsPerBeat{50.0f};      // Horizontal zoom level
    float m_timelineScrollOffset{0.0f}; // Horizontal scroll position
    int m_beatsPerBar{4};               // Time signature numerator
    int m_subdivision{4};               // Grid subdivision (4 = 16th notes)
    
    // UI Components
    std::shared_ptr<NomadUI::NUIScrollbar> m_scrollbar;
    std::shared_ptr<NomadUI::NUIScrollbar> m_horizontalScrollbar;
    std::shared_ptr<NomadUI::NUIButton> m_addTrackButton;
    std::shared_ptr<NomadUI::NUIIcon> m_closeIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_minimizeIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_maximizeIcon;
    NomadUI::NUIRect m_closeIconBounds;
    NomadUI::NUIRect m_minimizeIconBounds;
    NomadUI::NUIRect m_maximizeIconBounds;
    
    // Hover states for icons
    bool m_closeIconHovered = false;
    bool m_minimizeIconHovered = false;
    bool m_maximizeIconHovered = false;

    // Piano roll panel
    std::shared_ptr<NomadUI::PianoRollView> m_pianoRoll;
    bool m_showPianoRoll = true;

    void layoutTracks();
    void onAddTrackClicked();
    void updateTrackPositions();
    void updateScrollbar();
    void updateHorizontalScrollbar();
    void onScroll(double position);
    void onHorizontalScroll(double position);
    void deselectAllTracks();
    void renderTimeRuler(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& rulerBounds);
    void renderPlayhead(NomadUI::NUIRenderer& renderer);
    
    // Calculate maximum timeline extent based on samples
    double getMaxTimelineExtent() const;
};

} // namespace Audio
} // namespace Nomad
