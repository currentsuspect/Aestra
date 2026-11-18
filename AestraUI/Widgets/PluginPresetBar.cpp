// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "PluginPresetBar.h"
#include "../Graphics/NUIDepth.h"

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "PluginEditorKit.h"

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
// Design Spec, Menu (locked): one popup card, 10px round; rows inset 5px and rounded 6px.
constexpr float kCardRadius = 10.0f;
constexpr float kCardInset = 5.0f;
constexpr float kRowRadius = 6.0f;
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

// Design Spec, Plugin header (locked): the preset picker is one pill, "‹ name ⌄ ›", raised at rest and sunk while
// its fold is open. Hover lightens the part under the pointer.
void pickerPill(NUIRenderer& r, const NUIRect& prev, const NUIRect& name, const NUIRect& next, bool open, int hover) {
    const NUIRect pill{prev.x, prev.y, next.right() - prev.x, prev.height};
    const float rad = Depth::Radius::pill(pill.height);
    if (open)
        Depth::bed(r, pill, rad, Depth::Cut::Deep);
    else
        Depth::object(r, pill, rad);
    const NUIRect* parts[] = {&prev, &name, &next};
    if (!open && hover >= 0 && hover <= 2)
        r.fillRoundedRect(*parts[hover], rad, NUIThemeManager::getInstance().getColor("hover"));
}

// Draws @p text with the first case-insensitive match of @p query lit (Design Spec, Menu: matched letters in
// `secondary`).
void drawMatched(NUIRenderer& r, const std::string& text, const NUIPoint& at, float size, const NUIColor& ink,
                 const NUIColor& lit, const std::string& query) {
    const size_t pos = query.empty() ? std::string::npos : lower(text).find(lower(query));
    if (pos == std::string::npos) {
        r.drawText(text, at, size, ink);
        return;
    }
    const std::string head = text.substr(0, pos), hit = text.substr(pos, query.size()), tail = text.substr(pos + hit.size());
    float x = at.x;
    r.drawText(head, {x, at.y}, size, ink);
    x += r.measureText(head, size).width;
    r.drawText(hit, {x, at.y}, size, lit);
    x += r.measureText(hit, size).width;
    r.drawText(tail, {x, at.y}, size, ink);
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
    if (m_twoRow) {
        // FD-73 row two, left to right: ‹ preset › Save A|B ···, and the power button at the far right.
        const float y = titleBar.y + 4.0f;
        const float x = titleBar.x + 8.0f;
        Layout l;
        l.prev = {x, y, kArrowW, kBarH};
        l.name = {x + 28.0f, y, 180.0f, kBarH};
        l.next = {x + 212.0f, y, kArrowW, kBarH};
        l.save = {x + 244.0f, y, kSaveW, kBarH};
        l.ab = {x + 300.0f, y, 56.0f, kBarH};
        l.a = {l.ab.x, y, 28.0f, kBarH};
        l.b = {l.ab.x + 28.0f, y, 28.0f, kBarH};
        l.more = {x + 364.0f, y, kArrowW, kBarH};
        l.power = {titleBar.right() - 32.0f, y, kBarH, kBarH};
        return l;
    }
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
    case 6: return m_layout.more;
    case 7: return m_layout.power;
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
    if (m_twoRow) {
        renderTwoRow(r, titleBar);
        return;
    }
    const Colors c = colors();
    const Layout& l = m_layout;

    pickerPill(r, l.prev, l.name, l.next, m_popover == Popover::Browse, m_hoverControl);
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
    // Save: a soft pill that sinks while its card is open (Design Spec, Button); no outline.
    if (m_popover == Popover::Save)
        Depth::bed(r, l.save, Depth::Radius::pill(l.save.height), Depth::Cut::Deep);
    else
        EditorKit::drawPushButton(r, l.save, std::string(), m_hoverControl == 3);
    r.drawTextCentered("Save", l.save, 11.5f, c.text);

    // A | B: the Segment switch, as in the two-row header. A reads as chosen until a comparison starts.
    const bool cmp = m_session->comparing();
    const int slot = m_session->activeSlot();
    static const std::string kAB[2] = {"A", "B"};
    EditorKit::drawSegmented(r, l.ab, kAB, 2, (!cmp || slot == 0) ? 0 : 1, -1, false);
}

// The FD-73 look: flat, sharp, one edge colour, painted by the shared control kit's colours.
void PluginPresetBar::renderTwoRow(NUIRenderer& r, const NUIRect&) {
    const auto k = EditorKit::emberColors();
    const Layout& l = m_layout;
    const bool bypassed = m_actions.isBypassed && m_actions.isBypassed();

    // Save and ··· are soft pills (Design Spec, Button); Save sinks while its card is open, like the picker.
    auto button = [&](const NUIRect& rc, bool hover, bool open) {
        if (open)
            Depth::bed(r, rc, Depth::Radius::pill(rc.height), Depth::Cut::Deep);
        else
            EditorKit::drawPushButton(r, rc, std::string(), hover);
    };
    pickerPill(r, l.prev, l.name, l.next, m_popover == Popover::Browse, m_hoverControl);
    auto arrow = [&](const NUIRect& rc, bool left, bool hover) {
        const float cx = rc.x + rc.width * 0.5f, cy = rc.y + rc.height * 0.5f;
        const float dir = left ? 1.0f : -1.0f;
        const NUIColor ink = hover ? k.ink : k.inkSoft;
        r.drawLine({cx + dir * 2.0f, cy - 5.0f}, {cx - dir * 2.0f, cy}, 1.5f, ink);
        r.drawLine({cx - dir * 2.0f, cy}, {cx + dir * 2.0f, cy + 5.0f}, 1.5f, ink);
    };
    arrow(l.prev, true, m_hoverControl == 0);
    arrow(l.next, false, m_hoverControl == 2);

    // Preset name: a dropdown with the edit count beside the name.
    const auto& current = m_session->current();
    const int changes = m_session->changes();
    const std::string name = current ? current->name : "No preset";
    const float cy = l.name.y + kBarH * 0.5f;
    float tx = l.name.x + 8.0f;
    const float nameRoom = l.name.width - 30.0f - (changes > 0 ? 96.0f : 0.0f);
    const std::string shown = fit(r, name, 12.0f, nameRoom);
    EditorKit::drawTextLeft(r, shown, tx, cy, 12.0f, current ? k.ink : k.inkSoft);
    if (changes > 0) {
        tx += r.measureText(shown, 12.0f).width + 8.0f;
        r.fillRect({tx, cy - 3.0f, 6.0f, 6.0f}, k.warn);
        EditorKit::drawTextLeft(r, changes == 1 ? "1 change" : std::to_string(changes) + " changes", tx + 10.0f, cy,
                                11.0f, k.inkMuted);
    }
    const float vx = l.name.right() - 12.0f;
    r.drawLine({vx - 4.0f, cy - 2.0f}, {vx, cy + 2.0f}, 1.5f, k.inkSoft);
    r.drawLine({vx, cy + 2.0f}, {vx + 4.0f, cy - 2.0f}, 1.5f, k.inkSoft);

    button(l.save, m_hoverControl == 3, m_popover == Popover::Save);
    r.drawTextCentered("Save", l.save, 12.0f, m_hoverControl == 3 ? k.ink : k.inkSoft);

    // A | B: the 24px Segment switch with letters (Design Spec, Plugin header). A is the sound now until a
    // comparison starts, so it reads as the chosen side.
    const bool cmp = m_session->comparing();
    const int slot = m_session->activeSlot();
    static const std::string kAB[2] = {"A", "B"};
    EditorKit::drawSegmented(r, l.ab, kAB, 2, (!cmp || slot == 0) ? 0 : 1, -1, false);

    // ···
    button(l.more, m_hoverControl == 6, false);
    for (float dx : {7.0f, 12.0f, 17.0f})
        r.fillRect({l.more.x + dx - 1.0f, l.more.y + kBarH * 0.5f - 1.0f, 2.0f, 2.0f}, m_hoverControl == 6 ? k.ink : k.inkSoft);

    // Power = bypass. Lit while the plugin is active.
    if (m_actions.setBypassed) {
        button(l.power, false, false);
        r.fillRect({l.power.x + 1.0f, l.power.y + 1.0f, l.power.width - 2.0f, l.power.height - 2.0f},
                   (bypassed || m_hoverControl == 7) ? k.raised : k.control);
        const NUIPoint c{l.power.x + l.power.width * 0.5f, l.power.y + l.power.height * 0.5f + 1.0f};
        const NUIColor glyph = bypassed ? k.inkMuted : k.secondary;
        EditorKit::drawArc(r, c, 5.5f, -EditorKit::kPi * 0.5f + 0.7f, EditorKit::kPi * 1.5f - 0.7f, 1.6f, glyph);
        r.drawLine({c.x, c.y - 8.5f}, {c.x, c.y - 2.5f}, 1.6f, glyph);
        if (bypassed) {
            const float w = r.measureText("BYPASSED", 10.0f).width + 28.0f;
            const NUIRect pill{l.power.x - 8.0f - w, l.power.y, w, kBarH};
            r.fillRect(pill, k.raised);
            EditorKit::drawCaps(r, "Bypassed", pill.x + 8.0f, pill.y + kBarH * 0.5f, 10.0f, k.inkSoft);
        }
    }
}

void PluginPresetBar::renderOverlay(NUIRenderer& r, const NUIRect& window) {
    m_window = window;
    if (m_popover == Popover::None) return;
    const Colors c = colors();

    if (m_popover == Popover::Browse) {
        const NUIRect p = browseRect(window);
        m_popoverRect = p;
        // Design Spec, Menu: the fold is the one popup card, with a sunk pill search well at the top.
        Depth::floatingCard(r, p, kCardRadius, NUIThemeManager::getInstance().getColor("surfaceTertiary"));

        // search
        const NUIRect search{p.x + kPad, p.y + kPad, p.width - 2.0f * kPad, kSearchH};
        Depth::bed(r, search, Depth::Radius::pill(search.height), Depth::Cut::Shallow);
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
            if (on) r.fillRoundedRect(chip, kChipH * 0.5f, NUIThemeManager::getInstance().getColor("selected"));
            r.drawTextCentered(kFilterNames[i], chip, 11.0f, on ? c.text : c.text2);
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
            const NUIRect hit = browseRowRect(p, i);
            const NUIRect row{hit.x + kCardInset, hit.y, hit.width - kCardInset * 2.0f, hit.height}; // never edge to edge
            const bool isHeard = heard && *heard == idx;
            const auto& cur = m_session->current();
            const bool wasCurrent = !heard && cur && cur->name == pr.name && cur->source == pr.source;
            // The one being heard lays `selected`; hover and the one you came from are quiet.
            auto& tm = NUIThemeManager::getInstance();
            if (isHeard)
                r.fillRoundedRect(row, kRowRadius, tm.getColor("selected"));
            else if (wasCurrent || (m_hoverRow && *m_hoverRow == idx))
                r.fillRoundedRect(row, kRowRadius, tm.getColor("hover"));
            const float by = r.calculateTextY(row, 12.0f);
            if (isHeard) {
                // a small speaker: this is the one on the track now
                const float sx = row.x + 12.0f, cy = row.y + row.height * 0.5f;
                r.fillRect({sx, cy - 2.0f, 3.0f, 4.0f}, c.accent);
                r.drawLine({sx + 3.0f, cy - 2.0f}, {sx + 6.0f, cy - 5.0f}, 1.3f, c.accent);
                r.drawLine({sx + 3.0f, cy + 2.0f}, {sx + 6.0f, cy + 5.0f}, 1.3f, c.accent);
                r.drawLine({sx + 6.0f, cy - 5.0f}, {sx + 6.0f, cy + 5.0f}, 1.3f, c.accent);
            }
            drawMatched(r, fit(r, pr.name, 12.0f, 150.0f), {row.x + 28.0f, by}, 12.0f, c.text,
                        NUIThemeManager::getInstance().getColor("secondary"), m_search);
            const std::string desc = !pr.description.empty() ? pr.description : pr.category;
            if (!desc.empty())
                r.drawText(fit(r, desc, 11.0f, p.width - 28.0f - 160.0f - 90.0f), {row.x + 188.0f, by}, 11.0f, c.text2);
            const char* src = presetSourceName(pr.source);
            r.drawText(src, {row.right() - 14.0f - r.measureText(src, 10.5f).width, by}, 10.5f, c.text2);
        }

        // footer
        const NUIRect foot{p.x, p.bottom() - kFooterH, p.width, kFooterH};
        r.drawLine({foot.x + kPad, foot.y}, {foot.right() - kPad, foot.y}, 1.0f,
                   NUIThemeManager::getInstance().getColor("borderSubtle")); // a separator inset from the sides
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
    Depth::floatingCard(r, p, kCardRadius, NUIThemeManager::getInstance().getColor("surfaceTertiary"));
    r.drawText("NAME", {p.x + kPad, p.y + kPad + 4.0f}, 10.0f, c.text2);
    const NUIRect field{p.x + kPad, p.y + kPad + 20.0f, p.width - 2.0f * kPad, 30.0f};
    Depth::bed(r, field, Depth::Radius::pill(field.height), Depth::Cut::Shallow); // a Field: sunk, no outline
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
    const NUIRect frame{l.prev.x, l.prev.y, (m_twoRow ? l.more.right() : l.ab.right()) - l.prev.x, kBarH};
    const bool onPower = m_twoRow && m_actions.setBypassed && l.power.contains(e.position);
    if (!frame.contains(e.position) && !onPower) {
        if (m_hoverControl != -1) m_hoverControl = -1;
        return false;
    }
    m_hoverControl = l.prev.contains(e.position)   ? 0
                     : l.name.contains(e.position) ? 1
                     : l.next.contains(e.position) ? 2
                     : l.save.contains(e.position) ? 3
                     : l.ab.contains(e.position)   ? 4
                     : (m_twoRow && l.more.contains(e.position)) ? 6
                     : onPower ? 7
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
        case 6:
            if (m_actions.openMenu) m_actions.openMenu({l.more.x, l.more.bottom() + 2.0f});
            break;
        case 7:
            if (m_actions.setBypassed)
                m_actions.setBypassed(!(m_actions.isBypassed && m_actions.isBypassed()));
            break;
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
