// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>

#include "../NomadUI/Core/NUIComponent.h"
#include "../NomadUI/Core/NUIDragDrop.h"
#include "PatternSource.h"
#include "ClipSource.h"

// Forward declarations to avoid circular deps and build errors
namespace NomadUI {
    class NUISegmentedControl;
    class NUIButton;
    class NUIIcon;
}

namespace Nomad {
namespace Audio {

class TrackManager;

class PatternBrowserPanel : public NomadUI::NUIComponent, public NomadUI::IDropTarget {
public:
    PatternBrowserPanel(TrackManager* trackManager = nullptr);
    ~PatternBrowserPanel() override;

    // Refresh lists
    void refreshPatterns();
    void refreshClips();

    // Callbacks
    void setOnPatternSelected(std::function<void(Nomad::Audio::PatternID)> callback) { m_onPatternSelected = callback; }
    void setOnPatternDragStart(std::function<void(Nomad::Audio::PatternID)> callback) { m_onPatternDragStart = callback; }
    void setOnPatternDoubleClick(std::function<void(Nomad::Audio::PatternID)> callback) { m_onPatternDoubleClick = callback; }
    
    // Clip callbacks
    void setOnClipDragStart(std::function<void(Nomad::Audio::ClipSourceID)> callback) { m_onClipDragStart = callback; }

    // Currently selected item
    Nomad::Audio::PatternID getSelectedPatternId() const { return m_selectedPatternId; }

    // IDropTarget Implementation
    NomadUI::DropFeedback onDragEnter(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    NomadUI::DropFeedback onDragOver(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    void onDragLeave() override;
    NomadUI::DropResult onDrop(const NomadUI::DragData& data, const NomadUI::NUIPoint& position) override;
    NomadUI::NUIRect getDropBounds() const override;

protected:
    void onRender(NomadUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const NomadUI::NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;

private:
    TrackManager* m_trackManager = nullptr;
    
    enum class BrowserMode {
        Clips = 0,
        Patterns = 1
    };
    BrowserMode m_mode = BrowserMode::Clips; // Default to Clips
    
    std::shared_ptr<NomadUI::NUISegmentedControl> m_modeToggle;

    // Pattern list
    struct PatternEntry {
        Nomad::Audio::PatternID id;
        std::string name;
        bool isMidi;
        double lengthBeats;
        int mixerChannel = -1;
    };
    std::vector<PatternEntry> m_patterns;
    
    // Clip list
    struct ClipEntry {
        Nomad::Audio::ClipSourceID id;
        std::string name;
        std::string filename;
        uint32_t sampleRate;
        uint32_t numChannels;
        double duration;
    };
    std::vector<ClipEntry> m_clips;
    
    Nomad::Audio::PatternID m_selectedPatternId;
    Nomad::Audio::PatternID m_hoveredPatternId;
    
    // UI Layout
    float m_headerHeight = 40.0f; // Header now contains Buttons
    float m_footerHeight = 42.0f; // Footer contains Mode Toggle
    float m_itemHeight = 32.0f;
    float m_scrollOffset = 0.0f;
    
    // Callbacks
    std::function<void(Nomad::Audio::PatternID)> m_onPatternSelected;
    std::function<void(Nomad::Audio::PatternID)> m_onPatternDragStart;
    std::function<void(Nomad::Audio::PatternID)> m_onPatternDoubleClick;
    std::function<void(Nomad::Audio::ClipSourceID)> m_onClipDragStart;
    
    // Buttons (Patterns mode)
    std::shared_ptr<NomadUI::NUIButton> m_createButton;
    std::shared_ptr<NomadUI::NUIButton> m_duplicateButton;
    std::shared_ptr<NomadUI::NUIButton> m_deleteButton;
    
    // Icons
    std::shared_ptr<NomadUI::NUIIcon> m_addIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_copyIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_trashIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_midiIcon;
    std::shared_ptr<NomadUI::NUIIcon> m_audioIcon;
    
    // Theme colors
    NomadUI::NUIColor m_backgroundColor;
    NomadUI::NUIColor m_textColor;
    NomadUI::NUIColor m_borderColor;
    NomadUI::NUIColor m_selectedColor;
    
    // Drag state
    bool m_isDragging = false;
    Nomad::Audio::PatternID m_dragPatternId;
    
    // Improved drag logic
    bool m_dragPotential = false;
    NomadUI::NUIPoint m_dragStartPos;
    
    // Double-click detection
    double m_lastClickTime = 0.0;
    Nomad::Audio::PatternID m_lastClickedPatternId;
    
    // Drag visual feedback
    bool m_isDragOver = false;
    
    void renderHeader(NomadUI::NUIRenderer& renderer);
    void renderContent(NomadUI::NUIRenderer& renderer);
    void renderPatternList(NomadUI::NUIRenderer& renderer);
    void renderClipList(NomadUI::NUIRenderer& renderer);
    
    void renderPatternItem(NomadUI::NUIRenderer& renderer, const PatternEntry& entry, float y, bool selected, bool hovered);
    void renderClipItem(NomadUI::NUIRenderer& renderer, const ClipEntry& entry, float y, bool hovered);
    
    void switchMode(BrowserMode mode);
};

} // namespace Audio
} // namespace Nomad
