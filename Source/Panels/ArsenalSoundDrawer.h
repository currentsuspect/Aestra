// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUIRenderer.h"
#include "NUITypes.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

namespace Aestra {

/**
 * @brief The sound drawer under an open sampler row's sound strip (V81-AR1, FD-27 point 4): the sounds beside the
 * current one as a searchable list. Arrow keys and clicks swap the row's sound while the pattern loops (the swap is
 * gap-free, V81-E3), Enter keeps the one you are on, Esc puts back the one you started with.
 *
 * The rules are pure: filtering, selection, the visible window, and what a key does. Drawing and hit-testing take
 * the same state, so the tests drive the real thing.
 */
class ArsenalSoundDrawer {
public:
    static constexpr float kSearchH = 28.0f;
    static constexpr float kRowH = 22.0f;
    static constexpr float kPad = 8.0f;
    static constexpr int kVisibleRows = 6;
    /// Fixed, so typing never changes the rack's height.
    static constexpr float kHeight = kSearchH + kRowH * kVisibleRows + kPad;

    struct State {
        std::string filter;    ///< What was typed; matches any part of a file name, case-insensitively, every word.
        std::string original;  ///< The sound the row had when the drawer opened (Esc returns to it).
        int selected = -1;     ///< Index into the filtered list, -1 when nothing matches.
        int scroll = 0;        ///< Index of the first visible filtered row.
    };

    enum class Result { Ignored, Consumed, Swap, Keep, Revert };
    struct Outcome {
        Result result = Result::Ignored;
        std::string path;  ///< The sound to swap to, or to keep, or to revert to.
    };

    static std::string nameOf(const std::string& path) { return std::filesystem::path(path).stem().string(); }

    static std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    /// Indices into @p list whose file name contains every word of @p filter. An empty filter keeps everything.
    static std::vector<int> filtered(const std::vector<std::string>& list, const std::string& filter) {
        std::vector<std::string> words;
        std::string word;
        for (char c : lower(filter)) {
            if (c == ' ') { if (!word.empty()) words.push_back(word), word.clear(); }
            else word.push_back(c);
        }
        if (!word.empty()) words.push_back(word);
        std::vector<int> out;
        for (size_t i = 0; i < list.size(); ++i) {
            const std::string name = lower(nameOf(list[i]));
            const bool all = std::all_of(words.begin(), words.end(), [&](const std::string& w) { return name.find(w) != std::string::npos; });
            if (all) out.push_back(static_cast<int>(i));
        }
        return out;
    }

    /// Open on @p current: nothing typed, the current sound selected and in view.
    static State open(const std::vector<std::string>& list, const std::string& current) {
        State s;
        s.original = current;
        s.selected = -1;
        const auto idx = filtered(list, "");
        for (size_t i = 0; i < idx.size(); ++i)
            if (list[static_cast<size_t>(idx[i])] == current) s.selected = static_cast<int>(i);
        reveal(s, idx.size());
        return s;
    }

    /// Scroll just enough that the selected row is inside the window.
    static void reveal(State& s, size_t count) {
        if (s.selected < 0) { s.scroll = 0; return; }
        if (s.selected < s.scroll) s.scroll = s.selected;
        if (s.selected >= s.scroll + kVisibleRows) s.scroll = s.selected - kVisibleRows + 1;
        s.scroll = std::clamp(s.scroll, 0, std::max(0, static_cast<int>(count) - kVisibleRows));
    }

    static void scrollBy(State& s, size_t count, int rows) {
        s.scroll = std::clamp(s.scroll + rows, 0, std::max(0, static_cast<int>(count) - kVisibleRows));
    }

    /// What a key press does. Typing edits the filter; Up and Down move through the matches and swap to each;
    /// Return keeps the selected sound; Escape returns to the one the drawer opened on.
    static Outcome onKey(State& s, const std::vector<std::string>& list, const AestraUI::NUIKeyEvent& e) {
        using AestraUI::NUIKeyCode;
        if (!e.pressed) return {};
        if (e.modifiers & AestraUI::NUIModifiers::Ctrl) return {}; // shortcuts stay shortcuts
        auto idx = filtered(list, s.filter);
        const auto pathAt = [&](int i) { return (i >= 0 && i < static_cast<int>(idx.size())) ? list[static_cast<size_t>(idx[static_cast<size_t>(i)])] : std::string(); };

        if (e.keyCode == NUIKeyCode::Escape) return {Result::Revert, s.original};
        if (e.keyCode == NUIKeyCode::Enter) return {Result::Keep, pathAt(s.selected)};
        if (e.keyCode == NUIKeyCode::Down || e.keyCode == NUIKeyCode::Up) {
            if (idx.empty()) return {Result::Consumed, {}};
            const int step = e.keyCode == NUIKeyCode::Down ? 1 : -1;
            s.selected = s.selected < 0 ? (step > 0 ? 0 : static_cast<int>(idx.size()) - 1)
                                        : std::clamp(s.selected + step, 0, static_cast<int>(idx.size()) - 1);
            reveal(s, idx.size());
            return {Result::Swap, pathAt(s.selected)};
        }
        if (e.keyCode == NUIKeyCode::Backspace) {
            if (!s.filter.empty()) s.filter.pop_back();
        } else if (e.character >= 32 && e.character < 127) {
            s.filter.push_back(e.character);
        } else {
            return {Result::Consumed, {}}; // the drawer owns the keyboard while it is open
        }
        idx = filtered(list, s.filter);
        s.selected = idx.empty() ? -1 : 0;
        s.scroll = 0;
        return {Result::Consumed, {}};
    }

    /// Row under @p p in a drawer drawn into @p r: an index into the filtered list, or -1.
    static int rowAt(const State& s, size_t count, const AestraUI::NUIRect& r, const AestraUI::NUIPoint& p) {
        if (!r.contains(p) || p.y < r.y + kSearchH) return -1;
        const int row = s.scroll + static_cast<int>((p.y - r.y - kSearchH) / kRowH);
        return (row >= 0 && row < static_cast<int>(count) && row < s.scroll + kVisibleRows) ? row : -1;
    }

    /// The set chip at the right of the search line: "Folder" or the set's name; a click moves to the next set.
    static AestraUI::NUIRect chipRect(const AestraUI::NUIRect& r) { return {r.right() - 12.0f - 112.0f, r.y + 4.0f, 112.0f, kSearchH - 8.0f}; }

    static void draw(AestraUI::NUIRenderer& renderer, const AestraUI::NUIRect& r, const State& s,
                     const std::vector<std::string>& list, const std::string& current, const AestraUI::NUIColor& accent,
                     const std::string& setLabel = {}, size_t missing = 0) {
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const auto& tp = theme.getCurrentTheme();
        renderer.fillRect(r, theme.getColor("backgroundPrimary"));

        // Search line: what was typed, or what you can type; how many sounds match at the right.
        const auto idx = filtered(list, s.filter);
        const AestraUI::NUIRect search{r.x, r.y, r.width, kSearchH};
        renderer.fillRect({r.x, search.bottom() - 1.0f, r.width, 1.0f}, theme.getColor("borderSubtle"));
        const std::string shown = s.filter.empty() ? (setLabel.empty() ? "Type to search this folder" : "Type to search this set") : s.filter + "|";
        renderer.drawText(shown, {r.x + 12.0f, std::round(renderer.calculateTextY(search, tp.fontSizeS))}, tp.fontSizeS,
                          s.filter.empty() ? theme.getColor("textSecondary").withAlpha(0.6f) : theme.getColor("textPrimary"));
        const auto chip = chipRect(r);
        renderer.fillRect(chip, theme.getColor("surfaceTertiary"));
        const std::string label = setLabel.empty() ? "Folder" : setLabel;
        renderer.setClipRect({chip.x, chip.y, chip.width - 16.0f, chip.height});
        renderer.drawText(label, {chip.x + 8.0f, std::round(renderer.calculateTextY(chip, tp.fontSizeXS))}, tp.fontSizeXS,
                          setLabel.empty() ? theme.getColor("textSecondary") : theme.getColor("textPrimary"));
        renderer.clearClipRect();
        renderer.drawLine({chip.right() - 13.0f, chip.y + chip.height * 0.5f - 1.5f}, {chip.right() - 10.0f, chip.y + chip.height * 0.5f + 1.5f}, 1.2f, theme.getColor("textSecondary"));
        renderer.drawLine({chip.right() - 10.0f, chip.y + chip.height * 0.5f + 1.5f}, {chip.right() - 7.0f, chip.y + chip.height * 0.5f - 1.5f}, 1.2f, theme.getColor("textSecondary"));
        std::string count = std::to_string(idx.size()) + (idx.size() == 1 ? " sound" : " sounds");
        float right = chip.x - 12.0f;
        if (missing > 0) { // a member that is gone is said so, in amber
            const std::string gone = std::to_string(missing) + " missing";
            const float gw = renderer.measureText(gone, tp.fontSizeXS).width;
            renderer.drawText(gone, {right - gw, std::round(renderer.calculateTextY(search, tp.fontSizeXS))}, tp.fontSizeXS, theme.getColor("warning"));
            right -= gw + 12.0f;
        }
        const float cw = renderer.measureText(count, tp.fontSizeXS).width;
        renderer.drawText(count, {right - cw, std::round(renderer.calculateTextY(search, tp.fontSizeXS))}, tp.fontSizeXS,
                          theme.getColor("textSecondary").withAlpha(0.8f));

        if (idx.empty()) {
            renderer.drawTextCentered("No match", {r.x, search.bottom(), r.width, kRowH * kVisibleRows}, tp.fontSizeS,
                                      theme.getColor("textSecondary").withAlpha(0.6f));
            return;
        }
        renderer.setClipRect({r.x, search.bottom(), r.width, kRowH * kVisibleRows});
        for (int v = 0; v < kVisibleRows; ++v) {
            const int row = s.scroll + v;
            if (row >= static_cast<int>(idx.size())) break;
            const std::string& path = list[static_cast<size_t>(idx[static_cast<size_t>(row)])];
            const AestraUI::NUIRect rr{r.x, search.bottom() + v * kRowH, r.width - 8.0f, kRowH};
            if (row == s.selected) renderer.fillRect(rr, theme.getColor("surfaceRaised"));
            if (path == current) renderer.fillRect({rr.x, rr.y + 4.0f, 3.0f, rr.height - 8.0f}, accent); // the sound it has now
            renderer.drawText(nameOf(path), {rr.x + 14.0f, std::round(renderer.calculateTextY(rr, tp.fontSizeS))}, tp.fontSizeS,
                              row == s.selected ? theme.getColor("textPrimary") : theme.getColor("textPrimary").withAlpha(0.78f));
        }
        renderer.clearClipRect();

        // A thin scroll thumb when there are more matches than rows.
        if (static_cast<int>(idx.size()) > kVisibleRows) {
            const float track = kRowH * kVisibleRows;
            const float thumbH = std::max(16.0f, track * kVisibleRows / static_cast<float>(idx.size()));
            const float thumbY = search.bottom() + (track - thumbH) * s.scroll / std::max(1.0f, static_cast<float>(idx.size() - kVisibleRows));
            renderer.fillRect({r.right() - 5.0f, thumbY, 2.0f, thumbH}, theme.getColor("borderSubtle").withAlpha(0.9f));
        }
    }
};

} // namespace Aestra
