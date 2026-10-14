// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AuditionQueuePage.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../../AestraCore/include/AestraLog.h"
#include "../Core/AuditionLibrary.h"
#include "AuditionCover.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>

using namespace AestraUI;

namespace Aestra {

namespace {
std::string mmss(double seconds) {
    const int total = static_cast<int>(std::max(0.0, seconds));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return buf;
}
std::string titleOf(const Audio::AuditionQueueItem& item) { return item.title.empty() ? std::filesystem::path(item.filePath).stem().string() : item.title; }
NUIComponent* rootOf(NUIComponent* c) { while (c && c->getParent()) c = c->getParent(); return c; }
}

AuditionQueuePage::AuditionQueuePage(std::shared_ptr<Audio::AuditionEngine> engine) : m_engine(std::move(engine)) {}

AuditionQueuePage::~AuditionQueuePage() {
    NUIDragDropManager::getInstance().unregisterDropTarget(this);
    closeMenu();
}

void AuditionQueuePage::onUpdate(double deltaTime) {
    NUIComponent::onUpdate(deltaTime);
    if (!m_registered) {
        try {
            if (auto target = std::dynamic_pointer_cast<IDropTarget>(shared_from_this())) {
                NUIDragDropManager::getInstance().registerDropTarget(target);
                m_registered = true;
            }
        } catch (const std::bad_weak_ptr&) {}
    }
    if (isVisible() && m_engine && m_engine->isPlaying()) setDirty(true);
}

AuditionQueuePage::Layout AuditionQueuePage::layout() const {
    Layout l;
    const NUIRect b = getBounds();
    l.header = {b.x + 32.0f, b.y + 24.0f, b.width - 64.0f, 32.0f};
    l.clear = {b.right() - 32.0f - 64.0f, b.y + 26.0f, 64.0f, 26.0f};
    l.list = {b.x, b.y + 76.0f, b.width, std::max(0.0f, b.height - 76.0f)};
    return l;
}

int AuditionQueuePage::rowAt(const NUIPoint& p, const Layout& l) const {
    if (!m_engine || !l.list.contains(p)) return -1;
    const int row = static_cast<int>((p.y - l.list.y + m_scroll) / kRowH);
    return row >= 0 && row < static_cast<int>(m_engine->getQueue().size()) ? row : -1;
}

NUIRect AuditionQueuePage::removeButton(const NUIRect& row) const { return {row.right() - 32.0f - 24.0f, row.y + 14.0f, 24.0f, 24.0f}; }

bool AuditionQueuePage::getRowCenter(size_t row, NUIPoint& out) const {
    if (!m_engine || row >= m_engine->getQueue().size()) return false;
    const Layout l = layout();
    out = {l.list.x + 200.0f, l.list.y + (static_cast<float>(row) + 0.5f) * kRowH - m_scroll};
    return true;
}

void AuditionQueuePage::onRender(NUIRenderer& r) {
    if (!isVisible() || !m_engine) return;
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary"), muted = theme.getColor("textSecondary"), rule = theme.getColor("borderSubtle");
    const NUIRect b = getBounds();
    const Layout l = layout();
    r.fillRect(b, theme.getColor("backgroundPrimary"));
    const auto& queue = m_engine->getQueue();
    const auto current = m_engine->getCurrentItem();

    r.drawText("Queue", {l.header.x, l.header.y}, 20.0f, text);
    if (!queue.empty()) {
        r.drawText(std::to_string(queue.size()) + (queue.size() == 1 ? " track" : " tracks"), {l.header.x + 70.0f, l.header.y + 6.0f}, 11.5f, muted);
        r.fillRect(l.clear, m_hoverClear ? theme.getColor("surfaceRaised") : theme.getColor("surfaceTertiary"));
        const float cw = r.measureText("Clear", 11.0f).width;
        r.drawText("Clear", {l.clear.x + (l.clear.width - cw) * 0.5f, std::round(r.calculateTextY(l.clear, 11.0f))}, 11.0f, text);
    }
    r.fillRect({l.list.x, l.list.y - 1.0f, l.list.width, 1.0f}, rule);

    if (queue.empty()) {
        r.drawTextCentered(m_dropHover ? "Drop to add to the queue" : "Nothing queued. Drag audio here from Files.", l.list, 12.5f, m_dropHover ? text : muted.withAlpha(0.75f));
    }
    r.setClipRect(l.list);
    for (size_t i = 0; i < queue.size(); ++i) {
        const NUIRect row{l.list.x, l.list.y + static_cast<float>(i) * kRowH - m_scroll, l.list.width, kRowH};
        if (row.bottom() < l.list.y || row.y > l.list.bottom()) continue;
        const auto& item = queue[i];
        const bool playing = current && current->id == item.id;
        const bool hot = static_cast<int>(i) == m_hover && !m_reordering;
        if (playing) r.fillRect(row, theme.getColor("accentPrimary").withAlpha(0.12f));
        else if (hot) r.fillRect(row, theme.getColor("surfaceTertiary"));
        r.fillRect({row.x + 32.0f, row.y + 10.0f, 32.0f, 32.0f}, NUIColor(0, 0, 0, 1));
        drawCover(r, {row.x + 32.0f, row.y + 10.0f, 32.0f, 32.0f}, AuditionLibrary::seedOf(titleOf(item)));
        if (playing && m_engine->isPlaying()) {  // three small bars, moving
            const float t = static_cast<float>(std::fmod(m_scroll * 0.0f + static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()) / 140.0, 6.28318));
            for (int k = 0; k < 3; ++k) {
                const float h = 6.0f + 8.0f * (0.5f + 0.5f * std::sin(t + static_cast<float>(k) * 1.7f));
                r.fillRect({row.x + 42.0f + static_cast<float>(k) * 5.0f, row.y + 26.0f - h * 0.5f, 3.0f, h}, NUIColor::white());
            }
        }
        r.drawText(titleOf(item), {row.x + 80.0f, row.y + 11.0f}, 13.0f, playing ? theme.getColor("accentPrimary").lightened(0.3f) : text);
        const std::string sub = item.isReference ? "Reference" : item.isFromTimeline ? "From the timeline" : (item.artist.empty() || item.artist == "Unknown Artist" ? "" : item.artist);
        r.drawText(sub, {row.x + 80.0f, row.y + 29.0f}, 11.0f, muted);
        const std::string dur = mmss(item.durationSeconds);
        const float dw = r.measureText(dur, 11.0f).width;
        r.drawText(dur, {row.right() - 32.0f - 24.0f - 20.0f - dw, std::round(r.calculateTextY(row, 11.0f))}, 11.0f, muted);
        if (hot) {
            const NUIRect x = removeButton(row);
            r.drawLine({x.x + 7.0f, x.y + 7.0f}, {x.right() - 7.0f, x.bottom() - 7.0f}, 1.4f, muted);
            r.drawLine({x.right() - 7.0f, x.y + 7.0f}, {x.x + 7.0f, x.bottom() - 7.0f}, 1.4f, muted);
        }
        r.fillRect({row.x + 32.0f, row.bottom() - 1.0f, row.width - 64.0f, 1.0f}, rule.withAlpha(0.6f));
    }
    if (m_reordering && m_insert >= 0) r.fillRect({l.list.x + 32.0f, l.list.y + static_cast<float>(m_insert) * kRowH - m_scroll - 1.0f, l.list.width - 64.0f, 2.0f}, theme.getColor("accentPrimary"));
    r.clearClipRect();
    if (m_dropHover && !queue.empty()) r.strokeRect({b.x + 8.0f, b.y + 8.0f, b.width - 16.0f, b.height - 16.0f}, 2.0f, theme.getColor("accentPrimary").withAlpha(0.7f));
    if (m_menu) m_menu->onRender(r);
}

void AuditionQueuePage::closeMenu() {
    if (m_menu) { if (auto* p = m_menu->getParent()) p->removeChild(m_menu); }
    m_menu.reset();
}

void AuditionQueuePage::openMenu(size_t index, const NUIPoint& at) {
    closeMenu();
    if (!m_engine || index >= m_engine->getQueue().size()) return;
    m_menu = std::make_shared<NUIContextMenu>();
    m_menu->addItem("Play now", [this, index]() { m_engine->jumpToTrack(index); m_engine->play(); if (m_onPlayIndex) m_onPlayIndex(index); });
    m_menu->addSeparator();
    auto top = std::make_shared<NUIContextMenuItem>("Move to top");
    top->setEnabled(index > 0);
    top->setOnClick([this, index]() { m_engine->moveQueueItem(index, 0); });
    m_menu->addItem(top);
    m_menu->addItem("Remove", [this, index]() { m_engine->removeFromQueue(index); });
    if (auto* root = rootOf(this)) { root->addChild(m_menu); m_menu->showAt(at); root->repaint(); }
}

bool AuditionQueuePage::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible() || !m_engine) return false;
    if (m_menu) {
        if (m_menu->onMouseEvent(event)) return true;
        if (event.pressed) { closeMenu(); setDirty(true); }
    }
    const NUIPoint p = event.position;
    const Layout l = layout();
    const size_t count = m_engine->getQueue().size();

    if (m_reordering) {  // dragging a row to a new place
        const int slot = std::clamp(static_cast<int>(std::lround((p.y - l.list.y + m_scroll) / kRowH)), 0, static_cast<int>(count));
        m_insert = slot;
        if (!event.pressed && event.button == NUIMouseButton::Left && event.type != NUIMouseEventType::Move) {
            const size_t from = static_cast<size_t>(m_pressed);
            size_t to = static_cast<size_t>(slot);
            if (to > from) --to;
            if (to != from) m_engine->moveQueueItem(from, std::min(to, count - 1));
            m_reordering = false; m_pressed = -1; m_insert = -1;
        }
        setDirty(true);
        return true;
    }
    if (!getBounds().contains(p)) return false;

    if (event.type == NUIMouseEventType::Move) {
        const int row = rowAt(p, l);
        const bool clear = count > 0 && l.clear.contains(p);
        if (row != m_hover || clear != m_hoverClear) { m_hover = row; m_hoverClear = clear; setDirty(true); }
        if (m_pressed >= 0 && event.pressed) {
            const float dy = p.y - m_pressPos.y;
            if (std::fabs(dy) > 6.0f && count > 1) m_reordering = true;
        }
        return true;
    }
    if (event.type == NUIMouseEventType::Scroll) {
        m_scroll = std::clamp(m_scroll - event.wheelDelta * 60.0f, 0.0f, std::max(0.0f, static_cast<float>(count) * kRowH - l.list.height));
        setDirty(true);
        return true;
    }
    if (event.pressed && event.button == NUIMouseButton::Right) {
        const int row = rowAt(p, l);
        if (row >= 0) openMenu(static_cast<size_t>(row), p);
        return true;
    }
    if (event.pressed && event.button == NUIMouseButton::Left) {
        if (count > 0 && l.clear.contains(p)) { m_engine->clearQueue(); m_hover = -1; return true; }
        const int row = rowAt(p, l);
        if (row < 0) return true;
        const NUIRect rowRect{l.list.x, l.list.y + static_cast<float>(row) * kRowH - m_scroll, l.list.width, kRowH};
        if (removeButton(rowRect).contains(p)) { m_engine->removeFromQueue(static_cast<size_t>(row)); m_hover = -1; return true; }
        m_pressed = row;
        m_pressPos = p;
        return true;
    }
    if (event.released && event.button == NUIMouseButton::Left) {
        if (m_pressed >= 0 && rowAt(p, l) == m_pressed) {  // a click, not a drag: play it
            m_engine->jumpToTrack(static_cast<size_t>(m_pressed));
            m_engine->play();
            if (m_onPlayIndex) m_onPlayIndex(static_cast<size_t>(m_pressed));
        }
        m_pressed = -1;
        return true;
    }
    return true;
}

DropFeedback AuditionQueuePage::onDragEnter(const DragData& data, const NUIPoint&) {
    if (data.type != DragDataType::File) return DropFeedback::None;
    m_dropHover = true;
    setDirty(true);
    return DropFeedback::Copy;
}

DropFeedback AuditionQueuePage::onDragOver(const DragData& data, const NUIPoint&) {
    if (data.type != DragDataType::File) return DropFeedback::None;
    m_dropHover = true;
    return DropFeedback::Copy;
}

DropResult AuditionQueuePage::onDrop(const DragData& data, const NUIPoint&) {
    DropResult result;
    m_dropHover = false;
    if (data.type == DragDataType::File && m_engine) {
        Log::info("[Audition] Dropped file: " + data.filePath);
        m_engine->addToQueue(data.filePath);
        result.accepted = true;
        result.message = "Added to queue";
    }
    setDirty(true);
    return result;
}

} // namespace Aestra
