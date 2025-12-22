// Set loop region (called from Main.cpp when loop preset changes)
void TrackManagerUI::setLoopRegion(double startBeat, double endBeat, bool enabled) {
    m_loopStartBeat = startBeat;
    m_loopEndBeat = endBeat;
    m_loopEnabled = enabled;
    m_cacheInvalidated = true;  // Redraw to show updated markers
}

// Render FL Studio-style loop markers on ruler
void TrackManagerUI::renderLoopMarkers(NomadUI::NUIRenderer& renderer, const NomadUI::NUIRect& rulerBounds) {
    if (m_loopEndBeat <= m_loopStartBeat) return;  // Invalid loop region
    
    auto& themeManager = NomadUI::NUIThemeManager::getInstance();
    const auto& layout = themeManager.getLayoutDimensions();
    
    // Calculate grid start (same as ruler)
    float controlAreaWidth = layout.trackControlsWidth;
    float gridStartX = rulerBounds.x + controlAreaWidth + 5.0f;
    float scrollbarWidth = 15.0f;
    float trackWidth = rulerBounds.width - scrollbarWidth;
    float gridWidth = trackWidth - controlAreaWidth - 10.0f;
    gridWidth = std::max(0.0f, gridWidth);
    float gridEndX = gridStartX + gridWidth;
    
    // Convert loop beats to pixel positions
    float loopStartX = gridStartX + (static_cast<float>(m_loopStartBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
    float loopEndX = gridStartX + (static_cast<float>(m_loopEndBeat) * m_pixelsPerBeat) - m_timelineScrollOffset;
    
    // Check if markers are visible
    bool startVisible = (loopStartX >= gridStartX && loopStartX <= gridEndX);
    bool endVisible = (loopEndX >= gridStartX && loopEndX <= gridEndX);
    
    if (!startVisible && !endVisible) return;  // Both markers off-screen
    
    // Color based on enabled state and hover
    auto accentColor = themeManager.getColor("accentPrimary");
    NomadUI::NUIColor markerColor;
    
    if (m_loopEnabled) {
        markerColor = accentColor.withAlpha(0.8f);  // Bright when active
    } else {
        markerColor = accentColor.withAlpha(0.3f);  // Dimmed when inactive
    }
    
    // Marker dimensions
    const float triangleWidth = 12.0f;
    const float triangleHeight = 10.0f;
    const float lineHeight = rulerBounds.height - triangleHeight;
    
    // === RENDER LOOP START MARKER ===
    if (startVisible) {
        NomadUI::NUIColor startColor = markerColor;
        if (m_hoveringLoopStart || m_isDraggingLoopStart) {
            startColor = accentColor;  // Full brightness on hover/drag
        }
        
        // Draw triangle pointing down
        NomadUI::NUIPoint p1(loopStartX, rulerBounds.y + triangleHeight);  // Bottom center
        NomadUI::NUIPoint p2(loopStartX - triangleWidth / 2, rulerBounds.y);  // Top left
        NomadUI::NUIPoint p3(loopStartX + triangleWidth / 2, rulerBounds.y);  // Top right
        
        // Fill triangle
        renderer.fillTriangle(p1, p2, p3, startColor);
        
        // Draw vertical line from triangle to bottom
        renderer.drawLine(
            NomadUI::NUIPoint(loopStartX, rulerBounds.y + triangleHeight),
            NomadUI::NUIPoint(loopStartX, rulerBounds.y + rulerBounds.height),
            2.0f,
            startColor
        );
    }
    
    // === RENDER LOOP END MARKER ===
    if (endVisible) {
        NomadUI::NUIColor endColor = markerColor;
        if (m_hoveringLoopEnd || m_isDraggingLoopEnd) {
            endColor = accentColor;  // Full brightness on hover/drag
        }
        
        // Draw triangle pointing down
        NomadUI::NUIPoint p1(loopEndX, rulerBounds.y + triangleHeight);  // Bottom center
        NomadUI::NUIPoint p2(loopEndX - triangleWidth / 2, rulerBounds.y);  // Top left
        NomadUI::NUIPoint p3(loopEndX + triangleWidth / 2, rulerBounds.y);  // Top right
        
        // Fill triangle
        renderer.fillTriangle(p1, p2, p3, endColor);
        
        // Draw vertical line from triangle to bottom
        renderer.drawLine(
            NomadUI::NUIPoint(loopEndX, rulerBounds.y + triangleHeight),
            NomadUI::NUIPoint(loopEndX, rulerBounds.y + rulerBounds.height),
            2.0f,
            endColor
        );
    }
}
