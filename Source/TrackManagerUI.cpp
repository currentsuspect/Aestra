#include "TrackManagerUI.h"
#include "../NomadUI/Core/NUIThemeSystem.h"
#include "../NomadUI/Graphics/NUIRenderer.h"
#include "../NomadCore/include/NomadLog.h"

namespace Nomad {
namespace Audio {

TrackManagerUI::TrackManagerUI(std::shared_ptr<TrackManager> trackManager)
    : m_trackManager(trackManager)
{
    if (!m_trackManager) {
        Log::error("TrackManagerUI created with null track manager");
        return;
    }

    // Create add track button
    m_addTrackButton = std::make_shared<NomadUI::NUIButton>();
    m_addTrackButton->setText("+");
    m_addTrackButton->setOnClick([this]() {
        onAddTrackClicked();
    });
    // Set colors: grey on hover, white on click
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    m_addTrackButton->setBackgroundColor(NomadUI::NUIColor(0.15f, 0.15f, 0.18f, 1.0f)); // Default grey
    m_addTrackButton->setTextColor(themeManager.getColor("textPrimary")); // White text
    m_addTrackButton->setHoverColor(NomadUI::NUIColor(0.25f, 0.25f, 0.28f, 1.0f)); // Lighter grey on hover
    m_addTrackButton->setPressedColor(NomadUI::NUIColor(0.15f, 0.15f, 0.18f, 1.0f)); // Same as background - no color change on click
    addChild(m_addTrackButton);

    // Create UI components for existing tracks
    refreshTracks();
}

TrackManagerUI::~TrackManagerUI() {
    Log::info("TrackManagerUI destroyed");
}

void TrackManagerUI::addTrack(const std::string& name) {
    if (m_trackManager) {
        auto track = m_trackManager->addTrack(name);

        // Create UI component for the track
        auto trackUI = std::make_shared<TrackUIComponent>(track);
        m_trackUIComponents.push_back(trackUI);
        addChild(trackUI);

        layoutTracks();
        Log::info("Added track UI: " + name);
    }
}

void TrackManagerUI::refreshTracks() {
    if (!m_trackManager) return;

    // Clear existing UI components
    for (auto& trackUI : m_trackUIComponents) {
        removeChild(trackUI);
    }
    m_trackUIComponents.clear();

    // Create UI components for all tracks (except preview track)
    for (size_t i = 0; i < m_trackManager->getTrackCount(); ++i) {
        auto track = m_trackManager->getTrack(i);
        if (track && track->getName() != "Preview") {  // Skip preview track
            auto trackUI = std::make_shared<TrackUIComponent>(track);
            m_trackUIComponents.push_back(trackUI);
            addChild(trackUI);
        }
    }

    layoutTracks();
}

void TrackManagerUI::onAddTrackClicked() {
    addTrack(); // Add track with auto-generated name
}

void TrackManagerUI::layoutTracks() {
    NomadUI::NUIRect bounds = getBounds();
    Log::info("TrackManagerUI layoutTracks: parent bounds x=" + std::to_string(bounds.x) + ", y=" + std::to_string(bounds.y) + ", w=" + std::to_string(bounds.width) + ", h=" + std::to_string(bounds.height));

    // Get layout dimensions from theme
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();

    float currentY = 0.0f; // Start immediately at top of track manager area

    // Layout add track button at very top (no margin)
    if (m_addTrackButton) {
        float buttonSize = 30.0f; // Fixed size for add button
        m_addTrackButton->setBounds(NUIAbsolute(bounds, 0.0f, 0.0f, buttonSize, buttonSize)); // No left margin
        Log::info("TrackManagerUI addTrackButton bounds: x=" + std::to_string(bounds.x) + ", y=" + std::to_string(bounds.y));
        currentY = buttonSize; // Tracks start immediately after button
    }

    // Layout track UI components using configurable dimensions
    float controlWidth = layout.trackControlsWidth;
    for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
        auto trackUI = m_trackUIComponents[i];
        if (trackUI) {
            // Position tracks starting from the left edge of the track manager area
            trackUI->setBounds(NUIAbsolute(bounds, 0, currentY, controlWidth, layout.trackHeight));
            // Log::info("TrackManagerUI trackUI[" + std::to_string(i) + "] bounds: x=" + std::to_string(bounds.x) + ", y=" + std::to_string(bounds.y + currentY));
            currentY += layout.trackHeight; // No spacing between tracks - they touch
        }
    }
}

void TrackManagerUI::updateTrackPositions() {
    layoutTracks();
}

void TrackManagerUI::onRender(NomadUI::NUIRenderer& renderer) {
    NomadUI::NUIRect bounds = getBounds();

    // Draw background
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    NomadUI::NUIColor bgColor = themeManager.getColor("backgroundPrimary");
    renderer.fillRect(bounds, bgColor);

    // Draw border
    NomadUI::NUIColor borderColor = themeManager.getColor("border");
    renderer.strokeRect(bounds, 1, borderColor);

    // 🔥 VIEWPORT CULLING: Calculate visible track range
    const auto& layout = themeManager.getLayoutDimensions();
    float buttonSize = 30.0f;
    float trackStartY = buttonSize;
    
    // Find which tracks are actually visible
    int firstVisibleTrack = 0;
    int lastVisibleTrack = static_cast<int>(m_trackUIComponents.size());
    
    // Calculate visible range based on viewport
    if (layout.trackHeight > 0) {
        firstVisibleTrack = std::max(0, static_cast<int>((0 - trackStartY) / layout.trackHeight));
        lastVisibleTrack = std::min(static_cast<int>(m_trackUIComponents.size()), 
                                     static_cast<int>((bounds.height - trackStartY) / layout.trackHeight) + 1);
    }

    // Draw track separator lines (from control width to end) - full width
    float controlWidth = layout.trackControlsWidth;
    float currentY = buttonSize + layout.trackHeight; // Start after button and first track
    
    // Only draw separators for visible tracks
    for (int i = firstVisibleTrack; i < std::min(lastVisibleTrack, static_cast<int>(m_trackUIComponents.size()) - 1); ++i) {
        currentY = buttonSize + (i + 1) * layout.trackHeight;
        renderer.drawLine(
            NUIAbsolutePoint(bounds, controlWidth, currentY),
            NUIAbsolutePoint(bounds, bounds.width, currentY),
            1.0f,
            borderColor
        );
    }

    // Draw track count - positioned in top-right corner with proper margin
    std::string infoText = "Tracks: " + std::to_string(m_trackManager ? m_trackManager->getTrackCount() - (m_trackManager->getTrackCount() > 0 ? 1 : 0) : 0);  // Exclude preview track
    auto infoSize = renderer.measureText(infoText, 12);

    // Ensure text doesn't exceed bounds and position with proper margin
    float margin = layout.panelMargin;
    float maxTextWidth = bounds.width - 2 * margin;
    if (infoSize.width > maxTextWidth) {
        // Truncate if too long
        std::string truncatedText = infoText;
        while (!truncatedText.empty() && renderer.measureText(truncatedText, 12).width > maxTextWidth) {
            truncatedText = truncatedText.substr(0, truncatedText.length() - 1);
        }
        infoText = truncatedText + "...";
        infoSize = renderer.measureText(infoText, 12);
    }

    renderer.drawText(infoText,
                       NUIAbsolutePoint(bounds, bounds.width - infoSize.width - margin, 15),
                       12, themeManager.getColor("textSecondary"));

    // Draw playlist grid (vertical lines for time divisions) - full width to edges
    float gridStartX = bounds.x + layout.trackControlsWidth;
    float gridWidth = bounds.width - layout.trackControlsWidth;
    if (gridWidth > 0) {
        float gridStep = layout.gridLineSpacing;
        NomadUI::NUIColor gridColor = themeManager.getColor("border").withAlpha(0.5f);

        for (float x = gridStartX; x < bounds.x + bounds.width; x += gridStep) {
            renderer.drawLine(
                NUIAbsolutePoint(bounds, x - bounds.x, 0),
                NUIAbsolutePoint(bounds, x - bounds.x, bounds.height),
                1.0f,
                gridColor
            );
        }

        // Add alternating row backgrounds for better separation - full width
        NomadUI::NUIColor evenRowColor = themeManager.getColor("backgroundSecondary").darkened(0.02f);
        NomadUI::NUIColor oddRowColor = themeManager.getColor("backgroundSecondary").lightened(0.01f);
        float buttonSize = 30.0f; // Match button size from layoutTracks
        for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
            // Start row backgrounds from where tracks actually start (after add button)
            float rowY = bounds.y + buttonSize + (i * layout.trackHeight); // No spacing between tracks
            NomadUI::NUIColor rowColor = (i % 2 == 0) ? evenRowColor : oddRowColor;
            renderer.fillRect(NomadUI::NUIRect(gridStartX, rowY, gridWidth, layout.trackHeight), rowColor);
        }
    }

    // Render children (tracks and buttons) - this must come AFTER row backgrounds so + button renders on top
    renderChildren(renderer);
}

void TrackManagerUI::renderChildren(NomadUI::NUIRenderer& renderer) {
    // 🔥 VIEWPORT CULLING: Only render visible tracks + always render add button
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    NomadUI::NUIRect bounds = getBounds();
    
    float buttonSize = 30.0f;
    float trackStartY = buttonSize;
    
    // Calculate visible track range
    int firstVisibleTrack = 0;
    int lastVisibleTrack = static_cast<int>(m_trackUIComponents.size());
    
    if (layout.trackHeight > 0) {
        firstVisibleTrack = std::max(0, static_cast<int>((0 - trackStartY) / layout.trackHeight));
        lastVisibleTrack = std::min(static_cast<int>(m_trackUIComponents.size()), 
                                     static_cast<int>((bounds.height - trackStartY) / layout.trackHeight) + 1);
    }
    
    // Render all children but skip track UIComponents that are outside viewport
    const auto& children = getChildren();
    for (const auto& child : children) {
        if (!child->isVisible()) continue;
        
        // Always render the add button
        if (child == m_addTrackButton) {
            child->onRender(renderer);
            continue;
        }
        
        // Check if this child is a track UI component
        bool isTrackUI = false;
        int trackIndex = -1;
        for (size_t i = 0; i < m_trackUIComponents.size(); ++i) {
            if (child == m_trackUIComponents[i]) {
                isTrackUI = true;
                trackIndex = static_cast<int>(i);
                break;
            }
        }
        
        // If it's a track, only render if visible in viewport
        if (isTrackUI) {
            if (trackIndex >= firstVisibleTrack && trackIndex < lastVisibleTrack) {
                child->onRender(renderer);
            }
        } else {
            // Not a track UI, render normally (other UI elements)
            child->onRender(renderer);
        }
    }
    
    // Debug: Print culling stats occasionally
    static int frameCount = 0;
    if (++frameCount >= 300) {  // Every 300 frames
        int culledTracks = static_cast<int>(m_trackUIComponents.size()) - (lastVisibleTrack - firstVisibleTrack);
        if (culledTracks > 0) {
            Log::info("Viewport Culling: Rendering " + std::to_string(lastVisibleTrack - firstVisibleTrack) + 
                      "/" + std::to_string(m_trackUIComponents.size()) + " tracks (culled " + 
                      std::to_string(culledTracks) + ")");
        }
        frameCount = 0;
    }
}

void TrackManagerUI::onResize(int width, int height) {
    layoutTracks();
    NomadUI::NUIComponent::onResize(width, height);
}

} // namespace Audio
} // namespace Nomad
