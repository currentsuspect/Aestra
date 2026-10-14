// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AuditionFilesPage.h"

#include "../AestraUI/Core/NUIDragDrop.h"
#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>

using namespace AestraUI;

namespace Aestra {

AuditionFilesPage::AuditionFilesPage() {
    const char* home = std::getenv("HOME");
    if (!home) home = std::getenv("USERPROFILE");
    m_home = home ? home : "";
}

void AuditionFilesPage::setProjectDir(const std::string& dir) {
    if (dir == m_projectDir && !m_places.empty()) return;
    m_projectDir = dir;
    m_places = AuditionFiles::places(dir, m_home);
    if (m_dir.empty()) {  // the first look: the music, if there is any
        const auto music = std::find_if(m_places.begin(), m_places.end(), [](const auto& p) { return p.name == "Music"; });
        setDirectory(music != m_places.end() ? music->path : (!m_places.empty() ? m_places.front().path : m_home));
    }
    setDirty(true);
}

void AuditionFilesPage::setDirectory(const std::string& dir) {
    if (dir.empty()) return;
    m_dir = dir;
    m_entries = AuditionFiles::list(dir);
    m_scroll = 0.0f;
    m_hover = m_selected = m_pressed = -1;
    setDirty(true);
}

AuditionFilesPage::Layout AuditionFilesPage::layout() const {
    Layout l;
    const NUIRect b = getBounds();
    l.header = {b.x + 24.0f, b.y + 24.0f, b.width - 48.0f, 32.0f};
    l.up = {b.right() - 24.0f - 56.0f, b.y + 56.0f, 56.0f, 24.0f};
    l.path = {b.x + 24.0f, b.y + 56.0f, b.width - 48.0f - 64.0f, 24.0f};
    // Places wrap in rows of chips under the path.
    float x = b.x + 24.0f, y = b.y + 92.0f;
    for (const auto& p : m_places) {
        const float w = static_cast<float>(p.name.size()) * 7.0f + 24.0f;
        if (x + w > b.right() - 24.0f && x > b.x + 24.0f) { x = b.x + 24.0f; y += 32.0f; }
        l.places.push_back({x, y, w, 24.0f});
        x += w + 8.0f;
    }
    l.placesArea = {b.x + 24.0f, b.y + 92.0f, b.width - 48.0f, y + 24.0f - (b.y + 92.0f)};
    const float top = l.placesArea.bottom() + 16.0f;
    l.list = {b.x, top, b.width, std::max(0.0f, b.bottom() - top)};
    return l;
}

int AuditionFilesPage::rowAt(const NUIPoint& p, const Layout& l) const {
    if (!l.list.contains(p)) return -1;
    const int row = static_cast<int>((p.y - l.list.y + m_scroll) / rowHeight());
    return row >= 0 && row < static_cast<int>(m_entries.size()) ? row : -1;
}

NUIRect AuditionFilesPage::playButton(const NUIRect& row) const { return {row.right() - 24.0f - 130.0f, row.y + 6.0f, 52.0f, 24.0f}; }
NUIRect AuditionFilesPage::refButton(const NUIRect& row) const { return {row.right() - 24.0f - 70.0f, row.y + 6.0f, 70.0f, 24.0f}; }

bool AuditionFilesPage::getRowCenter(size_t row, NUIPoint& out) const {
    const Layout l = layout();
    if (row >= m_entries.size()) return false;
    out = {l.list.x + 120.0f, l.list.y + (static_cast<float>(row) + 0.5f) * rowHeight() - m_scroll};
    return true;
}

void AuditionFilesPage::clampScroll() {
    const Layout l = layout();
    m_scroll = std::clamp(m_scroll, 0.0f, std::max(0.0f, static_cast<float>(m_entries.size()) * rowHeight() - l.list.height));
}

void AuditionFilesPage::onRender(NUIRenderer& r) {
    if (!isVisible()) return;
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary"), muted = theme.getColor("textSecondary"), rule = theme.getColor("borderSubtle");
    const NUIRect b = getBounds();
    const Layout l = layout();
    r.fillRect(b, theme.getColor("backgroundPrimary"));
    r.fillRect({b.right() - 1.0f, b.y, 1.0f, b.height}, rule);

    r.drawText("Files", {l.header.x, l.header.y}, 20.0f, text);
    // The path, from the right so the folder you are in is always the part you can read.
    std::string path = m_dir;
    float pw = r.measureText(path, 11.0f).width;
    while (pw > l.path.width && path.size() > 4) { path.erase(0, 4); pw = r.measureText("..." + path, 11.0f).width; if (pw <= l.path.width) { path = "..." + path; break; } }
    r.drawText(path, {l.path.x, std::round(r.calculateTextY(l.path, 11.0f))}, 11.0f, muted);
    const bool upHot = m_hoverControl == "up";
    r.fillRect(l.up, upHot ? theme.getColor("surfaceRaised") : theme.getColor("surfaceTertiary"));
    const float uw = r.measureText("Up", 11.0f).width;
    r.drawText("Up", {l.up.x + (l.up.width - uw) * 0.5f, std::round(r.calculateTextY(l.up, 11.0f))}, 11.0f, text);

    for (size_t i = 0; i < l.places.size(); ++i) {
        const bool here = m_places[i].path == m_dir;
        const bool hot = m_hoverControl == "place" + std::to_string(i);
        r.fillRect(l.places[i], here ? theme.getColor("accentPrimary").withAlpha(0.28f) : hot ? theme.getColor("surfaceRaised") : theme.getColor("surfaceTertiary"));
        const float tw = r.measureText(m_places[i].name, 11.0f).width;
        r.drawText(m_places[i].name, {l.places[i].x + (l.places[i].width - tw) * 0.5f, std::round(r.calculateTextY(l.places[i], 11.0f))}, 11.0f, here ? text : muted);
    }

    r.fillRect({l.list.x, l.list.y - 1.0f, l.list.width, 1.0f}, rule);
    r.setClipRect(l.list);
    for (size_t i = 0; i < m_entries.size(); ++i) {
        const NUIRect row{l.list.x, l.list.y + static_cast<float>(i) * rowHeight() - m_scroll, l.list.width, rowHeight()};
        if (row.bottom() < l.list.y || row.y > l.list.bottom()) continue;
        const auto& e = m_entries[i];
        const bool hot = static_cast<int>(i) == m_hover;
        if (static_cast<int>(i) == m_selected) r.fillRect(row, theme.getColor("surfaceRaised"));
        else if (hot) r.fillRect(row, theme.getColor("surfaceTertiary"));
        // A folder is an outline, a sound is a small waveform: the two read apart at a glance.
        const float cy = row.y + row.height * 0.5f;
        if (e.folder) {
            r.strokeRect({row.x + 24.0f, cy - 6.0f, 16.0f, 12.0f}, 1.2f, muted);
            r.fillRect({row.x + 24.0f, cy - 8.0f, 7.0f, 3.0f}, muted);
        } else {
            static const float bars[] = {4, 9, 6, 12, 8, 5, 10, 4};
            for (int k = 0; k < 8; ++k) r.fillRect({row.x + 24.0f + static_cast<float>(k) * 2.2f, cy - bars[k] * 0.5f, 1.4f, bars[k]}, theme.getColor("accentPrimary").withAlpha(0.85f));
        }
        std::string name = e.name;
        const float maxName = row.width - 24.0f - 56.0f - 24.0f - (hot && !e.folder ? 150.0f : 74.0f);
        while (name.size() > 4 && r.measureText(name, 12.5f).width > maxName) name.pop_back();
        if (name != e.name) name += "...";
        r.drawText(name, {row.x + 56.0f, std::round(r.calculateTextY(row, 12.5f))}, 12.5f, e.folder ? text : text.withAlpha(0.95f));
        if (e.folder) continue;
        if (hot) {
            const NUIRect play = playButton(row), ref = refButton(row);
            for (const auto& btn : {std::make_pair(play, std::string("Play")), std::make_pair(ref, std::string("+ Reference"))}) {
                r.fillRect(btn.first, theme.getColor("backgroundSecondary"));
                r.strokeRect(btn.first, 1.0f, rule);
                const float tw = r.measureText(btn.second, 10.5f).width;
                r.drawText(btn.second, {btn.first.x + (btn.first.width - tw) * 0.5f, std::round(r.calculateTextY(btn.first, 10.5f))}, 10.5f, muted);
            }
        } else {
            const std::string size = AuditionFiles::sizeLabel(e.bytes);
            const float sw = r.measureText(size, 10.5f).width;
            r.drawText(size, {row.right() - 24.0f - sw, std::round(r.calculateTextY(row, 10.5f))}, 10.5f, muted.withAlpha(0.7f));
        }
    }
    if (m_entries.empty()) r.drawTextCentered("No audio or folders here", l.list, 12.0f, muted.withAlpha(0.7f));
    r.clearClipRect();
}

bool AuditionFilesPage::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    auto& drag = NUIDragDropManager::getInstance();
    const Layout l = layout();
    const NUIPoint p = event.position;

    if (m_dragging) {  // the drag belongs to this page until the button goes up
        if (drag.isDragging()) drag.updateDrag(p);
        if (!event.pressed && event.button == NUIMouseButton::Left && event.type != NUIMouseEventType::Move) {
            drag.endDrag(p);
            m_dragging = false;
            m_pressed = -1;
        }
        return true;
    }
    if (!getBounds().contains(p)) return false;

    if (event.type == NUIMouseEventType::Move) {
        const int row = rowAt(p, l);
        std::string control;
        if (l.up.contains(p)) control = "up";
        for (size_t i = 0; i < l.places.size(); ++i) if (l.places[i].contains(p)) control = "place" + std::to_string(i);
        if (row != m_hover || control != m_hoverControl) { m_hover = row; m_hoverControl = control; setDirty(true); }
        if (m_pressed >= 0 && m_pressed < static_cast<int>(m_entries.size()) && (event.pressed || m_pressPos.x != 0.0f)) {
            const float dx = p.x - m_pressPos.x, dy = p.y - m_pressPos.y;
            if (std::sqrt(dx * dx + dy * dy) >= drag.getDragThreshold() && !m_entries[static_cast<size_t>(m_pressed)].folder) {
                const auto& e = m_entries[static_cast<size_t>(m_pressed)];
                DragData data;
                data.type = DragDataType::File;
                data.filePath = e.path;
                data.displayName = e.name;
                data.accentColor = NUIThemeManager::getInstance().getColor("accentPrimary");
                data.previewWidth = 180.0f;
                data.previewHeight = 30.0f;
                drag.beginDrag(data, m_pressPos, this);
                m_dragging = true;
            }
        }
        return true;
    }
    if (event.type == NUIMouseEventType::Scroll) {
        m_scroll -= event.wheelDelta * 60.0f;
        clampScroll();
        setDirty(true);
        return true;
    }
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (l.up.contains(p)) { setDirectory(std::filesystem::path(m_dir).parent_path().string()); return true; }
        for (size_t i = 0; i < l.places.size(); ++i) if (l.places[i].contains(p)) { setDirectory(m_places[i].path); return true; }
        const int row = rowAt(p, l);
        if (row < 0) return true;
        const auto& e = m_entries[static_cast<size_t>(row)];
        const NUIRect rowRect{l.list.x, l.list.y + static_cast<float>(row) * rowHeight() - m_scroll, l.list.width, rowHeight()};
        if (e.folder) { setDirectory(e.path); return true; }
        if (m_hover == row && playButton(rowRect).contains(p)) { if (m_onPlay) m_onPlay(e.path); return true; }
        if (m_hover == row && refButton(rowRect).contains(p)) { if (m_onAddReference) m_onAddReference(e.path); return true; }
        m_selected = row;
        m_pressed = row;
        m_pressPos = p;
        if (event.type == NUIMouseEventType::DoubleClick && m_onPlay) m_onPlay(e.path);
        setDirty(true);
        return true;
    }
    if (event.released && event.button == NUIMouseButton::Left) { m_pressed = -1; m_pressPos = {0.0f, 0.0f}; return true; }
    return true;
}

} // namespace Aestra
