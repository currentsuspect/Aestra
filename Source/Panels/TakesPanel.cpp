// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TakesPanel.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <algorithm>
#include <chrono>
#include <unordered_map>

using namespace AestraUI;

namespace Aestra {
namespace Audio {

namespace {
constexpr float ACTION_BUTTON_PAD = 8.0f;
constexpr float TOOLBAR_BUTTON_HEIGHT = 22.0f;
} // namespace

TakesPanel::TakesPanel() : WindowPanel("TAKES") {
    setMinimumPanelSize(280.0f, 260.0f);
}

void TakesPanel::refreshTakes() {
    rebuildRows();
    setDirty(true);
}

void TakesPanel::rebuildRows() {
    std::string previousSelectedId;
    if (m_selectedTake >= 0 && m_selectedTake < static_cast<int>(m_takeRows.size())) {
        previousSelectedId = m_takeRows[static_cast<size_t>(m_selectedTake)].take.id;
    }

    m_takeRows.clear();
    m_snapshotRows.clear();
    m_manifestError.clear();

    if (m_takesProvider) {
        TakeManager::Manifest manifest = m_takesProvider();
        if (manifest.ok) {
            // Depth = length of the parent chain; guards against cycles/orphans.
            std::unordered_map<std::string, std::string> parentOf;
            for (const auto& take : manifest.takes) {
                parentOf[take.id] = take.parentId;
            }
            for (const auto& take : manifest.takes) {
                int depth = 0;
                std::string cursor = take.parentId;
                while (!cursor.empty() && depth < 8) {
                    auto it = parentOf.find(cursor);
                    if (it == parentOf.end()) {
                        break;
                    }
                    ++depth;
                    cursor = it->second;
                }
                m_takeRows.push_back({take, depth});
            }
        } else if (manifest.errorMessage != "No Takes manifest") {
            m_manifestError = manifest.errorMessage;
        }
    }

    if (m_snapshotsProvider) {
        auto snapshots = m_snapshotsProvider();
        if (snapshots.size() > MAX_SNAPSHOT_ROWS) {
            snapshots.resize(MAX_SNAPSHOT_ROWS);
        }
        m_snapshotRows = std::move(snapshots);
    }

    // Selection follows the take's identity, not its row index.
    m_selectedTake = -1;
    if (!previousSelectedId.empty()) {
        for (int i = 0; i < static_cast<int>(m_takeRows.size()); ++i) {
            if (m_takeRows[static_cast<size_t>(i)].take.id == previousSelectedId) {
                m_selectedTake = i;
                break;
            }
        }
    }
    if (m_selectedSnapshot >= static_cast<int>(m_snapshotRows.size())) {
        m_selectedSnapshot = -1;
    }
    if (m_renameActive) {
        // The renamed take may have vanished (external change) — drop the edit.
        bool stillPresent = false;
        for (const auto& row : m_takeRows) {
            if (row.take.id == m_renameTakeId) {
                stillPresent = true;
                break;
            }
        }
        if (!stillPresent) {
            cancelRename();
        }
    }
}

TakesPanel::Layout TakesPanel::computeLayout() const {
    Layout layout;
    const auto bounds = getBounds();
    const float x = bounds.x;
    const float w = bounds.width;
    float top = bounds.y + HEADER_HEIGHT;
    float bottom = bounds.y + bounds.height;

    if (!m_statusText.empty()) {
        bottom -= STATUS_HEIGHT;
        layout.statusLine = NUIRect(x, bottom, w, STATUS_HEIGHT);
    }

    if (!m_snapshotRows.empty()) {
        const float snapshotListH = static_cast<float>(m_snapshotRows.size()) * ROW_HEIGHT;
        bottom -= snapshotListH;
        layout.snapshotList = NUIRect(x, bottom, w, snapshotListH);
        bottom -= SECTION_HEADER_HEIGHT;
        layout.snapshotHeader = NUIRect(x, bottom, w, SECTION_HEADER_HEIGHT);
    }

    layout.actionBarVisible = (m_selectedTake >= 0 || m_selectedSnapshot >= 0);
    if (layout.actionBarVisible) {
        bottom -= ACTION_BAR_HEIGHT;
        layout.actionBar = NUIRect(x, bottom, w, ACTION_BAR_HEIGHT);
    }

    layout.toolbar = NUIRect(x, top, w, TOOLBAR_HEIGHT);
    const float buttonY = top + (TOOLBAR_HEIGHT - TOOLBAR_BUTTON_HEIGHT) * 0.5f;
    const float buttonW = (w - LEFT_PAD * 3.0f) * 0.5f;
    layout.newTakeButton = NUIRect(x + LEFT_PAD, buttonY, buttonW, TOOLBAR_BUTTON_HEIGHT);
    layout.saveActiveButton = NUIRect(x + LEFT_PAD * 2.0f + buttonW, buttonY, buttonW, TOOLBAR_BUTTON_HEIGHT);

    top += TOOLBAR_HEIGHT;
    layout.takesList = NUIRect(x, top, w, std::max(0.0f, bottom - top));
    return layout;
}

int TakesPanel::takeRowAt(const NUIPoint& point, const Layout& layout) const {
    if (!layout.takesList.contains(point)) {
        return -1;
    }
    const int index = static_cast<int>((point.y - layout.takesList.y + m_scrollY) / ROW_HEIGHT);
    if (index < 0 || index >= static_cast<int>(m_takeRows.size())) {
        return -1;
    }
    return index;
}

int TakesPanel::snapshotRowAt(const NUIPoint& point, const Layout& layout) const {
    if (m_snapshotRows.empty() || !layout.snapshotList.contains(point)) {
        return -1;
    }
    const int index = static_cast<int>((point.y - layout.snapshotList.y) / ROW_HEIGHT);
    if (index < 0 || index >= static_cast<int>(m_snapshotRows.size())) {
        return -1;
    }
    return index;
}

std::vector<std::string> TakesPanel::actionButtonLabels() const {
    if (m_selectedSnapshot >= 0) {
        return {"RESTORE"};
    }
    if (m_selectedTake >= 0) {
        const bool active = m_takeRows[static_cast<size_t>(m_selectedTake)].take.active;
        if (active) {
            // The active take can't be opened (it already is) or deleted
            // (the working state must never be pulled out from under itself).
            return {"RENAME", "DUP", "BRANCH"};
        }
        return {"OPEN", "RENAME", "DUP", "BRANCH", "DELETE"};
    }
    return {};
}

std::vector<NUIRect> TakesPanel::actionButtonRects(const Layout& layout) const {
    std::vector<NUIRect> rects;
    if (!layout.actionBarVisible) {
        return rects;
    }
    const auto labels = actionButtonLabels();
    if (labels.empty()) {
        return rects;
    }
    const float buttonH = ACTION_BAR_HEIGHT - 8.0f;
    const float totalPad = LEFT_PAD * 2.0f + ACTION_BUTTON_PAD * static_cast<float>(labels.size() - 1);
    const float buttonW = (layout.actionBar.width - totalPad) / static_cast<float>(labels.size());
    float bx = layout.actionBar.x + LEFT_PAD;
    for (size_t i = 0; i < labels.size(); ++i) {
        rects.emplace_back(bx, layout.actionBar.y + 4.0f, buttonW, buttonH);
        bx += buttonW + ACTION_BUTTON_PAD;
    }
    return rects;
}

void TakesPanel::selectTake(int index) {
    if (index < -1 || index >= static_cast<int>(m_takeRows.size())) {
        index = -1;
    }
    if (m_renameActive && index != m_selectedTake) {
        cancelRename();
    }
    m_selectedTake = index;
    if (index >= 0) {
        m_selectedSnapshot = -1;
    }
    setDirty(true);
}

void TakesPanel::selectSnapshot(int index) {
    if (index < -1 || index >= static_cast<int>(m_snapshotRows.size())) {
        index = -1;
    }
    if (m_renameActive) {
        cancelRename();
    }
    m_selectedSnapshot = index;
    if (index >= 0) {
        m_selectedTake = -1;
    }
    setDirty(true);
}

void TakesPanel::setStatus(const std::string& text) {
    m_statusText = text;
    setDirty(true);
}

bool TakesPanel::invokeAction(const std::function<bool()>& action, const char* failureMessage) {
    if (!action) {
        return false;
    }
    setStatus("");
    const bool ok = action();
    if (!ok) {
        setStatus(failureMessage);
    }
    refreshTakes();
    return ok;
}

bool TakesPanel::requestCreateTake() {
    return invokeAction([this]() { return m_onCreateTake && m_onCreateTake(); }, "Could not create take");
}

bool TakesPanel::requestSaveActiveTake() {
    return invokeAction([this]() { return m_onSaveActiveTake && m_onSaveActiveTake(); },
                        "Could not save the active take");
}

bool TakesPanel::requestOpenSelected() {
    if (m_selectedTake < 0) {
        return false;
    }
    const std::string takeId = m_takeRows[static_cast<size_t>(m_selectedTake)].take.id;
    return invokeAction([this, takeId]() { return m_onOpenTake && m_onOpenTake(takeId); }, "Could not open take");
}

bool TakesPanel::requestDuplicateSelected() {
    if (m_selectedTake < 0) {
        return false;
    }
    const std::string takeId = m_takeRows[static_cast<size_t>(m_selectedTake)].take.id;
    return invokeAction([this, takeId]() { return m_onDuplicateTake && m_onDuplicateTake(takeId); },
                        "Could not duplicate take");
}

bool TakesPanel::requestBranchSelected() {
    if (m_selectedTake < 0) {
        return false;
    }
    const std::string takeId = m_takeRows[static_cast<size_t>(m_selectedTake)].take.id;
    return invokeAction([this, takeId]() { return m_onBranchTake && m_onBranchTake(takeId); },
                        "Could not branch from take");
}

bool TakesPanel::requestDeleteSelected() {
    if (m_selectedTake < 0) {
        return false;
    }
    const auto& take = m_takeRows[static_cast<size_t>(m_selectedTake)].take;
    if (take.active) {
        setStatus("Switch to another take before deleting this one");
        return false;
    }
    const std::string takeId = take.id;
    return invokeAction([this, takeId]() { return m_onDeleteTake && m_onDeleteTake(takeId); },
                        "Could not delete take");
}

bool TakesPanel::requestRestoreSelectedSnapshot() {
    if (m_selectedSnapshot < 0) {
        return false;
    }
    const std::string path = m_snapshotRows[static_cast<size_t>(m_selectedSnapshot)].path;
    return invokeAction([this, path]() { return m_onRestoreSnapshot && m_onRestoreSnapshot(path); },
                        "Could not restore snapshot");
}

void TakesPanel::beginRenameSelected() {
    if (m_selectedTake < 0) {
        return;
    }
    const auto& take = m_takeRows[static_cast<size_t>(m_selectedTake)].take;
    m_renameActive = true;
    m_renameTakeId = take.id;
    m_renameBuffer = take.name;
    // Printable characters are delivered through the focused-component text
    // path (see AestraWindowManager charCallback), so claim focus for the edit.
    setFocused(true);
    setStatus("");
    setDirty(true);
}

bool TakesPanel::commitRename() {
    if (!m_renameActive) {
        return false;
    }
    const std::string takeId = m_renameTakeId;
    const std::string name = m_renameBuffer;
    m_renameActive = false;
    m_renameTakeId.clear();
    m_renameBuffer.clear();
    setFocused(false);
    if (name.empty()) {
        setStatus("Take name cannot be empty");
        return false;
    }
    return invokeAction([this, takeId, name]() { return m_onRenameTake && m_onRenameTake(takeId, name); },
                        "Could not rename take");
}

void TakesPanel::cancelRename() {
    m_renameActive = false;
    m_renameTakeId.clear();
    m_renameBuffer.clear();
    setFocused(false);
    setDirty(true);
}

bool TakesPanel::onKeyEvent(const NUIKeyEvent& event) {
    return handleKeyEvent(event);
}

bool TakesPanel::handleKeyEvent(const NUIKeyEvent& event) {
    if (!m_renameActive || !isVisible() || !event.pressed) {
        return false;
    }

    if (event.keyCode == NUIKeyCode::Enter) {
        commitRename();
        return true;
    }
    if (event.keyCode == NUIKeyCode::Escape) {
        cancelRename();
        return true;
    }
    if (event.keyCode == NUIKeyCode::Backspace) {
        if (!m_renameBuffer.empty()) {
            m_renameBuffer.pop_back();
            setDirty(true);
        }
        return true;
    }
    if (event.character >= 32 && event.character != 127) {
        if (m_renameBuffer.size() < 128) {
            m_renameBuffer.push_back(event.character);
            setDirty(true);
        }
        return true;
    }
    // While editing, swallow other presses so global shortcuts don't fire.
    return true;
}

bool TakesPanel::runRowAction(int actionIndex) {
    const auto labels = actionButtonLabels();
    if (actionIndex < 0 || actionIndex >= static_cast<int>(labels.size())) {
        return false;
    }
    const std::string& label = labels[static_cast<size_t>(actionIndex)];
    if (label == "OPEN") {
        return requestOpenSelected();
    }
    if (label == "RENAME") {
        beginRenameSelected();
        return true;
    }
    if (label == "DUP") {
        return requestDuplicateSelected();
    }
    if (label == "BRANCH") {
        return requestBranchSelected();
    }
    if (label == "DELETE") {
        return requestDeleteSelected();
    }
    if (label == "RESTORE") {
        return requestRestoreSelectedSnapshot();
    }
    return false;
}

std::string TakesPanel::relativeTimeLabel(uint64_t epochMs) {
    if (epochMs == 0) {
        return {};
    }
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const int64_t deltaSec = std::max<int64_t>(0, (static_cast<int64_t>(now) - static_cast<int64_t>(epochMs)) / 1000);
    if (deltaSec < 60) {
        return "now";
    }
    if (deltaSec < 3600) {
        return std::to_string(deltaSec / 60) + "m";
    }
    if (deltaSec < 86400) {
        return std::to_string(deltaSec / 3600) + "h";
    }
    return std::to_string(deltaSec / 86400) + "d";
}

void TakesPanel::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    WindowPanel::onRender(renderer);

    const auto bounds = getBounds();
    if (bounds.width <= 0 || bounds.height <= 0 || isMinimized()) {
        return;
    }

    const Layout layout = computeLayout();

    // Toolbar buttons
    const auto drawButton = [&](const NUIRect& rect, const std::string& label, bool accent) {
        renderer.fillRoundedRect(rect, 4.0f,
                                 (accent ? theme.getColor("accentPrimary") : theme.getColor("backgroundSecondary"))
                                     .withAlpha(accent ? 0.28f : 0.9f));
        const NUISize textSize = renderer.measureText(label, 11.0f);
        renderer.drawText(label,
                          NUIPoint(rect.x + (rect.width - textSize.width) * 0.5f,
                                   rect.y + (rect.height - textSize.height) * 0.5f),
                          11.0f, theme.getColor(accent ? "textPrimary" : "textSecondary"));
    };
    drawButton(layout.newTakeButton, "+ NEW TAKE", true);
    drawButton(layout.saveActiveButton, "SAVE ACTIVE", false);
    renderer.drawLine(NUIPoint(bounds.x, layout.toolbar.y + layout.toolbar.height),
                      NUIPoint(bounds.x + bounds.width, layout.toolbar.y + layout.toolbar.height), 0.5f,
                      theme.getColor("divider").withAlpha(0.86f));

    // Takes list
    if (m_takeRows.empty()) {
        const std::string hint = !m_manifestError.empty()
                                     ? ("Takes unavailable: " + m_manifestError)
                                     : "No takes yet — save a version with + NEW TAKE";
        renderer.drawText(hint, NUIPoint(bounds.x + LEFT_PAD, layout.takesList.y + ROW_HEIGHT * 0.6f), 12.0f,
                          theme.getColor("textSecondary").withAlpha(0.72f));
    }
    for (int i = 0; i < static_cast<int>(m_takeRows.size()); ++i) {
        const auto& row = m_takeRows[static_cast<size_t>(i)];
        const float rowY = layout.takesList.y + static_cast<float>(i) * ROW_HEIGHT - m_scrollY;
        if (rowY + ROW_HEIGHT < layout.takesList.y || rowY > layout.takesList.y + layout.takesList.height) {
            continue;
        }

        if (i == m_selectedTake) {
            renderer.fillRect(NUIRect(bounds.x, rowY, bounds.width, ROW_HEIGHT),
                              theme.getColor("accentPrimary").withAlpha(0.18f));
        } else if (i == m_hoveredTake) {
            renderer.fillRect(NUIRect(bounds.x, rowY, bounds.width, ROW_HEIGHT),
                              theme.getColor("hover").withAlpha(0.12f));
        }

        const float indent = static_cast<float>(row.depth) * DEPTH_INDENT;
        const NUIColor dotColor =
            row.take.active ? theme.getColor("accentPrimary") : theme.getColor("textDisabled").withAlpha(0.7f);
        renderer.fillCircle(NUIPoint(bounds.x + LEFT_PAD + indent, rowY + ROW_HEIGHT * 0.5f), 3.0f, dotColor);

        const bool editingThisRow = m_renameActive && row.take.id == m_renameTakeId;
        if (editingThisRow) {
            renderer.fillRect(NUIRect(bounds.x + TEXT_LEFT_PAD + indent - 4.0f, rowY + 3.0f,
                                      bounds.width - TEXT_LEFT_PAD - indent - LEFT_PAD, ROW_HEIGHT - 6.0f),
                              theme.getColor("backgroundSecondary").withAlpha(0.95f));
            renderer.drawText(m_renameBuffer + "|", NUIPoint(bounds.x + TEXT_LEFT_PAD + indent, rowY + 7.0f), 12.0f,
                              theme.getColor("textPrimary"));
        } else {
            const NUIColor textColor = row.take.active ? theme.getColor("textPrimary")
                                                       : theme.getColor("textPrimary").withAlpha(0.82f);
            renderer.drawText(row.take.name, NUIPoint(bounds.x + TEXT_LEFT_PAD + indent, rowY + 7.0f), 12.0f,
                              textColor);
            const std::string timeLabel = relativeTimeLabel(row.take.updatedAtMs);
            if (!timeLabel.empty()) {
                const NUISize timeSize = renderer.measureText(timeLabel, 10.0f);
                renderer.drawText(timeLabel,
                                  NUIPoint(bounds.x + bounds.width - LEFT_PAD - timeSize.width, rowY + 8.0f), 10.0f,
                                  theme.getColor("textSecondary").withAlpha(0.7f));
            }
        }

        renderer.drawLine(NUIPoint(bounds.x + TEXT_LEFT_PAD, rowY + ROW_HEIGHT),
                          NUIPoint(bounds.x + bounds.width, rowY + ROW_HEIGHT), 0.5f,
                          theme.getColor("divider").withAlpha(0.7f));
    }

    // Action bar for the current selection
    if (layout.actionBarVisible) {
        renderer.fillRect(layout.actionBar, theme.getColor("backgroundSecondary").withAlpha(0.55f));
        const auto labels = actionButtonLabels();
        const auto rects = actionButtonRects(layout);
        for (size_t i = 0; i < labels.size() && i < rects.size(); ++i) {
            const bool hovered = static_cast<int>(i) == m_hoveredAction;
            const bool destructive = labels[i] == "DELETE";
            const char* fillToken = destructive ? "warning" : (hovered ? "accentPrimary" : "borderSubtle");
            renderer.fillRoundedRect(rects[i], 4.0f,
                                     theme.getColor(fillToken).withAlpha(hovered ? 0.32f : (destructive ? 0.2f : 0.5f)));
            const NUISize textSize = renderer.measureText(labels[i], 10.0f);
            renderer.drawText(labels[i],
                              NUIPoint(rects[i].x + (rects[i].width - textSize.width) * 0.5f,
                                       rects[i].y + (rects[i].height - textSize.height) * 0.5f),
                              10.0f, theme.getColor("textPrimary").withAlpha(0.92f));
        }
    }

    // Recovery snapshot section — automatic save history, restore-only.
    if (!m_snapshotRows.empty()) {
        renderer.fillRect(layout.snapshotHeader, theme.getColor("backgroundSecondary").withAlpha(0.4f));
        renderer.drawText("RECOVERY SNAPSHOTS", NUIPoint(bounds.x + LEFT_PAD, layout.snapshotHeader.y + 5.0f), 9.0f,
                          theme.getColor("textSecondary").withAlpha(0.8f));
        for (int i = 0; i < static_cast<int>(m_snapshotRows.size()); ++i) {
            const float rowY = layout.snapshotList.y + static_cast<float>(i) * ROW_HEIGHT;
            if (i == m_selectedSnapshot) {
                renderer.fillRect(NUIRect(bounds.x, rowY, bounds.width, ROW_HEIGHT),
                                  theme.getColor("accentPrimary").withAlpha(0.18f));
            } else if (i == m_hoveredSnapshot) {
                renderer.fillRect(NUIRect(bounds.x, rowY, bounds.width, ROW_HEIGHT),
                                  theme.getColor("hover").withAlpha(0.12f));
            }
            renderer.fillCircle(NUIPoint(bounds.x + LEFT_PAD, rowY + ROW_HEIGHT * 0.5f), 3.0f,
                                theme.getColor("warning").withAlpha(0.75f));
            renderer.drawText(m_snapshotRows[static_cast<size_t>(i)].label,
                              NUIPoint(bounds.x + TEXT_LEFT_PAD, rowY + 7.0f), 11.0f,
                              theme.getColor("textSecondary").withAlpha(0.95f));
        }
    }

    if (!m_statusText.empty()) {
        renderer.fillRect(layout.statusLine, theme.getColor("warning").withAlpha(0.14f));
        renderer.drawText(m_statusText, NUIPoint(bounds.x + LEFT_PAD, layout.statusLine.y + 3.0f), 10.0f,
                          theme.getColor("warning").withAlpha(0.95f));
    }
}

bool TakesPanel::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) {
        return false;
    }
    if (isMinimized()) {
        return WindowPanel::onMouseEvent(event);
    }

    const Layout layout = computeLayout();

    if (event.type == NUIMouseEventType::Move) {
        m_hoveredTake = takeRowAt(event.position, layout);
        m_hoveredSnapshot = snapshotRowAt(event.position, layout);
        m_hoveredAction = -1;
        const auto rects = actionButtonRects(layout);
        for (size_t i = 0; i < rects.size(); ++i) {
            if (rects[i].contains(event.position)) {
                m_hoveredAction = static_cast<int>(i);
                break;
            }
        }
    }

    if (event.type == NUIMouseEventType::Scroll && layout.takesList.contains(event.position)) {
        const float contentHeight = static_cast<float>(m_takeRows.size()) * ROW_HEIGHT;
        const float maxScroll = std::max(0.0f, contentHeight - layout.takesList.height);
        m_scrollY = std::clamp(m_scrollY - event.wheelDelta * 30.0f, 0.0f, maxScroll);
        setDirty(true);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (layout.newTakeButton.contains(event.position)) {
            requestCreateTake();
            return true;
        }
        if (layout.saveActiveButton.contains(event.position)) {
            requestSaveActiveTake();
            return true;
        }

        const auto rects = actionButtonRects(layout);
        for (size_t i = 0; i < rects.size(); ++i) {
            if (rects[i].contains(event.position)) {
                runRowAction(static_cast<int>(i));
                return true;
            }
        }

        const int takeRow = takeRowAt(event.position, layout);
        if (takeRow >= 0) {
            if (event.type == NUIMouseEventType::DoubleClick) {
                selectTake(takeRow);
                if (!m_takeRows[static_cast<size_t>(takeRow)].take.active) {
                    requestOpenSelected();
                }
            } else {
                selectTake(takeRow);
            }
            return true;
        }

        const int snapshotRow = snapshotRowAt(event.position, layout);
        if (snapshotRow >= 0) {
            selectSnapshot(snapshotRow);
            return true;
        }
    }

    // Title bar, drag, resize, close, click-swallow inside panel bounds.
    return WindowPanel::onMouseEvent(event);
}

} // namespace Audio
} // namespace Aestra
