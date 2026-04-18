// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <unordered_set>

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraUI/Core/NUIDragDrop.h"
#include "PatternSource.h"
#include "ClipSource.h"

// Forward declarations to avoid circular deps and build errors
namespace AestraUI {
    class NUISegmentedControl;
    class NUIButton;
    class NUIIcon;
    class NUIContextMenu;
}

namespace Aestra {
namespace Audio {

class TrackManager;

class PatternBrowserPanel : public AestraUI::NUIComponent, public AestraUI::IDropTarget {
public:
    PatternBrowserPanel(TrackManager* trackManager = nullptr);
    ~PatternBrowserPanel() override;

    // Refresh lists
    void refreshPatterns();
    void refreshClips();

    // Callbacks
    void setOnPatternSelected(std::function<void(Aestra::Audio::PatternID)> callback) { m_onPatternSelected = callback; }
    void setOnPatternDragStart(std::function<void(Aestra::Audio::PatternID)> callback) { m_onPatternDragStart = callback; }
    void setOnPatternDoubleClick(std::function<void(Aestra::Audio::PatternID)> callback) { m_onPatternDoubleClick = callback; }
    void setOnPatternPreviewRequested(std::function<void(Aestra::Audio::PatternID)> callback) { m_onPatternPreviewRequested = std::move(callback); }
    void setOnPatternPlaceOnTimelineRequested(std::function<void(Aestra::Audio::PatternID)> callback) { m_onPatternPlaceOnTimelineRequested = std::move(callback); }
    
    // Clip callbacks
    void setOnClipDragStart(std::function<void(Aestra::Audio::ClipSourceID)> callback) { m_onClipDragStart = callback; }
    void setOnClipPreviewRequested(std::function<void(const std::string&)> callback) { m_onClipPreviewRequested = std::move(callback); }
    void setOnClipPlaceOnTimelineRequested(std::function<void(const std::string&, const std::string&)> callback) { m_onClipPlaceOnTimelineRequested = std::move(callback); }
    void setOnClipShowInFileBrowserRequested(std::function<void(const std::string&)> callback) { m_onClipShowInFileBrowserRequested = std::move(callback); }

    // Currently selected item
    Aestra::Audio::PatternID getSelectedPatternId() const { return m_selectedPatternId; }
    void setSelectedPatternId(Aestra::Audio::PatternID patternId, bool notify = false);
    bool isClipBinEmpty() const;
    bool usesCompactRail() const;

    // IDropTarget Implementation
    AestraUI::DropFeedback onDragEnter(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    AestraUI::DropFeedback onDragOver(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    void onDragLeave() override;
    AestraUI::DropResult onDrop(const AestraUI::DragData& data, const AestraUI::NUIPoint& position) override;
    AestraUI::NUIRect getDropBounds() const override;

protected:
    void onRender(AestraUI::NUIRenderer& renderer) override;
    bool onMouseEvent(const AestraUI::NUIMouseEvent& event) override;
    void onResize(int width, int height) override;
    void onUpdate(double deltaTime) override;

private:
    TrackManager* m_trackManager = nullptr;
    
    enum class BrowserMode {
        Clips = 0,
        Patterns = 1
    };
    BrowserMode m_mode = BrowserMode::Clips; // Default to Clips
    
    std::shared_ptr<AestraUI::NUISegmentedControl> m_modeToggle;

    // Pattern list
    struct PatternEntry {
        Aestra::Audio::PatternID id;
        std::string name;
        bool isMidi;
        double lengthBeats;
        int mixerChannel = -1;
        bool isPlacedOnTimeline = false;
    };
    std::vector<PatternEntry> m_patterns;
    
    // Clip list
    struct ClipEntry {
        Aestra::Audio::ClipSourceID id;
        std::string name;
        std::string filename;
        uint32_t sampleRate;
        uint32_t numChannels;
        double duration;
        bool isPlacedOnTimeline = false;
    };
    std::vector<ClipEntry> m_clips;
    
    Aestra::Audio::PatternID m_selectedPatternId;
    Aestra::Audio::PatternID m_hoveredPatternId;
    Aestra::Audio::ClipSourceID m_selectedClipId;
    Aestra::Audio::ClipSourceID m_hoveredClipId;
    
    // UI Layout
    float m_headerHeight = 40.0f; // Header now contains Buttons
    float m_footerHeight = 42.0f; // Footer contains Mode Toggle
    float m_itemHeight = 32.0f;
    float m_scrollOffset = 0.0f;
    float m_targetScrollOffset = 0.0f;
    
    // Callbacks
    std::function<void(Aestra::Audio::PatternID)> m_onPatternSelected;
    std::function<void(Aestra::Audio::PatternID)> m_onPatternDragStart;
    std::function<void(Aestra::Audio::PatternID)> m_onPatternDoubleClick;
    std::function<void(Aestra::Audio::PatternID)> m_onPatternPreviewRequested;
    std::function<void(Aestra::Audio::PatternID)> m_onPatternPlaceOnTimelineRequested;
    std::function<void(Aestra::Audio::ClipSourceID)> m_onClipDragStart;
    std::function<void(const std::string&)> m_onClipPreviewRequested;
    std::function<void(const std::string&, const std::string&)> m_onClipPlaceOnTimelineRequested;
    std::function<void(const std::string&)> m_onClipShowInFileBrowserRequested;
    
    // Buttons (Patterns mode)
    std::shared_ptr<AestraUI::NUIButton> m_createButton;
    std::shared_ptr<AestraUI::NUIButton> m_duplicateButton;
    std::shared_ptr<AestraUI::NUIButton> m_deleteButton;
    
    // Icons
    std::shared_ptr<AestraUI::NUIIcon> m_addIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_copyIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_trashIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_midiIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_audioIcon;
    std::shared_ptr<AestraUI::NUIIcon> m_playIcon;
    
    // Theme colors
    AestraUI::NUIColor m_backgroundColor;
    AestraUI::NUIColor m_textColor;
    AestraUI::NUIColor m_borderColor;
    AestraUI::NUIColor m_selectedColor;
    
    // Drag state
    bool m_isDragging = false;
    Aestra::Audio::PatternID m_dragPatternId;
    Aestra::Audio::ClipSourceID m_dragClipId;
    
    // Improved drag logic
    bool m_dragPotential = false;
    AestraUI::NUIPoint m_dragStartPos;
    
    // Double-click detection
    double m_lastClickTime = 0.0;
    Aestra::Audio::PatternID m_lastClickedPatternId;
    Aestra::Audio::ClipSourceID m_lastClickedClipId;
    
    // Drag visual feedback
    bool m_isDragOver = false;
    bool m_dropTargetRegistered = false;
    std::unordered_set<uint64_t> m_removedClipSourceIds;
    std::unordered_set<uint64_t> m_removedPatternIds;
    std::shared_ptr<AestraUI::NUIContextMenu> m_clipContextMenu;
    std::shared_ptr<AestraUI::NUIContextMenu> m_patternContextMenu;

    void renderHeader(AestraUI::NUIRenderer& renderer);
    void renderContent(AestraUI::NUIRenderer& renderer);
    void renderPatternList(AestraUI::NUIRenderer& renderer);
    void renderClipList(AestraUI::NUIRenderer& renderer);
    
    void renderPatternItem(AestraUI::NUIRenderer& renderer, const PatternEntry& entry, float y, bool selected, bool hovered);
    void renderClipItem(AestraUI::NUIRenderer& renderer, const ClipEntry& entry, float y, bool selected, bool hovered);
    
    void switchMode(BrowserMode mode);
    void ensureDropTargetRegistration();
    void showClipContextMenu(const ClipEntry& entry, const AestraUI::NUIPoint& position);
    void showPatternContextMenu(const PatternEntry& entry, const AestraUI::NUIPoint& position);
};

} // namespace Audio
} // namespace Aestra
