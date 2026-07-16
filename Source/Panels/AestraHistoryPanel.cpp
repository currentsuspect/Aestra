// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AestraHistoryPanel.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../AestraUI/Core/NUIThemeSystem.h"

using namespace AestraUI;

namespace Aestra {
namespace Audio {

static NUIColor categoryColor(const std::string& name) {
    auto& theme = NUIThemeManager::getInstance();
    if (name.find("Volume") != std::string::npos || name.find("Mute") != std::string::npos ||
        name.find("Solo") != std::string::npos || name.find("Pan") != std::string::npos ||
        name.find("Send") != std::string::npos)
        return theme.getColor("warning");
    if (name.find("Note") != std::string::npos || name.find("Pitch") != std::string::npos)
        return theme.getColor("success");
    if (name.find("Clip") != std::string::npos || name.find("Split") != std::string::npos ||
        name.find("Duplicate") != std::string::npos || name.find("Delete") != std::string::npos ||
        name.find("Move") != std::string::npos)
        return theme.getColor("secondary");
    if (name.find("Unit") != std::string::npos || name.find("Pattern") != std::string::npos)
        return theme.getColor("primary");
    if (name.find("Plugin") != std::string::npos || name.find("Bypass") != std::string::npos)
        return theme.getColor("accentMagenta");
    return theme.getColor("textSecondary");
}

AestraHistoryPanel::AestraHistoryPanel(std::shared_ptr<TrackManager> trackManager)
    : WindowPanel("HISTORY"), m_trackManager(std::move(trackManager))
{
    rebuildEntries();
}

void AestraHistoryPanel::refreshHistory() { rebuildEntries(); }

void AestraHistoryPanel::activateEntry(int index)
{
    if (!m_trackManager || index < 0 || index >= static_cast<int>(m_entries.size())) return;
    auto& history = m_trackManager->getCommandHistory();
    if (m_entries[index].isRedo) history.redoTo(m_entries[index].stackIndex);
    else history.undoTo(m_entries[index].stackIndex);
    refreshHistory();
    if (m_onHistoryChanged) m_onHistoryChanged();
}

void AestraHistoryPanel::rebuildEntries()
{
    m_entries.clear();
    if (!m_trackManager) return;

    const auto& history = m_trackManager->getCommandHistory();
    for (int i = 0; i < static_cast<int>(history.getRedoStack().size()); ++i) {
        std::string name = history.getRedoStack()[i]->getName();
        m_entries.push_back({name.empty() ? "(unnamed)" : name, true, i});
    }
    for (int i = 0; i < static_cast<int>(history.getUndoStack().size()); ++i) {
        std::string name = history.getUndoStack()[i]->getName();
        m_entries.push_back({name.empty() ? "(unnamed)" : name, false, i});
    }
    m_contentHeight = m_entries.size() * ROW_HEIGHT + ROW_HEIGHT;
    m_scrollY = std::min(m_scrollY, std::max(0.0f, m_contentHeight - ROW_HEIGHT));
}

void AestraHistoryPanel::onResize(int width, int height) {
    WindowPanel::onResize(width, height);
}

void AestraHistoryPanel::onRender(NUIRenderer& renderer)
{
    auto& theme = NUIThemeManager::getInstance();
    // Let WindowPanel draw title bar, border, close button — matches other panels exactly
    WindowPanel::onRender(renderer);

    auto bounds = getBounds();
    if (bounds.width <= 0 || bounds.height <= 0) return;

    // Content area starts below WindowPanel's title bar + padding
    float headerH = 36.0f; // WindowPanel title bar height
    float contentY = bounds.y + headerH;
    float contentH = bounds.height - headerH;

    // Empty state
    if (m_entries.empty()) {
        float cy = contentY + contentH * 0.5f;
        renderer.drawText("Start making changes", NUIPoint(bounds.x + LEFT_PAD, cy), 12.0f,
            theme.getColor("textSecondary").withAlpha(0.72f));
        return;
    }

    // Redo count for HEAD position
    int headRedo = 0;
    for (const auto& e : m_entries) { if (e.isRedo) ++headRedo; else break; }

    // Draw entries top-down from header, offset by the scroll position
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const auto& entry = m_entries[i];
        float rowY = contentY + i * ROW_HEIGHT - m_scrollY;

        // Skip entries outside visible area
        if (rowY + ROW_HEIGHT < contentY || rowY > contentY + contentH) continue;

        bool hovered = (i == m_hoveredEntry);
        if (hovered) {
            // Keep the lift subtle — the hover token is light, and a strong
            // fill washes out light row text.
            renderer.fillRect(NUIRect(bounds.x, rowY, bounds.width, ROW_HEIGHT),
                theme.getColor("hover").withAlpha(entry.isRedo ? 0.08f : 0.14f));
        }

        // Category dot
        NUIColor dotColor = entry.isRedo ? theme.getColor("textDisabled") : categoryColor(entry.name);
        renderer.fillCircle(NUIPoint(bounds.x + LEFT_PAD, rowY + ROW_HEIGHT * 0.5f), INDICATOR_SIZE * 0.5f, dotColor);

        // Text
        NUIColor textColor = entry.isRedo ? theme.getColor("textDisabled").withAlpha(0.86f)
                                          : theme.getColor("textPrimary").withAlpha(0.96f);
        renderer.drawText(entry.name, NUIPoint(bounds.x + TEXT_LEFT_PAD, rowY + 7.0f), 12.0f, textColor);

        // Separator
        renderer.drawLine(NUIPoint(bounds.x + TEXT_LEFT_PAD, rowY + ROW_HEIGHT),
            NUIPoint(bounds.x + bounds.width, rowY + ROW_HEIGHT), 0.5f, theme.getColor("divider").withAlpha(0.86f));
    }

    // HEAD marker
    float headY = contentY + headRedo * ROW_HEIGHT - m_scrollY;
    if (headY >= contentY && headY < contentY + contentH) {
        renderer.fillRect(NUIRect(bounds.x, headY, 3.0f, ROW_HEIGHT), theme.getColor("secondary").withAlpha(0.92f));
        renderer.drawText("HEAD", NUIPoint(bounds.x + 6.0f, headY + 7.0f), 9.0f,
            theme.getColor("secondary").withAlpha(0.88f));
        renderer.drawLine(NUIPoint(bounds.x + 42.0f, headY + ROW_HEIGHT * 0.5f),
            NUIPoint(bounds.x + bounds.width, headY + ROW_HEIGHT * 0.5f),
            0.5f, theme.getColor("secondary").withAlpha(0.24f));
    }
}

bool AestraHistoryPanel::onMouseEvent(const NUIMouseEvent& event)
{
    if (!isVisible()) return false;

    auto bounds = getBounds();
    float headerH = 32.0f;
    float contentY = bounds.y + headerH;
    float contentH = bounds.height - headerH;
    const bool inContent = bounds.contains(event.position) && event.position.y >= contentY;

    // Handle click on entries (scroll-aware)
    if (event.pressed && event.button == NUIMouseButton::Left && m_trackManager && inContent) {
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            float rowY = contentY + i * ROW_HEIGHT - m_scrollY;
            if (rowY + ROW_HEIGHT < contentY) continue;
            if (event.position.y >= rowY && event.position.y < rowY + ROW_HEIGHT) {
                activateEntry(i);
                return true;
            }
        }
    }

    // Handle hover
    if (event.type == NUIMouseEventType::Move) {
        m_hoveredEntry = -1;
        if (inContent) {
            for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
                float rowY = contentY + i * ROW_HEIGHT - m_scrollY;
                if (rowY + ROW_HEIGHT < contentY) continue;
                if (event.position.y >= rowY && event.position.y < rowY + ROW_HEIGHT) { m_hoveredEntry = i; break; }
            }
        }
    }

    // Handle scroll
    if (event.type == NUIMouseEventType::Scroll) {
        float maxScroll = std::max(0.0f, m_contentHeight - contentH);
        m_scrollY = std::max(0.0f, std::min(m_scrollY - event.wheelDelta * 30.0f, maxScroll));
        return true;
    }

    // Delegate to WindowPanel for title bar, buttons, click-through prevention
    return WindowPanel::onMouseEvent(event);
}

} // namespace Audio
} // namespace Aestra
