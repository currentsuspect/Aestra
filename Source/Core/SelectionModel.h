// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
/**
 * @file SelectionModel.h
 * @brief The one selection in the app: pure control state, no UI, no audio, no window (Selection System Spec 2.1).
 *
 * UI thread only. Ids are stable DOMAIN ids (unit id, clip id, note id), never row indices, so a reorder or a scroll
 * cannot change what is selected. The model stores ids and nothing else; domain data stays authoritative.
 *
 * Two slots:
 *  - the ACTIVE slot holds the single selection across every non-exempt surface. Selecting on a different
 *    surface, or pressing (pressedIn) in a different non-exempt surface, clears it first; nothing is remembered.
 *  - the EXEMPT slot holds the selection of an exempt surface (AuditionFiles). It neither clears nor is cleared
 *    by activity elsewhere; only select/removeIds on that surface or clearExempt() change it.
 *
 * Notifications: delivered after the state is updated, once per mutating call that changed something, in
 * subscription order. A callback may unsubscribe (itself or another: an unsubscribed callback is not called
 * again, even in the current delivery) and may call select/clear/etc.: such nested changes are queued and delivered
 * after the current change has reached every subscriber, so all subscribers see changes in the same order. State
 * read inside a callback is always the latest, which can be newer than that change's `after`. A subscriber added
 * during delivery first sees the next change.
 */

#pragma once

#include <algorithm>
#include <cstdint>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace Aestra {

enum class SelectionSurface {
    ArsenalUnits, ArsenalSteps, ArsenalPatterns, TracksAndChannels, TimelineClips,
    BrowserRows, PatternsPanel, PianoRollNotes, AutomationPoints,
    Takes, AuditionFiles, SoundDrawer
};

struct SelectionKey {
    SelectionSurface surface;
    uint64_t id;  // a stable domain id, never a row index
    bool operator==(const SelectionKey& o) const { return surface == o.surface && id == o.id; }
};

/// Click, ctrl-click, shift-click, marquee-add. Extend and Add behave identically here: working out the range
/// for a shift-click is the CALLER's job, the model only receives the ids to add.
enum class SelectMode { Replace, Toggle, Extend, Add };

enum class Reason { Click, Key, Marquee, Command, Undo, Programmatic };

struct Selection {
    SelectionSurface surface;
    std::vector<uint64_t> ids;  // ordered, no duplicates, never empty while stored
    uint64_t primary;           // always a member of ids
    bool operator==(const Selection& o) const { return surface == o.surface && ids == o.ids && primary == o.primary; }
};

struct SelectionChange {
    std::optional<Selection> before, after;
    Reason reason = Reason::Programmatic;
};

namespace selection_detail {
struct Registry {
    using Callback = std::function<void(const SelectionChange&)>;
    struct Entry {
        uint64_t token;
        std::shared_ptr<Callback> fn;
    };
    std::vector<Entry> entries;
    uint64_t nextToken = 1;
    std::deque<SelectionChange> pending;
    bool delivering = false;

    void remove(uint64_t token) {
        entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Entry& e) { return e.token == token; }),
                      entries.end());
    }
    bool has(uint64_t token) const {
        return std::any_of(entries.begin(), entries.end(), [&](const Entry& e) { return e.token == token; });
    }
    // A callback that throws must not wedge delivery or starve the subscribers after it: every callback and every
    // queued change is still delivered, then the first failure is rethrown to the caller that made the change.
    void publish(SelectionChange change) {
        pending.push_back(std::move(change));
        if (delivering) return;  // the outer loop below delivers it
        delivering = true;
        std::exception_ptr firstFailure;
        while (!pending.empty()) {
            const SelectionChange current = std::move(pending.front());
            pending.pop_front();
            const std::vector<Entry> snapshot = entries;  // callbacks may subscribe/unsubscribe
            for (const Entry& e : snapshot) {
                if (!has(e.token)) continue;
                try {
                    (*e.fn)(current);
                } catch (...) {
                    if (!firstFailure) firstFailure = std::current_exception();
                }
            }
        }
        delivering = false;
        if (firstFailure) std::rethrow_exception(firstFailure);
    }
};
}  // namespace selection_detail

/// RAII handle: unsubscribes when destroyed or reset. Move-only; safe to outlive the model.
class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& o) noexcept : registry_(std::move(o.registry_)), token_(o.token_) { o.token_ = 0; }
    Subscription& operator=(Subscription&& o) noexcept {
        if (this != &o) {
            reset();
            registry_ = std::move(o.registry_);
            token_ = o.token_;
            o.token_ = 0;
        }
        return *this;
    }
    ~Subscription() { reset(); }

    void reset() {
        if (auto r = registry_.lock()) r->remove(token_);
        registry_.reset();
        token_ = 0;
    }
    explicit operator bool() const { return token_ != 0 && !registry_.expired(); }

private:
    friend class SelectionModel;
    Subscription(std::weak_ptr<selection_detail::Registry> r, uint64_t t) : registry_(std::move(r)), token_(t) {}
    std::weak_ptr<selection_detail::Registry> registry_;
    uint64_t token_ = 0;
};

class SelectionModel {
public:
    using Callback = std::function<void(const SelectionChange&)>;

    SelectionModel() : registry_(std::make_shared<selection_detail::Registry>()) {}
    SelectionModel(const SelectionModel&) = delete;
    SelectionModel& operator=(const SelectionModel&) = delete;

    /// Audition's file list is the only exempt surface; the transport and title bar are not selection surfaces.
    static bool isExempt(SelectionSurface s) { return s == SelectionSurface::AuditionFiles; }

    void select(SelectionSurface surface, const std::vector<uint64_t>& rawIds, SelectMode mode, Reason reason) {
        std::optional<Selection>& slot = slotFor(surface);
        const std::optional<Selection> before = slot;
        const std::vector<uint64_t> ids = unique(rawIds);

        // A different surface than the slot holds: the old selection is cleared first, then the mode applies to
        // an empty set. One change event covers both.
        const bool sameSurface = slot && slot->surface == surface;
        std::vector<uint64_t> next = sameSurface ? slot->ids : std::vector<uint64_t>{};
        uint64_t primary = sameSurface ? slot->primary : 0;

        switch (mode) {
            case SelectMode::Replace:
                next = ids;
                if (!next.empty()) primary = next.back();
                break;
            case SelectMode::Toggle: {
                bool added = false;
                uint64_t lastAdded = 0;
                for (uint64_t id : ids) {
                    auto it = std::find(next.begin(), next.end(), id);
                    if (it != next.end()) {
                        next.erase(it);
                    } else {
                        next.push_back(id);
                        added = true;
                        lastAdded = id;
                    }
                }
                if (added) primary = lastAdded;
                else if (!next.empty() && std::find(next.begin(), next.end(), primary) == next.end()) primary = next.back();
                break;
            }
            case SelectMode::Extend:
            case SelectMode::Add: {
                const bool wasEmpty = next.empty();
                for (uint64_t id : ids)
                    if (std::find(next.begin(), next.end(), id) == next.end()) next.push_back(id);
                if (wasEmpty && !next.empty()) primary = next.back();
                break;
            }
        }

        if (next.empty()) slot.reset();
        else slot = Selection{surface, std::move(next), primary};
        publishIfChanged(before, slot, reason);
    }

    /// Clears the non-exempt selection (Escape, a command that removes the selected thing). Leaves the exempt slot.
    void clear(Reason reason) { clearSlot(active_, reason); }
    void clearExempt(Reason reason) { clearSlot(exempt_, reason); }

    /// A press anywhere in `where`, even on empty space. Another non-exempt surface than the active one clears the
    /// active selection and leaves it empty; the press then selects through select() if it hit something.
    /// A press in the active surface or in an exempt surface does nothing.
    void pressedIn(SelectionSurface where, Reason reason) {
        if (isExempt(where)) return;
        if (active_ && active_->surface != where) clearSlot(active_, reason);
    }

    /// For commands that delete things: drops the ids from `surface`'s selection, keeps the rest, clears if none
    /// remain. If the primary was removed it becomes the last remaining id.
    void removeIds(SelectionSurface surface, const std::vector<uint64_t>& removed, Reason reason) {
        std::optional<Selection>& slot = slotFor(surface);
        if (!slot || slot->surface != surface) return;
        const std::vector<uint64_t> rawIds = removed;  // a delete command may pass m.active()->ids, which erase() would invalidate
        const std::optional<Selection> before = slot;
        auto& v = slot->ids;
        for (uint64_t id : rawIds) v.erase(std::remove(v.begin(), v.end(), id), v.end());
        if (v.empty()) slot.reset();
        else if (std::find(v.begin(), v.end(), slot->primary) == v.end()) slot->primary = v.back();
        publishIfChanged(before, slot, reason);
    }

    const std::optional<Selection>& active() const { return active_; }
    const std::optional<Selection>& activeExempt() const { return exempt_; }

    bool isSelected(SelectionKey key) const {
        const std::optional<Selection>& slot = isExempt(key.surface) ? exempt_ : active_;
        return slot && slot->surface == key.surface &&
               std::find(slot->ids.begin(), slot->ids.end(), key.id) != slot->ids.end();
    }

    Subscription subscribe(Callback cb) {
        auto& r = *registry_;
        const uint64_t token = r.nextToken++;
        r.entries.push_back({token, std::make_shared<Callback>(std::move(cb))});
        return Subscription(registry_, token);
    }

    /// What must repaint for `change`: ids whose selected state flipped, plus a primary that moved. A surface
    /// switch yields every old and every new id. Before-ids first, then after-ids, no duplicates.
    static std::vector<SelectionKey> affectedKeys(const SelectionChange& change) {
        std::vector<SelectionKey> out;
        auto contains = [](const std::optional<Selection>& s, uint64_t id) {
            return s && std::find(s->ids.begin(), s->ids.end(), id) != s->ids.end();
        };
        auto add = [&](SelectionSurface s, uint64_t id) {
            const SelectionKey k{s, id};
            if (std::find(out.begin(), out.end(), k) == out.end()) out.push_back(k);
        };
        const bool sameSurface = change.before && change.after && change.before->surface == change.after->surface;
        if (change.before)
            for (uint64_t id : change.before->ids)
                if (!sameSurface || !contains(change.after, id)) add(change.before->surface, id);
        if (change.after)
            for (uint64_t id : change.after->ids)
                if (!sameSurface || !contains(change.before, id)) add(change.after->surface, id);
        if (sameSurface && change.before->primary != change.after->primary) {
            add(change.after->surface, change.before->primary);
            add(change.after->surface, change.after->primary);
        }
        return out;
    }

private:
    std::optional<Selection>& slotFor(SelectionSurface s) { return isExempt(s) ? exempt_ : active_; }

    static std::vector<uint64_t> unique(const std::vector<uint64_t>& in) {
        std::vector<uint64_t> out;
        out.reserve(in.size());
        for (uint64_t id : in)
            if (std::find(out.begin(), out.end(), id) == out.end()) out.push_back(id);
        return out;
    }

    void clearSlot(std::optional<Selection>& slot, Reason reason) {
        if (!slot) return;
        const std::optional<Selection> before = slot;
        slot.reset();
        publishIfChanged(before, slot, reason);
    }

    void publishIfChanged(const std::optional<Selection>& before, const std::optional<Selection>& after, Reason reason) {
        if (before == after) return;
        const std::shared_ptr<selection_detail::Registry> keepAlive = registry_;  // a callback may destroy this model
        keepAlive->publish(SelectionChange{before, after, reason});
    }

    std::optional<Selection> active_, exempt_;
    std::shared_ptr<selection_detail::Registry> registry_;
};

}  // namespace Aestra
