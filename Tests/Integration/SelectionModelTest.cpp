// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// SelectionModel policy table (Selection System Spec 2.1, 2.3, 5, 7): single select across surfaces, the exempt
// slot, every SelectMode, clear/pressedIn/removeIds, subscription delivery rules, affectedKeys, and a seeded
// random-gesture run that checks the invariants against a brute-force snapshot diff. Pure header, no libraries.

#include "SelectionModel.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace Aestra;
using S = SelectionSurface;
using M = SelectMode;
using R = Reason;
using Ids = std::vector<uint64_t>;

namespace {

int g_checks = 0;

void check(bool condition, const std::string& message) {
    ++g_checks;
    if (!condition) {
        std::cerr << "[FAIL] " << message << "\n";
        std::exit(1);
    }
}

bool idsAre(const SelectionModel& m, S surface, const Ids& expected, uint64_t primary) {
    const auto& slot = SelectionModel::isExempt(surface) ? m.activeExempt() : m.active();
    return slot && slot->surface == surface && slot->ids == expected && slot->primary == primary;
}

using Keys = std::set<std::pair<int, uint64_t>>;
Keys keySet(const std::vector<SelectionKey>& keys) {
    Keys out;
    for (const auto& k : keys) out.insert({static_cast<int>(k.surface), k.id});
    return out;
}

void testReplaceAndDuplicates() {
    SelectionModel m;
    check(!m.active() && !m.activeExempt(), "a new model selects nothing");
    m.select(S::ArsenalUnits, {3, 1, 3, 2, 1}, M::Replace, R::Click);
    check(idsAre(m, S::ArsenalUnits, {3, 1, 2}, 2), "replace collapses duplicates, keeps order, primary = last");
    check(m.isSelected({S::ArsenalUnits, 1}) && !m.isSelected({S::ArsenalUnits, 9}), "isSelected by id");
    check(!m.isSelected({S::ArsenalSteps, 1}), "same id on another surface is not selected");
    m.select(S::ArsenalUnits, {7}, M::Replace, R::Click);
    check(idsAre(m, S::ArsenalUnits, {7}, 7), "replace sets exactly the ids");
    m.select(S::ArsenalUnits, {}, M::Replace, R::Click);
    check(!m.active(), "replace with no ids clears");
}

void testToggle() {
    SelectionModel m;
    m.select(S::TimelineClips, {1, 2, 3}, M::Replace, R::Click);
    m.select(S::TimelineClips, {2}, M::Toggle, R::Click);
    check(idsAre(m, S::TimelineClips, {1, 3}, 3), "toggle off a middle id keeps the primary");
    m.select(S::TimelineClips, {3}, M::Toggle, R::Click);
    check(idsAre(m, S::TimelineClips, {1}, 1), "toggle off the primary moves it to the remaining last");
    m.select(S::TimelineClips, {5, 1}, M::Toggle, R::Click);
    check(idsAre(m, S::TimelineClips, {5}, 5), "toggle flips each id: 5 on, 1 off, primary = last added");
    m.select(S::TimelineClips, {5}, M::Toggle, R::Click);
    check(!m.active(), "toggling the last id off empties and clears");
    m.select(S::Takes, {4}, M::Toggle, R::Click);
    check(idsAre(m, S::Takes, {4}, 4), "toggle on an empty model selects");
}

void testExtendAdd() {
    for (M mode : {M::Extend, M::Add}) {
        SelectionModel m;
        m.select(S::PianoRollNotes, {10, 11}, M::Replace, R::Click);
        m.select(S::PianoRollNotes, {12, 10, 13}, mode, R::Click);
        check(idsAre(m, S::PianoRollNotes, {10, 11, 12, 13}, 11), "extend/add append new ids and keep the primary");
        SelectionModel e;
        e.select(S::PianoRollNotes, {4, 5, 4}, mode, R::Click);
        check(idsAre(e, S::PianoRollNotes, {4, 5}, 5), "extend/add on an empty set: primary = last id added");
        SelectionModel n;
        n.select(S::PianoRollNotes, {}, mode, R::Click);
        check(!n.active(), "extend/add with no ids on an empty model does nothing");
    }
}

void testSurfaceSwitchAndPress() {
    SelectionModel m;
    m.select(S::ArsenalUnits, {1, 2}, M::Replace, R::Click);
    m.select(S::TimelineClips, {9}, M::Add, R::Click);
    check(idsAre(m, S::TimelineClips, {9}, 9), "select on another surface replaces the whole selection");
    check(!m.isSelected({S::ArsenalUnits, 1}), "the old surface is cleared, nothing remembered");

    m.pressedIn(S::TimelineClips, R::Click);
    check(m.active().has_value(), "a press in the active surface changes nothing");
    m.pressedIn(S::BrowserRows, R::Click);
    check(!m.active(), "a press in another non-exempt surface clears and leaves it empty");
    m.pressedIn(S::BrowserRows, R::Click);
    check(!m.active(), "a press with nothing selected stays empty");

    // Tracks and channels are one surface: no second slot for the mixer.
    m.select(S::TracksAndChannels, {5}, M::Replace, R::Click);
    m.pressedIn(S::TracksAndChannels, R::Click);
    check(idsAre(m, S::TracksAndChannels, {5}, 5), "mixer channel and timeline track share one surface");
}

void testExemptSlot() {
    check(SelectionModel::isExempt(S::AuditionFiles), "AuditionFiles is exempt");
    for (S s : {S::ArsenalUnits, S::ArsenalSteps, S::ArsenalPatterns, S::TracksAndChannels, S::TimelineClips,
                S::BrowserRows, S::PatternsPanel, S::PianoRollNotes, S::AutomationPoints, S::Takes, S::SoundDrawer})
        check(!SelectionModel::isExempt(s), "every other surface is not exempt");

    SelectionModel m;
    m.select(S::ArsenalUnits, {1}, M::Replace, R::Click);
    m.select(S::AuditionFiles, {100, 101}, M::Replace, R::Click);
    check(idsAre(m, S::ArsenalUnits, {1}, 1), "an exempt select does not clear the non-exempt selection");
    check(idsAre(m, S::AuditionFiles, {100, 101}, 101), "the exempt surface keeps its own selection");
    check(m.isSelected({S::AuditionFiles, 100}) && m.isSelected({S::ArsenalUnits, 1}), "isSelected sees both slots");

    m.pressedIn(S::AuditionFiles, R::Click);
    check(m.active().has_value(), "a press in the exempt surface is ignored");
    m.pressedIn(S::TimelineClips, R::Click);
    check(!m.active() && m.activeExempt().has_value(), "a press elsewhere clears the active one, never the exempt one");
    m.select(S::TimelineClips, {2}, M::Replace, R::Click);
    check(m.activeExempt().has_value(), "a non-exempt select leaves the exempt slot alone");
    m.clear(R::Key);
    check(!m.active() && m.activeExempt().has_value(), "clear leaves the exempt slot");
    m.clearExempt(R::Key);
    check(!m.activeExempt(), "clearExempt clears the exempt slot");
}

void testRemoveIds() {
    SelectionModel m;
    m.select(S::TimelineClips, {1, 2, 3, 4}, M::Replace, R::Click);
    m.removeIds(S::ArsenalUnits, {1}, R::Command);
    check(idsAre(m, S::TimelineClips, {1, 2, 3, 4}, 4), "removeIds on another surface does nothing");
    m.removeIds(S::TimelineClips, {9}, R::Command);
    check(idsAre(m, S::TimelineClips, {1, 2, 3, 4}, 4), "removeIds of unselected ids does nothing");
    m.removeIds(S::TimelineClips, {2, 9}, R::Command);
    check(idsAre(m, S::TimelineClips, {1, 3, 4}, 4), "removeIds drops only the selected ones");
    m.removeIds(S::TimelineClips, {4}, R::Command);
    check(idsAre(m, S::TimelineClips, {1, 3}, 3), "removing the primary moves it to the remaining last");
    m.removeIds(S::TimelineClips, {3, 1}, R::Command);
    check(!m.active(), "removing everything clears");
    m.select(S::AuditionFiles, {7, 8}, M::Replace, R::Click);
    m.removeIds(S::AuditionFiles, {8}, R::Command);
    check(idsAre(m, S::AuditionFiles, {7}, 7), "removeIds works on the exempt slot");
}

struct Recorder {
    std::vector<SelectionChange> seen;
    SelectionModel::Callback fn() {
        return [this](const SelectionChange& c) { seen.push_back(c); };
    }
};

void testNotifications() {
    SelectionModel m;
    Recorder rec;
    Subscription sub = m.subscribe(rec.fn());

    m.select(S::ArsenalUnits, {1, 2}, M::Replace, R::Click);
    check(rec.seen.size() == 1 && !rec.seen[0].before && rec.seen[0].after && rec.seen[0].reason == R::Click,
          "one change per mutating call, reason carried, before empty");
    m.select(S::ArsenalUnits, {1, 2}, M::Replace, R::Click);
    m.select(S::ArsenalUnits, {2}, M::Add, R::Click);
    m.clear(R::Key);
    m.clear(R::Key);
    m.pressedIn(S::ArsenalUnits, R::Click);
    m.removeIds(S::ArsenalUnits, {1}, R::Command);
    check(rec.seen.size() == 2 && !rec.seen[1].after && rec.seen[1].reason == R::Key,
          "no-op calls notify nothing; a clear is one event with after empty");

    m.select(S::ArsenalUnits, {1}, M::Replace, R::Click);
    rec.seen.clear();
    m.select(S::TimelineClips, {5}, M::Replace, R::Marquee);
    check(rec.seen.size() == 1 && rec.seen[0].before->surface == S::ArsenalUnits &&
              rec.seen[0].after->surface == S::TimelineClips,
          "a surface switch is a single event carrying old and new");

    // Delivered after the state is updated.
    Subscription probe = m.subscribe([&](const SelectionChange& c) {
        check(m.active() && m.active()->ids == c.after->ids, "state is already updated inside the callback");
    });
    m.select(S::TimelineClips, {6}, M::Replace, R::Click);
    probe.reset();

    // Order = subscription order.
    std::vector<int> order;
    Subscription a = m.subscribe([&](const SelectionChange&) { order.push_back(1); });
    Subscription b = m.subscribe([&](const SelectionChange&) { order.push_back(2); });
    m.clear(R::Key);
    check(order == (std::vector<int>{1, 2}), "callbacks run in subscription order");

    // Unsubscribe by handle.
    a.reset();
    a.reset();
    order.clear();
    m.select(S::Takes, {1}, M::Replace, R::Click);
    check(order == (std::vector<int>{2}), "an unsubscribed callback is not called; reset is idempotent");
    {
        Subscription scoped = m.subscribe([&](const SelectionChange&) { order.push_back(3); });
    }
    order.clear();
    m.clear(R::Key);
    check(order == (std::vector<int>{2}), "destroying the handle unsubscribes");
}

void testReentrancy() {
    // A callback unsubscribing a later subscriber: that subscriber is not called afterwards.
    {
        SelectionModel m;
        std::vector<int> order;
        Subscription later;
        Subscription first = m.subscribe([&](const SelectionChange&) {
            order.push_back(1);
            later.reset();
        });
        later = m.subscribe([&](const SelectionChange&) { order.push_back(2); });
        m.select(S::Takes, {1}, M::Replace, R::Click);
        check(order == (std::vector<int>{1}), "a subscriber removed mid-delivery is skipped");
    }
    // A callback unsubscribing itself is safe.
    {
        SelectionModel m;
        int calls = 0;
        Subscription self;
        self = m.subscribe([&](const SelectionChange&) {
            ++calls;
            self.reset();
        });
        m.select(S::Takes, {1}, M::Replace, R::Click);
        m.select(S::Takes, {2}, M::Replace, R::Click);
        check(calls == 1, "a callback may unsubscribe itself");
    }
    // A callback calling select: the nested change is queued and every subscriber sees changes in order.
    {
        SelectionModel m;
        std::vector<std::pair<int, uint64_t>> log;
        Subscription s1 = m.subscribe([&](const SelectionChange& c) {
            log.push_back({1, c.after ? c.after->primary : 0});
            if (c.after && c.after->primary == 1) m.select(S::Takes, {2}, M::Replace, R::Programmatic);
        });
        Subscription s2 = m.subscribe([&](const SelectionChange& c) { log.push_back({2, c.after ? c.after->primary : 0}); });
        m.select(S::Takes, {1}, M::Replace, R::Click);
        check(log == (std::vector<std::pair<int, uint64_t>>{{1, 1}, {2, 1}, {1, 2}, {2, 2}}),
              "a nested select is delivered after the current change reaches everyone, in order");
        check(idsAre(m, S::Takes, {2}, 2), "the nested select took effect");
    }
    // A subscriber added during delivery starts with the next change.
    {
        SelectionModel m;
        int lateCalls = 0;
        Subscription late;
        Subscription s = m.subscribe([&](const SelectionChange&) {
            if (!late) late = m.subscribe([&](const SelectionChange&) { ++lateCalls; });
        });
        m.select(S::Takes, {1}, M::Replace, R::Click);
        check(lateCalls == 0, "a subscriber added mid-delivery misses the current change");
        m.select(S::Takes, {2}, M::Replace, R::Click);
        check(lateCalls == 1, "...and gets the next one");
    }
    // A handle outliving the model is harmless.
    {
        Subscription orphan;
        {
            SelectionModel m;
            orphan = m.subscribe([](const SelectionChange&) {});
        }
        orphan.reset();
        check(true, "a handle may outlive its model");
    }
}

void testAffectedKeys() {
    SelectionModel m;
    SelectionChange last;
    Subscription sub = m.subscribe([&](const SelectionChange& c) { last = c; });

    m.select(S::ArsenalUnits, {1, 2, 3}, M::Replace, R::Click);
    check(keySet(SelectionModel::affectedKeys(last)) == Keys({{0, 1}, {0, 2}, {0, 3}}), "first select: all new ids");
    m.select(S::ArsenalUnits, {2}, M::Toggle, R::Click);
    check(keySet(SelectionModel::affectedKeys(last)) == Keys({{0, 2}}), "toggle: only the toggled id (primary unchanged)");
    m.select(S::ArsenalUnits, {9}, M::Replace, R::Click);
    check(keySet(SelectionModel::affectedKeys(last)) == Keys({{0, 1}, {0, 3}, {0, 9}}), "replace: old and new ids");
    m.select(S::ArsenalUnits, {9, 4}, M::Add, R::Click);
    check(keySet(SelectionModel::affectedKeys(last)) == Keys({{0, 4}}), "add: only the added id");
    m.select(S::TimelineClips, {9}, M::Replace, R::Click);
    check(keySet(SelectionModel::affectedKeys(last)) ==
              Keys({{0, 9}, {0, 4}, {static_cast<int>(S::TimelineClips), 9}}),
          "surface switch: every old and new key, same id on two surfaces stays distinct");
    check(SelectionModel::affectedKeys(SelectionChange{}).empty(), "an empty change affects nothing");
}

// ---- seeded random gestures against a brute-force model ------------------------------------------------------
void testFuzz() {
    const S surfaces[] = {S::ArsenalUnits, S::TimelineClips, S::PianoRollNotes, S::AuditionFiles, S::Takes};
    const M modes[] = {M::Replace, M::Toggle, M::Extend, M::Add};
    const R reasons[] = {R::Click, R::Key, R::Marquee, R::Command, R::Undo, R::Programmatic};
    std::mt19937 rng(0xA357u);
    auto pick = [&](size_t n) { return static_cast<size_t>(rng() % n); };

    SelectionModel m;
    int events = 0;
    Subscription sub = m.subscribe([&](const SelectionChange& c) {
        ++events;
        // brute-force snapshot diff: every key whose selected state differs must be in affectedKeys
        auto has = [](const std::optional<Selection>& s, S surface, uint64_t id) {
            return s && s->surface == surface &&
                   std::find(s->ids.begin(), s->ids.end(), id) != s->ids.end();
        };
        const Keys affected = keySet(SelectionModel::affectedKeys(c));
        for (S s : surfaces)
            for (uint64_t id = 0; id < 12; ++id)
                if (has(c.before, s, id) != has(c.after, s, id))
                    check(affected.count({static_cast<int>(s), id}) == 1, "affectedKeys covers a changed key");
        check(!(c.before == c.after), "no event for an unchanged selection");
    });

    for (int i = 0; i < 4000; ++i) {
        const R reason = reasons[pick(6)];
        switch (pick(6)) {
            case 0: case 1: case 2: {
                Ids ids;
                for (size_t n = pick(5); n > 0; --n) ids.push_back(pick(12));
                m.select(surfaces[pick(5)], ids, modes[pick(4)], reason);
                break;
            }
            case 3: m.pressedIn(surfaces[pick(5)], reason); break;
            case 4: {
                Ids ids;
                for (size_t n = pick(4); n > 0; --n) ids.push_back(pick(12));
                m.removeIds(surfaces[pick(5)], ids, reason);
                break;
            }
            default: pick(2) ? m.clear(reason) : m.clearExempt(reason); break;
        }
        for (const auto* slot : {&m.active(), &m.activeExempt()}) {
            if (!*slot) continue;
            const Selection& s = **slot;
            check(!s.ids.empty(), "a stored selection is never empty");
            check(std::set<uint64_t>(s.ids.begin(), s.ids.end()).size() == s.ids.size(), "ids are unique");
            check(std::find(s.ids.begin(), s.ids.end(), s.primary) != s.ids.end(), "primary is a member");
            check(SelectionModel::isExempt(s.surface) == (slot == &m.activeExempt()), "slot holds the right kind");
        }
    }
    check(events > 500, "the run actually changed the selection many times");
}

}  // namespace

int main() {
    testReplaceAndDuplicates();
    testToggle();
    testExtendAdd();
    testSurfaceSwitchAndPress();
    testExemptSlot();
    testRemoveIds();
    testNotifications();
    testReentrancy();
    testAffectedKeys();
    testFuzz();
    std::cout << "SelectionModelTest: " << g_checks << " checks passed\n";
    return 0;
}
