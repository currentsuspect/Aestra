// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "NUILayoutSpace.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace AestraUI {
namespace Layout {

/**
 * @file NUILayoutExplain.h
 * @brief V8-X2b criterion 4: "debug tooling can explain exactly why a widget
 * ended up where it did", for surfaces laid out by the stateless algorithms.
 *
 * NUILayoutNode records a trace in arrange(), but every migrated surface
 * (WindowPanel's title bar, the mixer, the timeline) uses the free functions in
 * NUILayoutAlgorithms.h, which recorded nothing. Here each algorithm, when handed
 * a recorder, writes one step per rect it resolves: the rule it applied in words
 * and numbers, the space it was given and where that space came from, what was
 * asked for, what was given, and any clamp. The algorithm writes the rule itself,
 * at the moment it decides, so the answer is read off data rather than narrated
 * by the caller afterwards.
 *
 * Off by default and free when off: a surface asks layoutRecorderFor("timeline")
 * and gets nullptr unless AESTRA_LAYOUT_TRACE names it, and every algorithm takes
 * the recorder as a defaulted nullptr. SPEC-003 §10 is the reference:
 *
 *   AESTRA_LAYOUT_TRACE=all | timeline,mixer      record these surfaces and print
 *                                                each pass that changed something
 *   AESTRA_LAYOUT_EXPLAIN=timeline.rows[3]        also print the full "why" for any
 *                                                step whose name contains this text
 *
 * UI thread only, like layout itself. Header-only and dependency-free (standard
 * library only), so tests run under AESTRA_CI=ON instead of being skipped.
 */

/** @brief One resolved rect and the reason for it. All rects are in the surface's Local space. */
struct NUILayoutStep {
    std::string name;      //!< scope + suffix, e.g. "timeline.rows[3]", "titlebar.buttons[0]"
    std::string from;      //!< the step whose rect was this step's `available`, or "" for the surface itself
    std::string rule;      //!< the algorithm's own account, with the numbers it used
    NUILocalRect available;
    bool requestsWidth = false;  //!< false: takes whatever `available` gives on that axis ("fill")
    bool requestsHeight = false;
    float requestedWidth = 0.0f;
    float requestedHeight = 0.0f;
    NUILocalRect resolved;
    std::string clamp;           //!< empty when nothing was clamped; otherwise what and why
    bool overflowExpected = false; //!< scrolled content may lie outside `available`; a fixed row may not
    bool visible = true;           //!< inside `available` (scrolling stacks report this)
};

/** @brief How a step compares with the same-named step in the previous pass. */
enum class NUILayoutChange { Added, Changed, Unchanged, Removed };

/** @brief A §10.1 condition, with the step that raised it. */
struct NUILayoutProblem {
    std::string step;
    std::string what;
};

class NUILayoutRecorder {
public:
    explicit NUILayoutRecorder(std::string surface) : surface_(std::move(surface)) {}

    const std::string& surface() const { return surface_; }

    /**
     * Starts a pass. `originInWindow` is the surface's own Local origin in Window
     * space, so an explanation can quote both spaces without the reader doing the
     * conversion the contract exists to make explicit.
     */
    void beginPass(const NUIWindowPoint& originInWindow) {
        previous_.clear();
        for (const auto& s : steps_) {
            previous_[s.name] = s.resolved;
        }
        steps_.clear();
        origin_ = originInWindow;
        scope_.clear();
        scopeFrom_.clear();
        ++pass_;
    }

    /**
     * Names what the next algorithm call produces. `from` is the step whose
     * resolved rect is being passed in as that call's container — the answer to
     * "which constraint reached it, from which ancestor".
     */
    void scope(std::string name, std::string from = std::string()) {
        scope_ = std::move(name);
        scopeFrom_ = std::move(from);
    }

    /** Algorithms call this; `suffix` is appended to the current scope (".leading", "[2]"). */
    void add(NUILayoutStep step, const std::string& suffix) {
        step.name = qualified(suffix);
        step.from = scopeFrom_.empty() ? std::string() : qualified(scopeFrom_, true);
        steps_.push_back(std::move(step));
    }

    unsigned passNumber() const { return pass_; }
    const NUIWindowPoint& originInWindow() const { return origin_; }
    const std::vector<NUILayoutStep>& steps() const { return steps_; }

    const NUILayoutStep* find(const std::string& name) const {
        for (const auto& s : steps_) {
            if (s.name == name) {
                return &s;
            }
        }
        return nullptr;
    }

    /** Per-step comparison with the previous pass, in this pass's order, then removals. */
    std::vector<std::pair<std::string, NUILayoutChange>> changes() const {
        std::vector<std::pair<std::string, NUILayoutChange>> out;
        std::map<std::string, bool> seen;
        for (const auto& s : steps_) {
            seen[s.name] = true;
            const auto it = previous_.find(s.name);
            if (it == previous_.end()) {
                out.emplace_back(s.name, NUILayoutChange::Added);
            } else {
                out.emplace_back(s.name, sameRect(it->second, s.resolved) ? NUILayoutChange::Unchanged
                                                                          : NUILayoutChange::Changed);
            }
        }
        for (const auto& p : previous_) {
            if (!seen.count(p.first)) {
                out.emplace_back(p.first, NUILayoutChange::Removed);
            }
        }
        return out;
    }

    /**
     * §10.1, the conditions this surface can detect on its own: non-finite
     * geometry, negative sizes, a fixed (non-scrolling) rect outside the space it
     * was given, and a clamp that took away part of what was asked for.
     */
    std::vector<NUILayoutProblem> problems() const {
        std::vector<NUILayoutProblem> out;
        for (const auto& s : steps_) {
            const auto& r = s.resolved;
            if (!std::isfinite(r.x) || !std::isfinite(r.y) || !std::isfinite(r.width) || !std::isfinite(r.height)) {
                out.push_back({s.name, "non-finite geometry"});
                continue;
            }
            if (r.width < 0.0f || r.height < 0.0f) {
                out.push_back({s.name, "negative size " + size(r.width, r.height)});
            }
            if (!s.overflowExpected && !inside(r, s.available)) {
                out.push_back({s.name, "outside the space it was given: " + rect(r) + " not within " +
                                           rect(s.available)});
            }
            if (!s.clamp.empty()) {
                out.push_back({s.name, "clamped: " + s.clamp});
            }
        }
        return out;
    }

    /** SPEC-003 §10.2's "WHY did this widget end up here?", for one step, with the chain of spaces above it. */
    std::string explain(const std::string& name) const {
        const NUILayoutStep* s = find(name);
        if (!s) {
            return "No step named " + name + " in " + surface_ + " pass #" + std::to_string(pass_) + ".\n";
        }
        std::string o = "WHY did " + s->name + " end up here?   (" + surface_ + ", pass #" + std::to_string(pass_) +
                        ")\n";
        o += "  Rule:      " + s->rule + "\n";
        o += "  Available: " + rect(s->available) + "  from " + (s->from.empty() ? "the surface's own bounds" : s->from) +
             "\n";
        o += "  Requested: " + requested(*s) + "\n";
        o += "  Resolved:  " + rect(s->resolved) + "  (Local)   = " + rect(toWindow(s->resolved)) + "  (Window)\n";
        const float dw = s->requestsWidth ? s->resolved.width - s->requestedWidth : 0.0f;
        const float dh = s->requestsHeight ? s->resolved.height - s->requestedHeight : 0.0f;
        o += "  Delta:     " + (dw == 0.0f && dh == 0.0f ? std::string("none") : size(dw, dh)) + "\n";
        o += "  Clamped:   " + (s->clamp.empty() ? std::string("no") : s->clamp) + "\n";
        o += std::string("  Visible:   ") + (s->visible ? "yes" : "no, outside the space it was given") +
             (s->overflowExpected ? " (scrolled content)" : "") + "\n";
        // Walk the chain of containers so "where did 668 come from" has an answer too.
        std::string up = s->from;
        int guard = 0;
        while (!up.empty() && guard++ < 16) {
            const NUILayoutStep* p = find(up);
            if (!p) {
                break;
            }
            o += "  ↑ " + p->name + ": " + rect(p->resolved) + " — " + p->rule + "\n";
            up = p->from;
        }
        return o;
    }

    /** SPEC-003 §10.2's per-pass account: what moved, what didn't, and any §10.1 problems. */
    std::string passSummary(std::size_t maxNames = 12) const {
        unsigned added = 0, changed = 0, unchanged = 0, removed = 0;
        std::string names;
        std::size_t listed = 0;
        for (const auto& c : changes()) {
            switch (c.second) {
            case NUILayoutChange::Added: ++added; break;
            case NUILayoutChange::Changed: ++changed; break;
            case NUILayoutChange::Unchanged: ++unchanged; break;
            case NUILayoutChange::Removed: ++removed; break;
            }
            if (c.second != NUILayoutChange::Unchanged && listed < maxNames) {
                names += "\n    " + c.first + "  " + label(c.second);
                ++listed;
            }
        }
        std::string o = "Layout pass #" + std::to_string(pass_) + " " + surface_ + ": " + std::to_string(changed) +
                        " changed, " + std::to_string(added) + " added, " + std::to_string(removed) + " removed, " +
                        std::to_string(unchanged) + " unchanged" + names;
        for (const auto& p : problems()) {
            o += "\n  ! " + p.step + ": " + p.what;
        }
        return o + "\n";
    }

    /** True when the last pass moved, added or removed anything. */
    bool passChangedAnything() const {
        for (const auto& c : changes()) {
            if (c.second != NUILayoutChange::Unchanged) {
                return true;
            }
        }
        return false;
    }

    static std::string rect(const NUILocalRect& r) {
        return num(r.x) + "," + num(r.y) + " " + size(r.width, r.height);
    }
    static std::string rect(const NUIWindowRect& r) {
        return num(r.x) + "," + num(r.y) + " " + size(r.width, r.height);
    }
    static std::string num(float v) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", static_cast<double>(v));
        return buf;
    }

private:
    static bool sameRect(const NUILocalRect& a, const NUILocalRect& b) {
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    }
    static bool inside(const NUILocalRect& r, const NUILocalRect& c) {
        constexpr float kEps = 0.01f;
        return r.x >= c.x - kEps && r.y >= c.y - kEps && r.right() <= c.right() + kEps &&
               r.bottom() <= c.bottom() + kEps;
    }
    static std::string size(float w, float h) { return num(w) + "×" + num(h); }
    static std::string requested(const NUILayoutStep& s) {
        return (s.requestsWidth ? num(s.requestedWidth) : std::string("fill")) + " × " +
               (s.requestsHeight ? num(s.requestedHeight) : std::string("fill"));
    }
    static const char* label(NUILayoutChange c) {
        switch (c) {
        case NUILayoutChange::Added: return "added";
        case NUILayoutChange::Changed: return "changed";
        case NUILayoutChange::Removed: return "removed";
        case NUILayoutChange::Unchanged: return "unchanged";
        }
        return "";
    }
    NUIWindowRect toWindow(const NUILocalRect& r) const { return localToWindow(r, origin_); }

    std::string qualified(const std::string& suffix, bool isFullScopeName = false) const {
        if (isFullScopeName) {
            return surface_ + "." + suffix;
        }
        return surface_ + "." + scope_ + suffix;
    }

    std::string surface_;
    std::string scope_;
    std::string scopeFrom_;
    std::vector<NUILayoutStep> steps_;
    std::map<std::string, NUILocalRect> previous_;
    NUIWindowPoint origin_;
    unsigned pass_ = 0;
};

/** @brief Whether AESTRA_LAYOUT_TRACE names `surface` ("all", or a comma-separated list). */
inline bool layoutTraceEnabledFor(const char* envValue, const std::string& surface) {
    if (!envValue || !*envValue) {
        return false;
    }
    const std::string list = envValue;
    if (list == "all" || list == "1") {
        return true;
    }
    std::size_t start = 0;
    while (start <= list.size()) {
        const std::size_t comma = list.find(',', start);
        const std::string item = list.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
        if (item == surface) {
            return true;
        }
        if (comma == std::string::npos) {
            break;
        }
        start = comma + 1;
    }
    return false;
}

/**
 * @brief The recorder for `surface`, or nullptr when tracing is off for it.
 * One recorder per surface for the life of the process, so pass-to-pass changes
 * accumulate.
 */
inline NUILayoutRecorder* layoutRecorderFor(const std::string& surface) {
    static std::map<std::string, std::unique_ptr<NUILayoutRecorder>> recorders;
    static const char* env = std::getenv("AESTRA_LAYOUT_TRACE");
    if (!layoutTraceEnabledFor(env, surface)) {
        return nullptr;
    }
    auto& slot = recorders[surface];
    if (!slot) {
        slot = std::make_unique<NUILayoutRecorder>(surface);
    }
    return slot.get();
}

/** @brief layoutRecorderFor() plus beginPass(): the recorder for a pass starting now, or nullptr. */
inline NUILayoutRecorder* beginLayoutPass(const std::string& surface, const NUIWindowPoint& originInWindow) {
    NUILayoutRecorder* recorder = layoutRecorderFor(surface);
    if (recorder) {
        recorder->beginPass(originInWindow);
    }
    return recorder;
}

/**
 * @brief Ends a traced pass: prints its summary when something changed, and the
 * full explanation for any step matching AESTRA_LAYOUT_EXPLAIN. No-op on nullptr.
 */
inline void finishLayoutPass(const NUILayoutRecorder* recorder) {
    if (!recorder || !recorder->passChangedAnything()) {
        return;
    }
    std::fputs(recorder->passSummary().c_str(), stderr);
    static const char* explain = std::getenv("AESTRA_LAYOUT_EXPLAIN");
    if (explain && *explain) {
        for (const auto& s : recorder->steps()) {
            if (s.name.find(explain) != std::string::npos) {
                std::fputs(recorder->explain(s.name).c_str(), stderr);
            }
        }
    }
}

} // namespace Layout
} // namespace AestraUI
