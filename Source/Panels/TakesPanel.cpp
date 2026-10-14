// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "TakesPanel.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <unordered_map>

using namespace AestraUI;

namespace Aestra {
namespace Audio {

namespace {
constexpr float kPad = 24.0f;

uint64_t nowMs() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

void dashedLine(NUIRenderer& r, NUIPoint a, NUIPoint b, const NUIColor& color, float dash = 5.0f, float gap = 4.0f) {
    const float len = std::hypot(b.x - a.x, b.y - a.y);
    if (len <= 0.0f) return;
    const float dx = (b.x - a.x) / len, dy = (b.y - a.y) / len;
    for (float t = 0.0f; t < len; t += dash + gap) {
        const float e = std::min(len, t + dash);
        r.drawLine({a.x + dx * t, a.y + dy * t}, {a.x + dx * e, a.y + dy * e}, 1.4f, color);
    }
}

// A fork: down from the parent, then an elbow along the child's lane.
void forkCurve(NUIRenderer& r, NUIPoint from, NUIPoint to, const NUIColor& color) {
    const float radius = std::min({18.0f, std::fabs(to.y - from.y), std::fabs(to.x - from.x)});
    const NUIPoint corner{from.x, to.y};
    NUIPoint prev = from;
    r.drawLine(prev, {from.x, to.y - radius}, 2.0f, color);
    prev = {from.x, to.y - radius};
    for (int i = 1; i <= 8; ++i) {
        const float t = static_cast<float>(i) / 8.0f;
        const float u = 1.0f - t;
        const NUIPoint p{u * u * from.x + 2.0f * u * t * corner.x + t * t * (from.x + radius),
                         u * u * (to.y - radius) + 2.0f * u * t * corner.y + t * t * to.y};
        r.drawLine(prev, p, 2.0f, color);
        prev = p;
    }
    r.drawLine(prev, to, 2.0f, color);
}
} // namespace

TakesPanel::TakesPanel() : WindowPanel("VERSIONS") {
    setMinimumPanelSize(760.0f, 400.0f);
}

NUIColor TakesPanel::laneColor(int lane) {
    static const uint32_t palette[] = {0xff7c3aed, 0xff67e8f9, 0xfff5a83a, 0xfff472b6, 0xff8bc34a, 0xff60a5fa};
    return NUIColor::fromHex(palette[static_cast<size_t>(std::max(0, lane)) % (sizeof(palette) / sizeof(palette[0]))]);
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
    std::string previousSelectedSnapshotPath;
    if (m_selectedSnapshot >= 0 && m_selectedSnapshot < static_cast<int>(m_snapshotRows.size())) {
        previousSelectedSnapshotPath = m_snapshotRows[static_cast<size_t>(m_selectedSnapshot)].path;
    }

    m_takeRows.clear();
    m_snapshotRows.clear();
    m_manifestError.clear();

    if (m_takesProvider) {
        TakeManager::Manifest manifest = m_takesProvider();
        if (manifest.ok) {
            m_multipleBranches = manifest.branches().size() > 1;
            // Depth = length of the parent chain; guards against cycles/orphans.
            std::unordered_map<std::string, std::string> parentOf;
            for (const auto& take : manifest.takes) parentOf[take.id] = take.parentId;
            for (const auto& take : manifest.takes) {
                int depth = 0;
                std::string cursor = take.parentId;
                while (!cursor.empty() && depth < 8) {
                    auto it = parentOf.find(cursor);
                    if (it == parentOf.end()) break;
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
        if (snapshots.size() > MAX_SNAPSHOT_ROWS) snapshots.resize(MAX_SNAPSHOT_ROWS);
        m_snapshotRows = std::move(snapshots);
    }
    m_steps = m_stepsProvider ? m_stepsProvider() : std::vector<StepEntry>{};

    // Selection follows the version's identity, not its row index.
    m_selectedTake = -1;
    if (!previousSelectedId.empty()) {
        for (int i = 0; i < static_cast<int>(m_takeRows.size()); ++i)
            if (m_takeRows[static_cast<size_t>(i)].take.id == previousSelectedId) { m_selectedTake = i; break; }
    }
    m_selectedSnapshot = -1;
    if (!previousSelectedSnapshotPath.empty()) {
        for (int i = 0; i < static_cast<int>(m_snapshotRows.size()); ++i)
            if (m_snapshotRows[static_cast<size_t>(i)].path == previousSelectedSnapshotPath) { m_selectedSnapshot = i; break; }
    }
    if (m_renameActive) {
        bool stillPresent = false;
        for (const auto& row : m_takeRows) if (row.take.id == m_renameTakeId) { stillPresent = true; break; }
        if (!stillPresent) cancelRename();
    }
}

int TakesPanel::getDockVersion() const {
    if (m_selectedTake >= 0 && m_selectedTake < static_cast<int>(m_takeRows.size())) return m_selectedTake;
    for (int i = 0; i < static_cast<int>(m_takeRows.size()); ++i)
        if (m_takeRows[static_cast<size_t>(i)].take.active) return i;
    return m_takeRows.empty() ? -1 : 0;
}

TakesPanel::Layout TakesPanel::computeLayout() const {
    Layout layout;
    const auto bounds = getBounds();
    const float x = bounds.x, w = bounds.width;
    const float top = bounds.y + getTitleBarHeight();
    float bottom = bounds.bottom();

    if (!m_statusText.empty()) {
        bottom -= STATUS_HEIGHT;
        layout.statusLine = NUIRect(x, bottom, w, STATUS_HEIGHT);
    }

    layout.bar = NUIRect(x, top, w, BAR_HEIGHT);
    layout.saveButton = NUIRect(x + w - kPad - 112.0f, top + 12.0f, 112.0f, 28.0f);
    layout.branchButton = NUIRect(layout.saveButton.x - 8.0f - 128.0f, top + 12.0f, 128.0f, 28.0f);

    const float dockW = std::clamp(w * 0.30f, 340.0f, 540.0f);
    const float bodyTop = top + BAR_HEIGHT;
    layout.dock = NUIRect(x + w - dockW, bodyTop, dockW, std::max(0.0f, bottom - bodyTop));
    layout.legend = NUIRect(x, bottom - LEGEND_HEIGHT, w - dockW, LEGEND_HEIGHT);
    layout.timeline = NUIRect(x, bodyTop, w - dockW, std::max(0.0f, layout.legend.y - bodyTop));

    std::vector<TakeManager::TakeEntry> versions;
    versions.reserve(m_takeRows.size());
    for (const auto& r : m_takeRows) versions.push_back(r.take);
    layout.nodes = VersionsTimeline::layout(versions, layout.timeline);

    // The dock: the version's header, then its steps (the open version) or its notes and log, then the actions.
    const int shown = getDockVersion();
    const float dx = layout.dock.x + kPad, dw = layout.dock.width - kPad * 2.0f;
    float buttonsBottom = layout.dock.bottom() - 20.0f;
    if (!m_snapshotRows.empty()) {
        const float h = static_cast<float>(m_snapshotRows.size()) * SNAPSHOT_ROW_HEIGHT;
        layout.snapshotList = NUIRect(layout.dock.x, layout.dock.bottom() - h - 8.0f, layout.dock.width, h);
        layout.snapshotHeader = NUIRect(layout.dock.x, layout.snapshotList.y - 24.0f, layout.dock.width, 22.0f);
        buttonsBottom = layout.snapshotHeader.y - 10.0f;
    }
    std::vector<std::pair<std::string, std::pair<std::string, int>>> defs; // id, label, flags (1 primary, 2 danger)
    if (m_selectedSnapshot >= 0) {
        defs = {{"restore", {"Recover this snapshot", 1}}};
    } else if (shown >= 0) {
        if (m_takeRows[static_cast<size_t>(shown)].take.active) {
            defs = {{"save", {"Save version", 1}}, {"newbranch", {"Save to new branch", 0}}};
        } else {
            defs = {{"open", {"Open this version", 1}}, {"branch", {"Branch from here", 0}}, {"rename", {"Rename", 0}}, {"delete", {"Delete", 2}}};
        }
    }
    // Buttons wrap onto more rows when the dock is narrow; the last row sits at the bottom.
    const float charW = 7.0f;
    std::vector<DockButton> btns;
    std::vector<int> rowOf;
    int row = 0;
    float bx = dx;
    for (const auto& d : defs) {
        const float bw = d.second.first.size() * charW + 28.0f;
        if (bx + bw > dx + dw && bx > dx) { bx = dx; ++row; }
        btns.push_back({d.first, d.second.first, NUIRect(bx, 0.0f, bw, 30.0f), (d.second.second & 1) != 0, (d.second.second & 2) != 0});
        rowOf.push_back(row);
        bx += bw + 8.0f;
    }
    const float by = buttonsBottom - 30.0f - static_cast<float>(row) * 36.0f;
    for (size_t i = 0; i < btns.size(); ++i) btns[i].rect.y = by + static_cast<float>(rowOf[i]) * 36.0f;
    layout.buttons = std::move(btns);
    const float listTop = layout.dock.y + 100.0f;
    layout.stepsList = NUIRect(dx, listTop, dw, std::max(0.0f, by - 16.0f - listTop - 22.0f));
    return layout;
}

bool TakesPanel::getTimelineNodeCenter(int row, NUIPoint& out) const {
    const Layout layout = computeLayout();
    if (row < 0 || row >= static_cast<int>(layout.nodes.nodes.size())) return false;
    out = {layout.nodes.nodes[static_cast<size_t>(row)].x, layout.nodes.nodes[static_cast<size_t>(row)].y};
    return true;
}

int TakesPanel::snapshotRowAt(const NUIPoint& point, const Layout& layout) const {
    if (m_snapshotRows.empty() || !layout.snapshotList.contains(point)) return -1;
    const int index = static_cast<int>((point.y - layout.snapshotList.y) / SNAPSHOT_ROW_HEIGHT);
    return (index >= 0 && index < static_cast<int>(m_snapshotRows.size())) ? index : -1;
}

int TakesPanel::stepRowAt(const NUIPoint& point, const Layout& layout) const {
    if (!layout.stepsList.contains(point)) return -1;
    const int index = static_cast<int>((point.y - layout.stepsList.y + m_stepScroll) / STEP_ROW_HEIGHT);
    return (index >= 0 && index < static_cast<int>(m_steps.size())) ? index : -1;
}

void TakesPanel::selectTake(int index) {
    if (index < -1 || index >= static_cast<int>(m_takeRows.size())) index = -1;
    if (m_renameActive && index != m_selectedTake) cancelRename();
    m_selectedTake = index;
    if (index >= 0) m_selectedSnapshot = -1;
    m_stepScroll = 0.0f;
    setDirty(true);
}

void TakesPanel::selectSnapshot(int index) {
    if (index < -1 || index >= static_cast<int>(m_snapshotRows.size())) index = -1;
    if (m_renameActive) cancelRename();
    m_selectedSnapshot = index;
    if (index >= 0) m_selectedTake = -1;
    setDirty(true);
}

void TakesPanel::setStatus(const std::string& text) {
    m_statusText = text;
    setDirty(true);
}

bool TakesPanel::invokeAction(const std::function<bool()>& action, const char* failureMessage) {
    if (!action) return false;
    setStatus("");
    const bool ok = action();
    if (!ok) setStatus(failureMessage);
    refreshTakes();
    return ok;
}

bool TakesPanel::requestCreateTake() {
    return invokeAction([this]() { return m_onCreateTake && m_onCreateTake(); }, "Could not save the version");
}

bool TakesPanel::requestSaveToNewBranch() {
    return invokeAction([this]() { return m_onSaveToNewBranch && m_onSaveToNewBranch(); }, "Could not save to a new branch");
}

bool TakesPanel::requestSaveActiveTake() {
    return invokeAction([this]() { return m_onSaveActiveTake && m_onSaveActiveTake(); }, "Could not save the open version");
}

bool TakesPanel::requestOpenSelected() {
    const int row = getDockVersion();
    if (row < 0) return false;
    const std::string takeId = m_takeRows[static_cast<size_t>(row)].take.id;
    return invokeAction([this, takeId]() { return m_onOpenTake && m_onOpenTake(takeId); }, "Could not open the version");
}

bool TakesPanel::requestDuplicateSelected() {
    const int row = getDockVersion();
    if (row < 0) return false;
    const std::string takeId = m_takeRows[static_cast<size_t>(row)].take.id;
    return invokeAction([this, takeId]() { return m_onDuplicateTake && m_onDuplicateTake(takeId); }, "Could not duplicate the version");
}

bool TakesPanel::requestBranchSelected() {
    const int row = getDockVersion();
    if (row < 0) return false;
    const std::string takeId = m_takeRows[static_cast<size_t>(row)].take.id;
    return invokeAction([this, takeId]() { return m_onBranchTake && m_onBranchTake(takeId); }, "Could not branch from the version");
}

bool TakesPanel::requestDeleteSelected() {
    const int row = getDockVersion();
    if (row < 0) return false;
    const auto& take = m_takeRows[static_cast<size_t>(row)].take;
    if (take.active) {
        setStatus("Open another version before deleting this one");
        return false;
    }
    const std::string takeId = take.id;
    return invokeAction([this, takeId]() { return m_onDeleteTake && m_onDeleteTake(takeId); }, "Could not delete the version");
}

bool TakesPanel::requestRestoreSelectedSnapshot() {
    if (m_selectedSnapshot < 0) return false;
    const std::string path = m_snapshotRows[static_cast<size_t>(m_selectedSnapshot)].path;
    return invokeAction([this, path]() { return m_onRestoreSnapshot && m_onRestoreSnapshot(path); }, "Could not restore snapshot");
}

void TakesPanel::beginRenameSelected() {
    const int row = getDockVersion();
    if (row < 0) return;
    const auto& take = m_takeRows[static_cast<size_t>(row)].take;
    m_selectedTake = row;
    m_renameActive = true;
    m_renameTakeId = take.id;
    m_renameBuffer = take.name;
    // Printable characters are delivered through the focused-component text path, so claim focus for the edit.
    setFocused(true);
    setStatus("");
    setDirty(true);
}

bool TakesPanel::commitRename() {
    if (!m_renameActive) return false;
    const std::string takeId = m_renameTakeId;
    const std::string name = m_renameBuffer;
    m_renameActive = false;
    m_renameTakeId.clear();
    m_renameBuffer.clear();
    setFocused(false);
    if (name.empty()) {
        setStatus("A version needs a name");
        return false;
    }
    return invokeAction([this, takeId, name]() { return m_onRenameTake && m_onRenameTake(takeId, name); }, "Could not rename the version");
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
    if (!m_renameActive || !isVisible() || !event.pressed) return false;
    if (event.keyCode == NUIKeyCode::Enter) { commitRename(); return true; }
    if (event.keyCode == NUIKeyCode::Escape) { cancelRename(); return true; }
    if (event.keyCode == NUIKeyCode::Backspace) {
        if (!m_renameBuffer.empty()) { m_renameBuffer.pop_back(); setDirty(true); }
        return true;
    }
    if (event.character >= 32 && event.character != 127) {
        if (m_renameBuffer.size() < 128) { m_renameBuffer.push_back(event.character); setDirty(true); }
        return true;
    }
    return true; // while editing, swallow other presses so global shortcuts don't fire
}

bool TakesPanel::runDockAction(const std::string& id) {
    if (id == "save") return requestSaveActiveTake();
    if (id == "newbranch") return requestSaveToNewBranch();
    if (id == "open") return requestOpenSelected();
    if (id == "branch") return requestBranchSelected();
    if (id == "delete") return requestDeleteSelected();
    if (id == "restore") return requestRestoreSelectedSnapshot();
    if (id == "rename") { beginRenameSelected(); return true; }
    return false;
}

void TakesPanel::onRender(NUIRenderer& renderer) {
    auto& theme = NUIThemeManager::getInstance();
    WindowPanel::onRender(renderer);

    const auto bounds = getBounds();
    if (bounds.width <= 0 || bounds.height <= 0 || isMinimized()) return;

    const Layout layout = computeLayout();
    const NUIColor rule = theme.getColor("borderSubtle");
    const NUIColor text = theme.getColor("textPrimary");
    const NUIColor muted = theme.getColor("textSecondary");
    const NUIColor bed = theme.getColor("backgroundPrimary");
    const uint64_t now = nowMs();
    const int shown = getDockVersion();

    renderer.fillRect(NUIRect(bounds.x, layout.bar.y, bounds.width, bounds.bottom() - layout.bar.y), theme.getColor("backgroundSecondary"));
    renderer.fillRect(layout.timeline, bed);

    // --- Top bar: what this is, the project, the open branch; the two verbs you reach for most.
    int openRow = -1;
    for (int i = 0; i < static_cast<int>(m_takeRows.size()); ++i) if (m_takeRows[static_cast<size_t>(i)].take.active) openRow = i;
    renderer.drawText("HISTORY  \xc2\xb7  AESTRA VERSIONS", {layout.bar.x + kPad, layout.bar.y + 20.0f}, 10.0f, muted.withAlpha(0.85f));
    float cx = layout.bar.x + kPad + renderer.measureText("HISTORY  \xc2\xb7  AESTRA VERSIONS", 10.0f).width + 20.0f;
    const std::string project = m_projectNameProvider ? m_projectNameProvider() : std::string();
    if (!project.empty()) {
        renderer.drawText(project, {cx, layout.bar.y + 16.0f}, 14.0f, text);
        cx += renderer.measureText(project, 14.0f).width + 14.0f;
    }
    if (openRow >= 0) {
        const auto& open = m_takeRows[static_cast<size_t>(openRow)].take;
        const std::string branch = open.branch.empty() ? "Main" : open.branch;
        const float bw = renderer.measureText(branch, 11.0f).width + 34.0f;
        const NUIRect chip{cx, layout.bar.y + 14.0f, bw, 24.0f};
        renderer.fillRect(chip, theme.getColor("surfaceTertiary"));
        const int lane = static_cast<int>(std::find(layout.nodes.lanes.begin(), layout.nodes.lanes.end(), branch) - layout.nodes.lanes.begin());
        renderer.fillCircle({chip.x + 12.0f, chip.y + 12.0f}, 3.5f, laneColor(lane));
        renderer.drawText(branch, {chip.x + 24.0f, chip.y + 7.0f}, 11.0f, text);
    }
    const auto drawButton = [&](const NUIRect& r, const std::string& label, bool primary, bool danger, bool hot) {
        NUIColor fill = primary ? theme.getColor("accentPrimary") : theme.getColor("surfaceTertiary");
        if (hot) fill = fill.lightened(0.08f);
        renderer.fillRect(r, fill);
        const float tw = renderer.measureText(label, 12.0f).width;
        renderer.drawText(label, {r.x + (r.width - tw) * 0.5f, std::round(renderer.calculateTextY(r, 12.0f))}, 12.0f,
                          primary ? NUIColor::white() : danger ? theme.getColor("error") : text);
    };
    drawButton(layout.branchButton, "Branch from here", false, false, m_hoveredButton == "bar-branch");
    drawButton(layout.saveButton, "Save version", true, false, m_hoveredButton == "bar-save");
    renderer.fillRect({bounds.x, layout.bar.bottom() - 1.0f, bounds.width, 1.0f}, rule);

    // --- Timeline: lanes, links, nodes.
    renderer.setClipRect(layout.timeline);
    for (size_t i = 0; i < layout.nodes.lanes.size(); ++i) {
        const float y = layout.nodes.laneY[i];
        renderer.fillCircle({layout.timeline.x + 30.0f, y}, 3.5f, laneColor(static_cast<int>(i)));
        renderer.drawText(layout.nodes.lanes[i], {layout.timeline.x + 42.0f, y - 6.0f}, 11.0f, text.withAlpha(0.9f));
    }
    // Each lane runs from its first version to its last, in the lane's colour; forks join it to its parent.
    for (size_t l = 0; l < layout.nodes.lanes.size(); ++l) {
        float lo = 1e9f, hi = -1e9f;
        for (const auto& n : layout.nodes.nodes) if (n.lane == static_cast<int>(l)) { lo = std::min(lo, n.x); hi = std::max(hi, n.x); }
        if (hi > lo) renderer.drawLine({lo, layout.nodes.laneY[l]}, {hi, layout.nodes.laneY[l]}, 2.0f, laneColor(static_cast<int>(l)));
    }
    for (const auto& link : layout.nodes.links) {
        const auto& from = layout.nodes.nodes[static_cast<size_t>(link.from)];
        const auto& to = layout.nodes.nodes[static_cast<size_t>(link.to)];
        if (from.lane != to.lane) forkCurve(renderer, {from.x, from.y}, {to.x, to.y}, laneColor(to.lane));
    }
    const int unsaved = m_unsavedProvider ? m_unsavedProvider() : 0;
    for (size_t i = 0; i < layout.nodes.nodes.size(); ++i) {
        const auto& n = layout.nodes.nodes[i];
        const auto& v = m_takeRows[i].take;
        const NUIColor c = laneColor(n.lane);
        const bool isShown = static_cast<int>(i) == shown;
        if (n.autosave) {
            renderer.fillCircle({n.x, n.y}, 3.5f, theme.getColor("textDisabled").withAlpha(0.9f));
            continue;
        }
        if (n.open) {
            renderer.fillCircle({n.x, n.y}, 18.0f, c.withAlpha(0.18f));
            renderer.fillCircle({n.x, n.y}, 12.0f, c);
        } else {
            renderer.fillCircle({n.x, n.y}, 8.0f, bed);
            renderer.strokeCircle({n.x, n.y}, 8.0f, isShown ? 3.0f : 2.0f, c);
        }
        // The name over the lane at the top, under it on the lanes below, with the date beneath the name.
        const float labelY = n.lane == 0 ? n.y - 44.0f : n.y + 20.0f;
        const std::string name = v.name.empty() ? v.id : v.name;
        const float nw = renderer.measureText(name, 12.0f).width;
        renderer.drawText(name, {n.x - nw * 0.5f, labelY}, 12.0f, text);
        const std::string when = VersionsTimeline::timeLabel(v.updatedAtMs ? v.updatedAtMs : v.createdAtMs, now);
        const float ww = renderer.measureText(when, 10.0f).width;
        renderer.drawText(when, {n.x - ww * 0.5f, labelY + 16.0f}, 10.0f, muted.withAlpha(0.75f));
        if (n.open) {
            const std::string here = "YOU ARE HERE";
            const float hw = renderer.measureText(here, 9.5f).width;
            renderer.drawText(here, {n.x - hw * 0.5f, n.lane == 0 ? n.y + 24.0f : labelY + 32.0f}, 9.5f, c.lightened(0.15f));
            if (unsaved > 0) { // the dashed tail: what has been done since the version was saved
                const NUIPoint tailEnd{std::min(layout.timeline.right() - 24.0f, n.x + 150.0f), n.y};
                dashedLine(renderer, {n.x + 16.0f, n.y}, tailEnd, muted.withAlpha(0.6f));
                const std::string label = std::to_string(unsaved) + (unsaved == 1 ? " unsaved change" : " unsaved changes");
                const float lw = renderer.measureText(label, 11.0f).width;
                const NUIRect chip{std::min(tailEnd.x + 8.0f, layout.timeline.right() - lw - 40.0f), n.y - 14.0f, lw + 34.0f, 28.0f};
                renderer.fillRect(chip, theme.getColor("surfaceTertiary"));
                renderer.fillCircle({chip.x + 14.0f, chip.y + 14.0f}, 3.5f, theme.getColor("warning"));
                renderer.drawText(label, {chip.x + 24.0f, chip.y + 8.0f}, 11.0f, text);
            }
        }
    }
    {   // The time axis: a rule near the bottom with the first version of each day under it.
        const float axisY = layout.timeline.bottom() - 34.0f;
        renderer.fillRect({layout.timeline.x + 24.0f, axisY, layout.timeline.width - 48.0f, 1.0f}, rule);
        std::string lastDay;
        for (size_t i = 0; i < layout.nodes.nodes.size(); ++i) {
            const auto& v = m_takeRows[i].take;
            const std::string when = VersionsTimeline::timeLabel(v.createdAtMs, now);
            const std::string day = when.substr(0, when.find(" \xc2\xb7"));
            if (day.empty() || day == lastDay || layout.nodes.nodes[i].autosave) continue;
            lastDay = day;
            std::string up;
            for (char c : day) up.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
            renderer.drawText(up, {layout.nodes.nodes[i].x - 12.0f, axisY + 10.0f}, 9.5f, muted.withAlpha(0.7f));
        }
    }
    if (m_takeRows.empty()) {
        const std::string hint = !m_manifestError.empty() ? ("Versions unavailable: " + m_manifestError)
                                                          : "No versions yet. Save one to start the timeline.";
        renderer.drawTextCentered(hint, layout.timeline, 12.0f, muted.withAlpha(0.7f));
    }
    renderer.clearClipRect();

    // --- Legend.
    renderer.fillRect({layout.legend.x, layout.legend.y, layout.legend.width, 1.0f}, rule);
    float lx = layout.legend.x + kPad;
    const float ly = layout.legend.y + 12.0f;
    renderer.strokeCircle({lx + 6.0f, ly + 6.0f}, 5.5f, 2.0f, muted);
    renderer.drawText("Version you saved", {lx + 20.0f, ly}, 11.0f, muted);
    lx += 150.0f;
    renderer.fillCircle({lx + 4.0f, ly + 6.0f}, 3.5f, theme.getColor("textDisabled"));
    renderer.drawText("Autosave", {lx + 16.0f, ly}, 11.0f, muted);
    lx += 100.0f;
    dashedLine(renderer, {lx, ly + 6.0f}, {lx + 26.0f, ly + 6.0f}, muted.withAlpha(0.7f), 4.0f, 3.0f);
    renderer.drawText("Unsaved changes", {lx + 34.0f, ly}, 11.0f, muted);

    // --- Dock.
    renderer.fillRect({layout.dock.x, layout.dock.y, 1.0f, layout.dock.height}, rule);
    const float dx = layout.dock.x + kPad, dw = layout.dock.width - kPad * 2.0f;
    if (m_selectedSnapshot >= 0 && m_selectedSnapshot < static_cast<int>(m_snapshotRows.size())) {
        renderer.fillCircle({dx + 5.0f, layout.dock.y + 36.0f}, 5.0f, theme.getColor("warning"));
        renderer.drawText("Autosave", {dx + 20.0f, layout.dock.y + 27.0f}, 17.0f, text);
        renderer.drawText(m_snapshotRows[static_cast<size_t>(m_selectedSnapshot)].label, {dx, layout.dock.y + 58.0f}, 11.5f, muted);
        renderer.drawText("Work the app kept on its own. Recovering it opens its state in place of the current one.",
                          {dx, layout.dock.y + 86.0f}, 11.5f, muted.withAlpha(0.85f));
    } else if (shown >= 0) {
        const auto& v = m_takeRows[static_cast<size_t>(shown)].take;
        const int lane = static_cast<int>(std::find(layout.nodes.lanes.begin(), layout.nodes.lanes.end(), v.branch.empty() ? "Main" : v.branch) - layout.nodes.lanes.begin());
        const NUIColor c = laneColor(lane);
        const float titleY = layout.dock.y + 26.0f;
        if (v.active) renderer.fillCircle({dx + 6.0f, titleY + 11.0f}, 6.0f, c);
        else renderer.strokeCircle({dx + 6.0f, titleY + 11.0f}, 6.0f, 2.0f, c);
        const bool renaming = m_renameActive && m_renameTakeId == v.id;
        const std::string title = renaming ? m_renameBuffer + "|" : (v.name.empty() ? v.id : v.name);
        renderer.drawText(title, {dx + 22.0f, titleY}, 18.0f, text);
        const float tw = renderer.measureText(title, 18.0f).width;
        renderer.drawText(std::string(v.branch.empty() ? "Main" : v.branch) + (v.active ? "  \xc2\xb7  open" : ""), {dx + 30.0f + tw, titleY + 4.0f}, 11.0f, muted);
        const std::string when = VersionsTimeline::timeLabel(v.updatedAtMs ? v.updatedAtMs : v.createdAtMs, now);
        const float ww = renderer.measureText(when, 10.5f).width;
        renderer.drawText(when, {layout.dock.right() - kPad - ww, titleY + 5.0f}, 10.5f, muted.withAlpha(0.8f));

        if (v.active) {
            renderer.drawText("STEPS IN THIS VERSION  \xc2\xb7  CLICK TO GO BACK", {dx, layout.dock.y + 74.0f}, 9.5f, muted.withAlpha(0.8f));
            renderer.setClipRect(layout.stepsList);
            renderer.fillRect(layout.stepsList, bed);
            for (int i = 0; i < static_cast<int>(m_steps.size()); ++i) {
                const float ry = layout.stepsList.y + static_cast<float>(i) * STEP_ROW_HEIGHT - m_stepScroll;
                if (ry + STEP_ROW_HEIGHT < layout.stepsList.y || ry > layout.stepsList.bottom()) continue;
                const NUIRect row{layout.stepsList.x, ry, layout.stepsList.width, STEP_ROW_HEIGHT};
                const bool current = !m_steps[static_cast<size_t>(i)].undone && (i == 0 || m_steps[static_cast<size_t>(i - 1)].undone);
                if (i == m_hoveredStep) renderer.fillRect(row, theme.getColor("surfaceTertiary"));
                if (current) renderer.fillRect(row, c.withAlpha(0.18f));
                const NUIColor dot = current ? c : theme.getColor("textDisabled");
                renderer.fillCircle({row.x + 14.0f, row.y + row.height * 0.5f}, 3.5f, dot);
                renderer.drawText(m_steps[static_cast<size_t>(i)].label, {row.x + 28.0f, std::round(renderer.calculateTextY(row, 12.0f))}, 12.0f,
                                  m_steps[static_cast<size_t>(i)].undone ? muted.withAlpha(0.5f) : text);
                renderer.fillRect({row.x, row.bottom() - 1.0f, row.width, 1.0f}, rule.withAlpha(0.6f));
            }
            renderer.strokeRect(layout.stepsList, 1.0f, rule);
            if (m_steps.empty()) renderer.drawTextCentered("Nothing done in this version yet", layout.stepsList, 12.0f, muted.withAlpha(0.6f));
            renderer.clearClipRect();
            renderer.fillCircle({dx + 4.0f, layout.stepsList.bottom() + 16.0f}, 3.5f, theme.getColor("accentCyan"));
            renderer.drawText("Recording a take is a step too", {dx + 18.0f, layout.stepsList.bottom() + 10.0f}, 11.0f, muted.withAlpha(0.8f));
        } else {
            // An older version: its notes and its read-only log. Going back into it means opening it.
            float y = layout.dock.y + 74.0f;
            if (!v.notes.empty()) {
                renderer.drawText("NOTES ON THIS VERSION", {dx, y}, 9.5f, muted.withAlpha(0.8f));
                y += 20.0f;
                for (const auto& n : v.notes) { renderer.drawText(n.text, {dx, y}, 12.0f, text); y += 22.0f; }
                y += 10.0f;
            }
            renderer.drawText("STEPS IN THIS VERSION  \xc2\xb7  READ-ONLY LOG", {dx, y}, 9.5f, muted.withAlpha(0.8f));
            y += 22.0f;
            std::string line;
            for (const auto& s : v.steps) line += (line.empty() ? "" : "  \xc2\xb7  ") + s.label;
            if (line.empty()) line = "This version was saved before steps were recorded.";
            // Wrap the log at the dock width on the separator or a space.
            float lineW = 0.0f; std::string cur;
            size_t pos = 0;
            while (pos < line.size() && y < layout.buttons.front().rect.y - 40.0f) {
                size_t end = line.find(' ', pos);
                if (end == std::string::npos) end = line.size();
                const std::string word = line.substr(pos, end - pos) + " ";
                const float ww2 = renderer.measureText(word, 12.0f).width;
                if (lineW + ww2 > dw && !cur.empty()) { renderer.drawText(cur, {dx, y}, 12.0f, muted); y += 20.0f; cur.clear(); lineW = 0.0f; }
                cur += word; lineW += ww2; pos = end + 1;
            }
            if (!cur.empty()) { renderer.drawText(cur, {dx, y}, 12.0f, muted); y += 28.0f; }
            std::string forks;
            for (const auto& c : m_takeRows) // branches that started from this version
                if (c.take.parentId == v.id && c.take.branch != v.branch) forks += (forks.empty() ? "" : ", ") + (c.take.branch.empty() ? c.take.name : c.take.branch);
            if (!forks.empty()) renderer.drawText("Branched from here: " + forks + ". Open this version to work inside it.", {dx, y}, 11.0f, muted.withAlpha(0.75f));
        }
    }
    for (const auto& b : layout.buttons) drawButton(b.rect, b.label, b.primary, b.danger, m_hoveredButton == b.id);

    // Recovery snapshots (the app's automatic saves), when there are any.
    if (!m_snapshotRows.empty()) {
        renderer.fillRect({layout.dock.x + 1.0f, layout.snapshotHeader.y - 1.0f, layout.dock.width - 1.0f, 1.0f}, rule);
        renderer.drawText("RECOVERY SNAPSHOTS", {dx, layout.snapshotHeader.y + 6.0f}, 9.5f, muted.withAlpha(0.8f));
        for (int i = 0; i < static_cast<int>(m_snapshotRows.size()); ++i) {
            const NUIRect row{layout.dock.x + 1.0f, layout.snapshotList.y + static_cast<float>(i) * SNAPSHOT_ROW_HEIGHT, layout.dock.width - 1.0f, SNAPSHOT_ROW_HEIGHT};
            if (i == m_selectedSnapshot) renderer.fillRect(row, theme.getColor("surfaceRaised"));
            else if (i == m_hoveredSnapshot) renderer.fillRect(row, theme.getColor("surfaceTertiary"));
            renderer.fillCircle({dx + 4.0f, row.y + row.height * 0.5f}, 3.5f, theme.getColor("warning"));
            renderer.drawText(m_snapshotRows[static_cast<size_t>(i)].label, {dx + 18.0f, std::round(renderer.calculateTextY(row, 11.0f))}, 11.0f, muted);
        }
    }

    if (!m_statusText.empty()) {
        renderer.fillRect(layout.statusLine, theme.getColor("warning").withAlpha(0.14f));
        renderer.drawText(m_statusText, {bounds.x + 16.0f, layout.statusLine.y + 3.0f}, 10.0f, theme.getColor("warning"));
    }
}

bool TakesPanel::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (isMinimized()) return WindowPanel::onMouseEvent(event);

    const Layout layout = computeLayout();

    if (event.type == NUIMouseEventType::Move) {
        m_hoveredStep = stepRowAt(event.position, layout);
        m_hoveredSnapshot = snapshotRowAt(event.position, layout);
        m_hoveredButton.clear();
        if (layout.branchButton.contains(event.position)) m_hoveredButton = "bar-branch";
        if (layout.saveButton.contains(event.position)) m_hoveredButton = "bar-save";
        for (const auto& b : layout.buttons) if (b.rect.contains(event.position)) m_hoveredButton = b.id;
    }

    if (event.type == NUIMouseEventType::Scroll && layout.stepsList.contains(event.position)) {
        const float contentH = static_cast<float>(m_steps.size()) * STEP_ROW_HEIGHT;
        m_stepScroll = std::clamp(m_stepScroll - event.wheelDelta * 30.0f, 0.0f, std::max(0.0f, contentH - layout.stepsList.height));
        setDirty(true);
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (layout.saveButton.contains(event.position)) { requestCreateTake(); return true; }
        if (layout.branchButton.contains(event.position)) {
            // Branch from the version in the dock (selected, else the open one).
            requestBranchSelected();
            return true;
        }
        for (const auto& b : layout.buttons)
            if (b.rect.contains(event.position)) { runDockAction(b.id); return true; }

        const int node = VersionsTimeline::nodeAt(layout.nodes, event.position);
        if (node >= 0) {
            selectTake(node);
            if (event.type == NUIMouseEventType::DoubleClick && !m_takeRows[static_cast<size_t>(node)].take.active) requestOpenSelected();
            return true;
        }
        const int snap = snapshotRowAt(event.position, layout);
        if (snap >= 0) { selectSnapshot(snap); return true; }
        const int step = stepRowAt(event.position, layout);
        if (step >= 0 && m_onStepActivated) {
            m_onStepActivated(m_steps[static_cast<size_t>(step)]);
            refreshTakes();
            return true;
        }
        if (layout.timeline.contains(event.position)) { selectTake(-1); return true; }
    }
    return WindowPanel::onMouseEvent(event);
}

} // namespace Audio
} // namespace Aestra
