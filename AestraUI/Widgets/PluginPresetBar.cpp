// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PluginPresetBar.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cctype>

namespace AestraUI {

using Aestra::Audio::Presets::Preset;
using Aestra::Audio::Presets::PresetSession;
using Aestra::Audio::Presets::PresetSource;
using Aestra::Audio::Presets::presetSourceName;

namespace {

constexpr float kBarH = 24.0f;
constexpr float kArrowW = 24.0f;
constexpr float kSaveW = 48.0f;
constexpr float kAbW = 48.0f;
constexpr float kGap = 6.0f;
constexpr float kPad = 10.0f;
constexpr float kSearchH = 28.0f;
constexpr float kChipH = 22.0f;
constexpr float kFooterH = 28.0f;
constexpr float kSaveW_ = 340.0f;
constexpr float kSaveH = 196.0f;
constexpr const char* kFilterNames[] = {"All", "Factory", "Yours", "This project"};

struct Colors {
    NUIColor bed, surface, raised, border, text, text2, amber, accent;
};

Colors colors() {
    auto& t = NUIThemeManager::getInstance();
    return {t.getColor("backgroundPrimary"), t.getColor("backgroundSecondary"), t.getColor("surfaceRaised"),
            t.getColor("border"),            t.getColor("textPrimary"),         t.getColor("textSecondary"),
            t.getColor("warning"),           t.getColor("accentPrimary")};
}

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool matchesSearch(const Preset& p, const std::string& search) {
    if (search.empty()) return true;
    const std::string q = lower(search);
    std::string hay = lower(p.name + " " + p.category + " " + p.description);
    for (const auto& t : p.tags) hay += " " + lower(t);
    return hay.find(q) != std::string::npos;
}

bool matchesFilter(const Preset& p, int filter) {
    switch (filter) {
    case 1: return p.source == PresetSource::Factory;
    case 2: return p.source == PresetSource::User;
    case 3: return p.source == PresetSource::Project;
    default: return true;
    }
}

void chevron(NUIRenderer& r, const NUIRect& box, bool left, const NUIColor& c) {
    const float cx = box.x + box.width * 0.5f, cy = box.y + box.height * 0.5f, d = 3.5f;
    const float dir = left ? 1.0f : -1.0f;
    r.drawLine({cx + dir * d * 0.5f, cy - d}, {cx - dir * d * 0.5f, cy}, 1.5f, c);
    r.drawLine({cx - dir * d * 0.5f, cy}, {cx + dir * d * 0.5f, cy + d}, 1.5f, c);
}

std::string fit(NUIRenderer& r, const std::string& text, float size, float width) {
    if (r.measureText(text, size).width <= width) return text;
    std::string s = text;
    while (!s.empty() && r.measureText(s + "…", size).width > width) s.pop_back();
    return s + "…";
}

} // namespace

PluginPresetBar::PluginPresetBar(std::unique_ptr<PresetSession> session) : m_session(std::move(session)) {}

// ---------------------------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------------------------

PluginPresetBar::Layout PluginPresetBar::layoutFor(const NUIRect& titleBar) const {
    const float total = kArrowW + kNameWidth + kArrowW + kGap + kSaveW + kGap + kAbW;
    float x = titleBar.x + (titleBar.width - total) * 0.5f;
    // Keep clear of the title text on the left: never start before a third of the bar.
    x = std::max(x, titleBar.x + std::min(160.0f, titleBar.width * 0.33f));
    const float y = titleBar.y + (titleBar.height - kBarH) * 0.5f;
    Layout l;
    l.prev = {x, y, kArrowW, kBarH};
    l.name = {l.prev.right(), y, kNameWidth, kBarH};
    l.next = {l.name.right(), y, kArrowW, kBarH};
    l.save = {l.next.right() + kGap, y, kSaveW, kBarH};
    l.ab = {l.save.right() + kGap, y, kAbW, kBarH};
    l.a = {l.ab.x, y, kAbW * 0.5f, kBarH};
    l.b = {l.ab.x + kAbW * 0.5f, y, kAbW * 0.5f, kBarH};
    return l;
}

NUIRect PluginPresetBar::browseRect(const NUIRect& window) const {
    const auto rows = static_cast<float>(std::clamp<int>(static_cast<int>(visibleRows().size()), 1, kMaxRows));
    const float h = kPad + kSearchH + 8.0f + kChipH + kPad + rows * kRowH + 6.0f + kFooterH;
    float x = m_layout.name.x + m_layout.name.width * 0.5f - kBrowseW * 0.5f;
    x = std::clamp(x, window.x + 4.0f, std::max(window.x + 4.0f, window.right() - kBrowseW - 4.0f));
    return {x, m_layout.name.bottom() + 4.0f, kBrowseW, h};
}

NUIRect PluginPresetBar::saveRect(const NUIRect& window) const {
    float x = m_layout.save.x + m_layout.save.width * 0.5f - kSaveW_ * 0.5f;
    x = std::clamp(x, window.x + 4.0f, std::max(window.x + 4.0f, window.right() - kSaveW_ - 4.0f));
    // Without project presets there is one place to keep it, so the form leaves the choice out.
    const float h = m_session->canSaveToProject() ? kSaveH : kSaveH - 56.0f;
    return {x, m_layout.save.bottom() + 4.0f, kSaveW_, h};
}

NUIRect PluginPresetBar::filterChipRect(const NUIRect& p, int chip) const {
    static const float widths[] = {36.0f, 58.0f, 50.0f, 84.0f};
    float x = p.x + kPad;
    for (int i = 0; i < chip; ++i) x += widths[i] + 6.0f;
    return {x, p.y + kPad + kSearchH + 8.0f, widths[chip], kChipH};
}

NUIRect PluginPresetBar::browseRowRect(const NUIRect& p, int i) const {
    const float top = p.y + kPad + kSearchH + 8.0f + kChipH + kPad;
    return {p.x, top + static_cast<float>(i) * kRowH, p.width, kRowH};
}

NUIRect PluginPresetBar::savePlaceRect(const NUIRect& p, int which) const {
    const float w = (p.width - 2.0f * kPad) * 0.5f;
    return {p.x + kPad + static_cast<float>(which) * w, p.y + 118.0f, w, 26.0f};
}

NUIRect PluginPresetBar::saveButtonRect(const NUIRect& p, int which) const {
    const float y = p.bottom() - kPad - 28.0f;
    if (which == 0) return {p.x + kPad, y, 64.0f, 28.0f};
    if (which == 1) return {p.right() - kPad - 92.0f, y, 92.0f, 28.0f};
    return {p.right() - kPad - 92.0f - 6.0f - 96.0f, y, 96.0f, 28.0f};
}

std::vector<size_t> PluginPresetBar::visibleRows() const {
    std::vector<size_t> out;
    const auto& all = m_session->presets();
    for (size_t i = 0; i < all.size(); ++i)
        if (matchesFilter(all[i], m_filter) && matchesSearch(all[i], m_search)) out.push_back(i);
    return out;
}

NUIRect PluginPresetBar::controlRect(int which) const {
    switch (which) {
    case 0: return m_layout.prev;
    case 1: return m_layout.name;
    case 2: return m_layout.next;
    case 3: return m_layout.save;
    case 4: return m_layout.a;
    case 5: return m_layout.b;
    default: return {};
    }
}

int PluginPresetBar::chipCount() const { return m_session->canSaveToProject() ? 4 : 3; }

void PluginPresetBar::setFilter(int f) {
    m_filter = std::clamp(f, 0, chipCount() - 1);
    m_scroll = 0;
}

// ---------------------------------------------------------------------------------------------------------------
// Actions
// ---------------------------------------------------------------------------------------------------------------

void PluginPresetBar::openBrowse() {
    if (m_popover == Popover::Save) closePopover(false);
    m_session->refresh();
    m_search.clear();
    m_filter = 0;
    m_scroll = 0;
    m_hoverRow.reset();
    m_session->beginBrowse();
    m_popover = Popover::Browse;
}

void PluginPresetBar::openSave() {
    if (m_popover == Popover::Browse) closePopover(false);
    m_saveName = m_session->suggestedName();
    m_saveNameFresh = true;
    m_savePlace = 0;
    m_saveError.clear();
    m_popover = Popover::Save;
}

void PluginPresetBar::closePopover(bool keep) {
    if (m_popover == Popover::Browse) {
        if (keep) m_session->keepBrowse();
        else m_session->cancelBrowse();
    }
    m_popover = Popover::None;
    m_hoverRow.reset();
}

void PluginPresetBar::hear(size_t presetIndex) {
    if (m_session->auditioned() == presetIndex) return;
    m_session->audition(presetIndex);
}

void PluginPresetBar::moveSelection(int delta) {
    const auto rows = visibleRows();
    if (rows.empty()) return;
    int at = -1;
    if (auto a = m_session->auditioned())
        for (size_t i = 0; i < rows.size(); ++i)
            if (rows[i] == *a) at = static_cast<int>(i);
    const int n = static_cast<int>(rows.size());
    const int next = at < 0 ? (delta > 0 ? 0 : n - 1) : std::clamp(at + delta, 0, n - 1);
    hear(rows[static_cast<size_t>(next)]);
    if (next < m_scroll) m_scroll = next;
    if (next >= m_scroll + kMaxRows) m_scroll = next - kMaxRows + 1;
}

void PluginPresetBar::doSave(bool update) {
    const auto place = m_savePlace == 1 ? PresetSession::Place::Project : PresetSession::Place::Yours;
    std::string error;
    if (m_session->save(m_saveName, place, update, &error)) {
        m_popover = Popover::None;
    } else {
        m_saveError = error;
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------

void PluginPresetBar::render(NUIRenderer& r, const NUIRect& titleBar) {
    m_titleBar = titleBar;
    m_layout = layoutFor(titleBar);
    const Colors c = colors();
    const Layout& l = m_layout;

    // ‹ name › as one framed control
    const NUIRect frame{l.prev.x, l.prev.y, l.next.right() - l.prev.x, kBarH};
    r.fillRoundedRect(frame, 4.0f, c.bed);
    r.strokeRoundedRect(frame, 4.0f, 1.0f, m_popover == Popover::Browse ? c.accent : c.border);
    r.drawLine({l.name.x, l.name.y + 4.0f}, {l.name.x, l.name.bottom() - 4.0f}, 1.0f, c.border);
    r.drawLine({l.next.x, l.next.y + 4.0f}, {l.next.x, l.next.bottom() - 4.0f}, 1.0f, c.border);
    chevron(r, l.prev, true, m_hoverControl == 0 ? c.text : c.text2);
    chevron(r, l.next, false, m_hoverControl == 2 ? c.text : c.text2);

    const auto& current = m_session->current();
    const int changes = m_session->changes();
    float tx = l.name.x + 10.0f;
    const float base = r.calculateTextY(l.name, 12.0f);
    if (changes > 0) {
        r.fillRoundedRect({tx, l.name.y + kBarH * 0.5f - 3.0f, 6.0f, 6.0f}, 3.0f, c.amber);
        tx += 12.0f;
    }
    std::string meta;
    NUIColor metaColor = c.text2;
    if (m_session->comparing()) meta = m_session->activeSlot() == 0 ? "A" : "B";
    if (changes > 0) {
        meta = changes == 1 ? "1 change" : std::to_string(changes) + " changes";
        metaColor = c.amber;
    } else if (current) {
        meta = presetSourceName(current->source);
    }
    const float metaW = meta.empty() ? 0.0f : r.measureText(meta, 10.5f).width;
    const float nameRoom = l.name.right() - 22.0f - tx - (metaW > 0 ? metaW + 8.0f : 0.0f);
    const std::string name = current ? current->name : "No preset";
    r.drawText(fit(r, name, 12.0f, nameRoom), {tx, base}, 12.0f, current ? c.text : c.text2);
    if (!meta.empty())
        r.drawText(meta, {l.name.right() - 20.0f - metaW, r.calculateTextY(l.name, 10.5f)}, 10.5f, metaColor);
    // ▾
    const float vx = l.name.right() - 12.0f, vy = l.name.y + kBarH * 0.5f;
    r.drawLine({vx - 3.0f, vy - 1.5f}, {vx, vy + 1.5f}, 1.3f, c.text2);
    r.drawLine({vx, vy + 1.5f}, {vx + 3.0f, vy - 1.5f}, 1.3f, c.text2);

    // Save
    r.fillRoundedRect(l.save, 4.0f, m_hoverControl == 3 ? c.raised : c.surface);
    r.strokeRoundedRect(l.save, 4.0f, 1.0f, m_popover == Popover::Save ? c.accent : c.border);
    r.drawTextCentered("Save", l.save, 11.5f, c.text);

    // A | B
    const bool cmp = m_session->comparing();
    const int slot = m_session->activeSlot();
    r.fillRoundedRect(l.ab, 4.0f, c.surface);
    if (cmp) r.fillRoundedRect(slot == 0 ? l.a : l.b, 4.0f, c.accent);
    r.strokeRoundedRect(l.ab, 4.0f, 1.0f, c.border);
    r.drawLine({l.b.x, l.b.y}, {l.b.x, l.b.bottom()}, 1.0f, c.border);
    r.drawTextCentered("A", l.a, 11.5f, cmp && slot == 0 ? c.bed : c.text2);
    r.drawTextCentered("B", l.b, 11.5f, cmp && slot == 1 ? c.bed : c.text2);
}

void PluginPresetBar::renderOverlay(NUIRenderer& r, const NUIRect& window) {
    m_window = window;
    if (m_popover == Popover::None) return;
    const Colors c = colors();

    if (m_popover == Popover::Browse) {
        const NUIRect p = browseRect(window);
        m_popoverRect = p;
        r.fillRoundedRect({p.x, p.y + 6.0f, p.width, p.height}, 8.0f, NUIColor(0, 0, 0, 0.45f));
        r.fillRoundedRect(p, 8.0f, c.raised);
        r.strokeRoundedRect(p, 8.0f, 1.0f, c.border);

        // search
        const NUIRect search{p.x + kPad, p.y + kPad, p.width - 2.0f * kPad, kSearchH};
        r.fillRoundedRect(search, 4.0f, c.bed);
        r.strokeRoundedRect(search, 4.0f, 1.0f, c.accent);
        const float sy = r.calculateTextY(search, 12.0f);
        const auto rows = visibleRows();
        if (m_search.empty())
            r.drawText("Type to search", {search.x + 10.0f, sy}, 12.0f, c.text2);
        else
            r.drawText(m_search, {search.x + 10.0f, sy}, 12.0f, c.text);
        const std::string count = std::to_string(rows.size()) + (rows.size() == 1 ? " preset" : " presets");
        r.drawText(count, {search.right() - 10.0f - r.measureText(count, 10.5f).width, r.calculateTextY(search, 10.5f)},
                   10.5f, c.text2);

        // filter chips
        for (int i = 0; i < chipCount(); ++i) {
            const NUIRect chip = filterChipRect(p, i);
            const bool on = m_filter == i;
            r.fillRoundedRect(chip, kChipH * 0.5f, on ? c.accent : c.surface);
            if (!on) r.strokeRoundedRect(chip, kChipH * 0.5f, 1.0f, c.border);
            r.drawTextCentered(kFilterNames[i], chip, 11.0f, on ? c.bed : c.text);
        }

        // rows
        const auto& all = m_session->presets();
        const auto heard = m_session->auditioned();
        const int first = std::clamp(m_scroll, 0, std::max(0, static_cast<int>(rows.size()) - kMaxRows));
        if (rows.empty()) {
            r.drawText(all.empty() ? "No presets for this plugin yet. Save one to start." : "Nothing matches.",
                       {p.x + 16.0f, r.calculateTextY(browseRowRect(p, 0), 12.0f)}, 12.0f, c.text2);
        }
        for (int i = 0; i < kMaxRows && first + i < static_cast<int>(rows.size()); ++i) {
            const size_t idx = rows[static_cast<size_t>(first + i)];
            const Preset& pr = all[idx];
            const NUIRect row = browseRowRect(p, i);
            const bool isHeard = heard && *heard == idx;
            const auto& cur = m_session->current();
            const bool wasCurrent = !heard && cur && cur->name == pr.name && cur->source == pr.source;
            if (isHeard) {
                r.fillRect(row, c.surface);
                r.fillRect({row.x, row.y, 2.0f, row.height}, c.accent);
            } else if (wasCurrent || (m_hoverRow && *m_hoverRow == idx)) {
                r.fillRect(row, c.surface.withAlpha(0.6f));
            }
            const float by = r.calculateTextY(row, 12.0f);
            if (isHeard) {
                // a small speaker: this is the one on the track now
                const float sx = row.x + 12.0f, cy = row.y + row.height * 0.5f;
                r.fillRect({sx, cy - 2.0f, 3.0f, 4.0f}, c.accent);
                r.drawLine({sx + 3.0f, cy - 2.0f}, {sx + 6.0f, cy - 5.0f}, 1.3f, c.accent);
                r.drawLine({sx + 3.0f, cy + 2.0f}, {sx + 6.0f, cy + 5.0f}, 1.3f, c.accent);
                r.drawLine({sx + 6.0f, cy - 5.0f}, {sx + 6.0f, cy + 5.0f}, 1.3f, c.accent);
            }
            r.drawText(fit(r, pr.name, 12.0f, 150.0f), {row.x + 28.0f, by}, 12.0f, c.text);
            const std::string desc = !pr.description.empty() ? pr.description : pr.category;
            if (!desc.empty())
                r.drawText(fit(r, desc, 11.0f, p.width - 28.0f - 160.0f - 90.0f), {row.x + 188.0f, by}, 11.0f, c.text2);
            const char* src = presetSourceName(pr.source);
            r.drawText(src, {row.right() - 14.0f - r.measureText(src, 10.5f).width, by}, 10.5f, c.text2);
        }

        // footer
        const NUIRect foot{p.x, p.bottom() - kFooterH, p.width, kFooterH};
        r.drawLine({foot.x, foot.y}, {foot.right(), foot.y}, 1.0f, c.border);
        const float fy = r.calculateTextY(foot, 11.0f);
        std::string left;
        if (heard && *heard < all.size()) left = "Hearing " + all[*heard].name;
        else left = "Arrows or hover to hear";
        r.drawText(fit(r, left, 11.0f, p.width * 0.45f), {foot.x + 12.0f, fy}, 11.0f, c.text);
        const auto& before = m_session->current();
        const std::string right = "Enter keep   Esc back to " + (before ? before->name : std::string("before"));
        r.drawText(fit(r, right, 11.0f, p.width * 0.5f),
                   {foot.right() - 12.0f - std::min(p.width * 0.5f, r.measureText(right, 11.0f).width), fy}, 11.0f,
                   c.text2);
        return;
    }

    // Save
    const NUIRect p = saveRect(window);
    m_popoverRect = p;
    r.fillRoundedRect({p.x, p.y + 6.0f, p.width, p.height}, 8.0f, NUIColor(0, 0, 0, 0.45f));
    r.fillRoundedRect(p, 8.0f, c.raised);
    r.strokeRoundedRect(p, 8.0f, 1.0f, c.border);
    r.drawText("NAME", {p.x + kPad, p.y + kPad + 4.0f}, 10.0f, c.text2);
    const NUIRect field{p.x + kPad, p.y + kPad + 20.0f, p.width - 2.0f * kPad, 30.0f};
    r.fillRoundedRect(field, 4.0f, c.bed);
    r.strokeRoundedRect(field, 4.0f, 1.0f, c.accent);
    const float fy = r.calculateTextY(field, 13.0f);
    if (m_saveNameFresh && !m_saveName.empty()) {
        const float w = r.measureText(m_saveName, 13.0f).width;
        r.fillRect({field.x + 9.0f, field.y + 6.0f, w + 2.0f, field.height - 12.0f}, c.accent.withAlpha(0.28f));
    }
    r.drawText(m_saveName, {field.x + 10.0f, fy}, 13.0f, c.text);
    if (!m_saveNameFresh) {
        const float cx = field.x + 10.0f + r.measureText(m_saveName, 13.0f).width + 1.0f;
        r.drawLine({cx, field.y + 7.0f}, {cx, field.bottom() - 7.0f}, 1.0f, c.text);
    }
    r.drawText(m_saveError.empty() ? "Filled in for you. Type to change it." : m_saveError,
               {p.x + kPad, field.bottom() + 16.0f}, 10.5f, m_saveError.empty() ? c.text2 : c.amber);

    if (m_session->canSaveToProject()) {
    r.drawText("KEEP IT IN", {p.x + kPad, p.y + 98.0f}, 10.0f, c.text2);
    for (int i = 0; i < 2; ++i) {
        const NUIRect seg = savePlaceRect(p, i);
        const bool on = m_savePlace == i;
        r.fillRect(seg, on ? c.surface : c.bed);
        if (on) r.fillRect({seg.x, seg.bottom() - 2.0f, seg.width, 2.0f}, c.accent);
        r.drawTextCentered(i == 0 ? "Yours, every project" : "This project only", seg, 11.0f, on ? c.text : c.text2);
    }
    r.strokeRoundedRect({p.x + kPad, p.y + 118.0f, p.width - 2.0f * kPad, 26.0f}, 4.0f, 1.0f, c.border);
    }

    const bool update = m_session->canUpdateCurrent() && m_savePlace == 0 &&
                        m_session->current()->source == PresetSource::User &&
                        lower(m_saveName) == lower(m_session->current()->name);
    const NUIRect cancel = saveButtonRect(p, 0), primary = saveButtonRect(p, 1);
    r.strokeRoundedRect(cancel, 4.0f, 1.0f, c.border);
    r.drawTextCentered("Cancel", cancel, 11.5f, c.text2);
    r.fillRoundedRect(primary, 4.0f, c.accent);
    r.drawTextCentered(update ? "Update  \xE2\x86\xB5" : "Save  \xE2\x86\xB5", primary, 11.5f, c.bed);
    if (update) {
        const NUIRect asNew = saveButtonRect(p, 2);
        r.strokeRoundedRect(asNew, 4.0f, 1.0f, c.border);
        r.drawTextCentered("Save as new", asNew, 11.5f, c.text);
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------------------------

bool PluginPresetBar::onMouseEvent(const NUIMouseEvent& e) {
    const Layout& l = m_layout;
    const bool press = e.pressed && e.button == NUIMouseButton::Left;
    const bool releaseL = e.released && e.button == NUIMouseButton::Left;

    if (m_popover == Popover::Browse) {
        const NUIRect p = m_popoverRect;
        if (p.contains(e.position)) {
            const auto rows = visibleRows();
            const int first = std::clamp(m_scroll, 0, std::max(0, static_cast<int>(rows.size()) - kMaxRows));
            std::optional<size_t> under;
            for (int i = 0; i < kMaxRows && first + i < static_cast<int>(rows.size()); ++i)
                if (browseRowRect(p, i).contains(e.position)) under = rows[static_cast<size_t>(first + i)];
            if (e.wheelDelta != 0.0f) {
                m_scroll = std::clamp(m_scroll - (e.wheelDelta > 0 ? 1 : -1), 0,
                                      std::max(0, static_cast<int>(rows.size()) - kMaxRows));
                return true;
            }
            if (under && under != m_hoverRow) {
                m_hoverRow = under;
                hear(*under); // heard live as the pointer reaches it
            }
            if (press) {
                for (int i = 0; i < chipCount(); ++i)
                    if (filterChipRect(p, i).contains(e.position)) {
                        setFilter(i);
                        return true;
                    }
                if (under) {
                    hear(*under);
                    closePopover(true); // a click keeps it
                }
            }
            return true;
        }
        if (press) {
            // A click on the name toggles; anywhere else outside puts back what was there.
            closePopover(false);
            return l.name.contains(e.position);
        }
        return false;
    }

    if (m_popover == Popover::Save) {
        const NUIRect p = m_popoverRect;
        if (p.contains(e.position)) {
            if (press) {
                for (int i = 0; i < 2 && m_session->canSaveToProject(); ++i)
                    if (savePlaceRect(p, i).contains(e.position)) m_savePlace = i;
                if (saveButtonRect(p, 0).contains(e.position)) closePopover(false);
                const bool update = m_session->canUpdateCurrent() && m_savePlace == 0 &&
                                    lower(m_saveName) == lower(m_session->current()->name);
                if (saveButtonRect(p, 1).contains(e.position)) doSave(update);
                else if (update && saveButtonRect(p, 2).contains(e.position)) {
                    m_saveName = m_session->suggestedName() + " 2";
                    m_saveNameFresh = false;
                    doSave(false);
                }
            }
            return true;
        }
        if (press) {
            closePopover(false);
            return l.save.contains(e.position);
        }
        return false;
    }

    // The bar itself.
    const NUIRect frame{l.prev.x, l.prev.y, l.ab.right() - l.prev.x, kBarH};
    if (!frame.contains(e.position)) {
        if (m_hoverControl != -1) m_hoverControl = -1;
        return false;
    }
    m_hoverControl = l.prev.contains(e.position)   ? 0
                     : l.name.contains(e.position) ? 1
                     : l.next.contains(e.position) ? 2
                     : l.save.contains(e.position) ? 3
                     : l.ab.contains(e.position)   ? 4
                                                   : -1;
    if (e.wheelDelta != 0.0f && (m_hoverControl == 1)) {
        m_session->step(e.wheelDelta > 0 ? -1 : 1);
        return true;
    }
    if (press) {
        switch (m_hoverControl) {
        case 0: m_session->step(-1); break;
        case 1: m_namePressed = true; break;
        case 2: m_session->step(+1); break;
        case 3: openSave(); break;
        case 4:
            if (!m_session->comparing()) m_session->beginCompare();
            if ((l.a.contains(e.position) && m_session->activeSlot() == 1) ||
                (l.b.contains(e.position) && m_session->activeSlot() == 0))
                m_session->flip();
            break;
        default: break;
        }
        return true;
    }
    if (releaseL && m_namePressed) {
        m_namePressed = false;
        if (l.name.contains(e.position)) openBrowse();
        return true;
    }
    return m_hoverControl >= 0;
}

bool PluginPresetBar::onKeyEvent(const NUIKeyEvent& e) {
    if (!e.pressed) return m_popover != Popover::None;
    if (m_popover == Popover::Browse) {
        switch (e.keyCode) {
        case NUIKeyCode::Escape: closePopover(false); return true;
        case NUIKeyCode::Enter: closePopover(true); return true;
        case NUIKeyCode::Up: moveSelection(-1); return true;
        case NUIKeyCode::Down: moveSelection(+1); return true;
        case NUIKeyCode::Backspace:
            if (!m_search.empty()) m_search.pop_back();
            m_scroll = 0;
            return true;
        default: break;
        }
        if (e.character >= 32 && e.character < 127) {
            m_search += e.character;
            m_scroll = 0;
        }
        return true; // a key typed into the browser never reaches transport or musical typing
    }
    if (m_popover == Popover::Save) {
        switch (e.keyCode) {
        case NUIKeyCode::Escape: closePopover(false); return true;
        case NUIKeyCode::Enter: {
            const bool update = m_session->canUpdateCurrent() && m_savePlace == 0 &&
                                lower(m_saveName) == lower(m_session->current()->name);
            doSave(update);
            return true;
        }
        case NUIKeyCode::Tab:
            if (m_session->canSaveToProject()) m_savePlace = 1 - m_savePlace;
            return true;
        case NUIKeyCode::Backspace:
            if (m_saveNameFresh) m_saveName.clear();
            else if (!m_saveName.empty()) m_saveName.pop_back();
            m_saveNameFresh = false;
            return true;
        default: break;
        }
        if (e.character >= 32 && e.character < 127) {
            if (m_saveNameFresh) m_saveName.clear();
            m_saveNameFresh = false;
            if (m_saveName.size() < 80) m_saveName += e.character;
            m_saveError.clear();
        }
        return true;
    }
    return false;
}

} // namespace AestraUI
