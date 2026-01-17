// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUI/Core/NUIComponent.h"
#include "../AestraUI/Core/NUIAdaptiveFPS.h"
#include <memory>
#include <sstream>
#include <iomanip>

/**
 * @brief Live FPS Display Overlay
 * 
 * Shows real-time adaptive FPS statistics in the top-right corner of the window.
 */
class FPSDisplay : public AestraUI::NUIComponent {
public:
    FPSDisplay(AestraUI::NUIAdaptiveFPS* adaptiveFPS) 
        : m_adaptiveFPS(adaptiveFPS)
        , m_visible(false)  // Changed from true to false - FPS debug off by default
    {
        // Position in top-right corner
        setBounds(AestraUI::NUIRect(0, 0, 320, 120));
    }

    void onRender(AestraUI::NUIRenderer& renderer) override {
        if (!m_visible || !m_adaptiveFPS) {
            return;
        }

        auto stats = m_adaptiveFPS->getStats();
        auto bounds = getBounds();

        // Background panel (semi-transparent dark)
        AestraUI::NUIColor bgColor(0.0f, 0.0f, 0.0f, 0.75f);
        renderer.fillRect(bounds, bgColor);
        
        // Border
        AestraUI::NUIColor borderColor(0.3f, 0.3f, 0.3f, 0.9f);
        renderer.strokeRect(bounds, 1.0f, borderColor);

        float textX = bounds.x + 10;
        float textY = bounds.y + 10;
        float lineHeight = 18.0f;

        // Title with proper baseline alignment
        AestraUI::NUIColor titleColor(0.4f, 0.8f, 1.0f, 1.0f); // Light blue
        float titleFontSize = 14.0f;
        float titleY = textY + titleFontSize * 0.75f; // Proper baseline offset
        renderer.drawText("ADAPTIVE FPS MONITOR", AestraUI::NUIPoint(textX, titleY), titleFontSize, titleColor);
        textY += lineHeight + 5;

        // FPS info
        std::stringstream ss;
        
        // Target FPS (cyan)
        AestraUI::NUIColor labelColor(0.6f, 0.6f, 0.6f, 1.0f);
        AestraUI::NUIColor valueColor(0.2f, 1.0f, 0.2f, 1.0f); // Green
        
        ss.str("");
        ss << "Target: " << std::fixed << std::setprecision(1) << stats.currentTargetFPS << " FPS";
        renderer.drawText(ss.str(), AestraUI::NUIPoint(textX, textY), 12.0f, valueColor);
        textY += lineHeight;

        // Actual FPS - color based on performance
        ss.str("");
        ss << "Actual: " << std::fixed << std::setprecision(1) << stats.actualFPS << " FPS";
        AestraUI::NUIColor actualColor;
        if (stats.actualFPS >= stats.currentTargetFPS * 0.95f) {
            actualColor = AestraUI::NUIColor(0.2f, 1.0f, 0.2f, 1.0f); // Green - good
        } else if (stats.actualFPS >= stats.currentTargetFPS * 0.75f) {
            actualColor = AestraUI::NUIColor(1.0f, 1.0f, 0.2f, 1.0f); // Yellow - warning
        } else {
            actualColor = AestraUI::NUIColor(1.0f, 0.3f, 0.2f, 1.0f); // Red - bad
        }
        renderer.drawText(ss.str(), AestraUI::NUIPoint(textX, textY), 12.0f, actualColor);
        textY += lineHeight;

        // Frame time
        ss.str("");
        ss << "Frame: " << std::fixed << std::setprecision(2) << (stats.averageFrameTime * 1000.0) << " ms";
        renderer.drawText(ss.str(), AestraUI::NUIPoint(textX, textY), 12.0f, labelColor);
        textY += lineHeight;

        // Status indicators
        ss.str("");
        ss << "Active: " << (stats.userActive ? "YES" : "NO") 
           << "  |  Can60: " << (stats.canSustain60 ? "YES" : "NO");
        AestraUI::NUIColor statusColor = stats.userActive 
            ? AestraUI::NUIColor(1.0f, 0.8f, 0.2f, 1.0f)  // Orange when active
            : AestraUI::NUIColor(0.6f, 0.6f, 0.6f, 1.0f); // Gray when idle
        renderer.drawText(ss.str(), AestraUI::NUIPoint(textX, textY), 11.0f, statusColor);
        textY += lineHeight;

        // Mode
        ss.str("");
        ss << "Mode: ";
        switch (m_adaptiveFPS->getMode()) {
            case AestraUI::NUIAdaptiveFPS::Mode::Auto: ss << "Auto"; break;
            case AestraUI::NUIAdaptiveFPS::Mode::Locked30: ss << "Locked 30"; break;
            case AestraUI::NUIAdaptiveFPS::Mode::Locked60: ss << "Locked 60"; break;
        }
        renderer.drawText(ss.str(), AestraUI::NUIPoint(textX, textY), 11.0f, labelColor);
    }

    void onUpdate(double deltaTime) override {
        // Position in top-right corner
        if (getParent()) {
            auto parentBounds = getParent()->getBounds();
            float x = parentBounds.width - 330; // 320 width + 10 margin
            float y = 10; // Top margin
            setBounds(AestraUI::NUIRect(x, y, 320, 120));
        }
    }

    void setVisible(bool visible) {
        m_visible = visible;
    }

    bool isVisible() const {
        return m_visible;
    }

    void toggle() {
        m_visible = !m_visible;
    }

private:
    AestraUI::NUIAdaptiveFPS* m_adaptiveFPS;
    bool m_visible;
};
