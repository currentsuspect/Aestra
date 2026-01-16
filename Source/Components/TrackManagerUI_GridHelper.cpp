void TrackManagerUI::drawGrid(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& bounds, float gridStartX, float gridWidth, float timelineScrollOffset) {
    auto& themeManager = AestraUI::NUIThemeManager::getInstance();
    
    // Draw Dynamic Snap Grid
    double snapDur = AestraUI::MusicTheory::getSnapDuration(m_snapSetting);
    if (m_snapSetting == AestraUI::SnapGrid::None) snapDur = 1.0;
    if (snapDur <= 0.0001) snapDur = 1.0;

    // Dynamic Density: DEBUG - DISABLED (Allow all densities)
    // while ((m_pixelsPerBeat * snapDur) < 5.0f) {
    //     snapDur *= 2.0;
    // }

    double startBeat = timelineScrollOffset / m_pixelsPerBeat;
    double endBeat = startBeat + (gridWidth / m_pixelsPerBeat);

    // Round start to nearest snap
    double current = std::floor(startBeat / snapDur) * snapDur;
    
    for (; current <= endBeat + snapDur; current += snapDur) {
        float xPos = bounds.x + gridStartX + static_cast<float>(current * m_pixelsPerBeat) - timelineScrollOffset;
        
        // Strict culling within valid grid area
        if (xPos < bounds.x + gridStartX || xPos > bounds.x + gridStartX + gridWidth) continue;

        // Hierarchy logic
        bool isBar = (std::fmod(std::abs(current), (double)m_beatsPerBar) < 0.001);
        bool isBeat = (std::fmod(std::abs(current), 1.0) < 0.001);
        
        // Draw Vertical Grid Line (Full Height)
        float trackAreaTop = bounds.y;
        float trackAreaBottom = bounds.y + bounds.height;
        
        if (isBar) {
            // Bar: White 50%
            renderer.drawLine(
                AestraUI::NUIPoint(xPos, trackAreaTop),
                AestraUI::NUIPoint(xPos, trackAreaBottom),
                1.0f,
                AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.50f)
            );
        }
        else if (isBeat) {
            // Beat: White 25%
            renderer.drawLine(
                AestraUI::NUIPoint(xPos, trackAreaTop),
                AestraUI::NUIPoint(xPos, trackAreaBottom),
                1.0f,
                AestraUI::NUIColor(1.0f, 1.0f, 1.0f, 0.25f)
            );
        }
        else {
            // Subdivision: DEBUG RED and THICK (to verify it draws)
            renderer.drawLine(
                AestraUI::NUIPoint(xPos, trackAreaTop),
                AestraUI::NUIPoint(xPos, trackAreaBottom),
                2.0f, // Thicker line
                AestraUI::NUIColor(1.0f, 0.0f, 0.0f, 0.70f) // Bright Red
            );
        }
    }
}
