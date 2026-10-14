// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "NUITypes.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief The Bring over card (SPEC-017, V81-V1): what another version has, with a check beside each thing that
 * can come across. Pure state, layout and drawing: the panel owns one while the card is open and routes clicks,
 * wheel and keys to it, so the tests drive the real thing.
 */
struct BringOverCard {
    enum class Status { New, Changed, Same, Blocked };

    struct Row {
        std::string name;
        std::string detail;
        Status status = Status::New;
        bool checked = false;
        bool pattern = false;   ///< in the PATTERNS section rather than TRACKS
        uint64_t id = 0;        ///< what the app brings over (a track or pattern id in the other version)
    };

    static constexpr float kWidth = 660.0f;
    static constexpr float kPad = 28.0f;
    static constexpr float kHeaderH = 26.0f;
    static constexpr float kRowH = 44.0f;
    static constexpr float kListH = 250.0f;

    std::string from;           ///< the version being brought from
    std::string into;           ///< the open version
    std::vector<Row> rows;      ///< tracks first, then patterns
    bool canListen = false;     ///< the other version has a bounce to play
    std::string listenPath;     ///< that bounce's file
    float scroll = 0.0f;

    struct Layout {
        AestraUI::NUIRect card, list, bringButton, listenButton, cancelButton;
        struct Entry { int row = -1; bool header = false; std::string title; AestraUI::NUIRect rect; };
        std::vector<Entry> entries;  ///< headers and rows, positioned with the scroll applied
        float contentH = 0.0f;
    };

    int checkedCount() const { return static_cast<int>(std::count_if(rows.begin(), rows.end(), [](const Row& r) { return r.checked; })); }
    static bool selectable(const Row& r) { return r.status != Status::Blocked; }

    void toggle(int row) {
        if (row >= 0 && row < static_cast<int>(rows.size()) && selectable(rows[static_cast<size_t>(row)]))
            rows[static_cast<size_t>(row)].checked = !rows[static_cast<size_t>(row)].checked;
    }

    std::string bringLabel() const {
        const int n = checkedCount();
        return n == 0 ? "Bring over" : "Bring over " + std::to_string(n) + (n == 1 ? " item" : " items");
    }

    Layout layout(const AestraUI::NUIRect& area) const {
        Layout l;
        const bool anyTracks = std::any_of(rows.begin(), rows.end(), [](const Row& r) { return !r.pattern; });
        const bool anyPatterns = std::any_of(rows.begin(), rows.end(), [](const Row& r) { return r.pattern; });
        float y = 0.0f;
        const auto add = [&](int row, bool header, const std::string& title, float h) {
            l.entries.push_back({row, header, title, {0.0f, y, 0.0f, h}});
            y += h;
        };
        if (anyTracks) { add(-1, true, "TRACKS", kHeaderH); for (size_t i = 0; i < rows.size(); ++i) if (!rows[i].pattern) add(static_cast<int>(i), false, "", kRowH); }
        if (anyPatterns) { add(-1, true, "PATTERNS", kHeaderH + 6.0f); for (size_t i = 0; i < rows.size(); ++i) if (rows[i].pattern) add(static_cast<int>(i), false, "", kRowH); }
        l.contentH = y;

        const float h = 148.0f + kListH + 96.0f;
        const float w = std::min(kWidth, area.width - 40.0f);
        l.card = {area.x + (area.width - w) * 0.5f, area.y + std::max(12.0f, (area.height - h) * 0.5f), w, std::min(h, area.height - 24.0f)};
        l.list = {l.card.x + kPad, l.card.y + 96.0f, l.card.width - kPad * 2.0f, kListH};
        const float by = l.card.bottom() - 28.0f - 36.0f;
        float bx = l.card.x + kPad;
        l.bringButton = {bx, by, 172.0f, 36.0f};
        bx += 180.0f;
        if (canListen) { l.listenButton = {bx, by, 104.0f, 36.0f}; bx += 112.0f; }
        l.cancelButton = {bx, by, 86.0f, 36.0f};
        const float maxScroll = std::max(0.0f, y - kListH);
        const float s = std::clamp(scroll, 0.0f, maxScroll);
        for (auto& e : l.entries) { e.rect.x = l.list.x; e.rect.width = l.list.width; e.rect.y += l.list.y - s; }
        return l;
    }

    /// The row whose line is under @p p (inside the visible list), or -1.
    int rowAt(const Layout& l, const AestraUI::NUIPoint& p) const {
        if (!l.list.contains(p)) return -1;
        for (const auto& e : l.entries) if (!e.header && e.rect.contains(p)) return e.row;
        return -1;
    }

    void scrollBy(const AestraUI::NUIRect& area, float pixels) {
        const Layout l = layout(area);
        scroll = std::clamp(scroll + pixels, 0.0f, std::max(0.0f, l.contentH - kListH));
    }

    void draw(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& area, const std::string& hovered) const {
        using namespace AestraUI;
        auto& theme = NUIThemeManager::getInstance();
        const auto& tp = theme.getCurrentTheme();
        const NUIColor text = theme.getColor("textPrimary"), muted = theme.getColor("textSecondary"), rule = theme.getColor("borderSubtle");
        const Layout l = layout(area);
        renderer.fillRect(area, theme.getColor("backgroundPrimary").withAlpha(0.72f));
        renderer.fillRect(l.card, theme.getColor("backgroundSecondary"));
        renderer.strokeRect(l.card, 1.0f, rule);
        renderer.drawText("Bring over", {l.card.x + kPad, l.card.y + 24.0f}, 20.0f, text);
        renderer.drawText("From " + from + " into " + into + ". Nothing merges by itself; you choose what comes across.", {l.card.x + kPad, l.card.y + 56.0f}, 11.5f, muted);

        renderer.setClipRect(l.list);
        for (const auto& e : l.entries) {
            if (e.rect.bottom() < l.list.y || e.rect.y > l.list.bottom()) continue;
            if (e.header) { renderer.drawText(e.title, {e.rect.x, e.rect.y + 8.0f}, 9.5f, muted.withAlpha(0.8f)); continue; }
            const Row& r = rows[static_cast<size_t>(e.row)];
            renderer.fillRect(e.rect, theme.getColor("backgroundPrimary"));
            renderer.fillRect({e.rect.x, e.rect.bottom() - 1.0f, e.rect.width, 1.0f}, rule);
            const NUIRect box{e.rect.x + 14.0f, e.rect.y + (e.rect.height - 18.0f) * 0.5f, 18.0f, 18.0f};
            const bool ok = selectable(r);
            if (r.checked) {
                renderer.fillRect(box, theme.getColor("accentPrimary"));
                renderer.drawLine({box.x + 4.0f, box.y + 9.0f}, {box.x + 8.0f, box.y + 13.0f}, 2.0f, NUIColor::white());
                renderer.drawLine({box.x + 8.0f, box.y + 13.0f}, {box.x + 14.0f, box.y + 5.0f}, 2.0f, NUIColor::white());
            } else {
                renderer.strokeRect(box, 1.5f, ok ? muted : rule);
            }
            renderer.drawText(r.name, {box.right() + 14.0f, std::round(renderer.calculateTextY(e.rect, 13.0f))}, 13.0f, ok ? text : muted.withAlpha(0.6f));
            const NUIColor detail = r.status == Status::Blocked ? theme.getColor("warning") : muted.withAlpha(0.85f);
            const float dw = renderer.measureText(r.detail, 11.0f).width;
            renderer.drawText(r.detail, {e.rect.right() - 14.0f - dw, std::round(renderer.calculateTextY(e.rect, 11.0f))}, 11.0f, detail);
        }
        renderer.clearClipRect();
        renderer.strokeRect(l.list, 1.0f, rule);

        renderer.drawText("Arrives as one step in " + into + ", so a single undo takes it back out. Tracks come in routed to Master.",
                          {l.card.x + kPad, l.bringButton.y - 26.0f}, 10.5f, muted.withAlpha(0.8f));
        const auto button = [&](const NUIRect& b, const std::string& label, bool primary, bool dim, const std::string& id) {
            NUIColor fill = primary ? theme.getColor("accentPrimary") : theme.getColor("surfaceTertiary");
            if (dim) fill = theme.getColor("surfaceTertiary").withAlpha(0.5f);
            else if (hovered == id) fill = fill.lightened(0.08f);
            renderer.fillRect(b, fill);
            const float tw = renderer.measureText(label, 12.5f).width;
            renderer.drawText(label, {b.x + (b.width - tw) * 0.5f, std::round(renderer.calculateTextY(b, 12.5f))}, 12.5f,
                              primary && !dim ? NUIColor::white() : (dim ? muted.withAlpha(0.5f) : text));
        };
        button(l.bringButton, bringLabel(), true, checkedCount() == 0, "bring");
        if (canListen) button(l.listenButton, "Listen first", false, false, "listen");
        button(l.cancelButton, "Cancel", false, false, "cancel");
        (void)tp;
    }
};

} // namespace Aestra
