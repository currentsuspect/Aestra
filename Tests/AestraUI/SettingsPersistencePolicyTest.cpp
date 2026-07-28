// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Settings persistence policy (#648).
//
// The defect this pins: opening the Settings dialog rewrote audio_settings.conf
// while the asynchronous device enumeration was still pending, so the device
// dropdowns reported 0 and 0 was written over the user's real device ids. The
// user's saved output device was destroyed by the act of looking at Settings.
//
// The decision itself — "what do we write for a control whose list has not
// loaded?" — is extracted here precisely so it can be tested. The surrounding
// wiring is a UI class with no headless harness; this is the part that can be
// pinned honestly, and it is the part that was wrong.
//
// Note 0 is deliberately used as a "real" id throughout: on this machine the
// corrupted value WAS 0, and 0 is a legal device id on some backends. A policy
// that special-cased 0 instead of tracking populated-ness would pass a weaker
// test and still lose data.

#include "SettingsPersistencePolicy.h"

#include <iostream>
#include <string>

using Aestra::Settings::kNoPersistedValue;
using Aestra::Settings::selectionAfterPopulate;
using Aestra::Settings::valueToPersist;

namespace {

int g_failures = 0;

void expectEq(int got, int wanted, const std::string& what) {
    if (got != wanted) {
        std::cerr << "[FAIL] " << what << ": got " << got << ", wanted " << wanted << '\n';
        ++g_failures;
    }
}

} // namespace

int main() {
    // =====================================================================
    // The discriminator this whole file exists for:
    //
    //     "device 0 is selected"   !=   "nothing is selected"
    //
    // The predicate that shipped first asked "does the list have entries",
    // which cannot tell those apart, because NUIDropdown::getSelectedValue()
    // returns 0 for BOTH. Cases are stated in pairs so a future change cannot
    // satisfy one reading and quietly break the other.
    // =====================================================================

    // --- 1. items present, selectedIndex == -1 -> do NOT persist -------------
    // Reachable today: clearItems() sets selectedIndex_ = -1, and
    // setSelectedByValue() silently no-ops when the value is absent, so a
    // repopulated list can hold items with nothing selected.
    expectEq(valueToPersist(/*hasSelection=*/false, /*currentSelection=*/0, /*lastKnownSaved=*/129), 129,
             "items present but nothing selected: stored id survives");
    expectEq(valueToPersist(false, 0, 7), 7, "same for the input device");
    expectEq(valueToPersist(false, 0, 3), 3, "same for the driver");

    // --- 2. saved id absent from the populated list -> do not invent 0 -------
    // Enumeration succeeded, but the device the user chose is gone. The
    // dropdown reports 0 because nothing matched; 0 must not overwrite the
    // stored id, because the device may come back.
    expectEq(valueToPersist(/*hasSelection=*/false, /*currentSelection=*/0, /*lastKnownSaved=*/129), 129,
             "saved device absent from the list: 0 is not invented or persisted");

    // --- 3. selected item hidden (a state #658 introduces) ------------------
    // #658 sets selectedIndex_ = -1 when the selected item is hidden. Whatever
    // product policy that PR settles on, the persistence answer is pinned here:
    // a hidden selection is not a user selection, so the stored value survives
    // rather than being overwritten by the 0 the widget reports. If #658
    // instead retains a hidden logical selection, the caller passes
    // hasSelection=true with the real value and case 4 covers it.
    expectEq(valueToPersist(/*hasSelection=*/false, /*currentSelection=*/0, /*lastKnownSaved=*/129), 129,
             "selection hidden: stored id survives, 0 is not persisted");

    // --- 4. a REAL selection whose id happens to be 0 -> persist it ----------
    // The other half of the discriminator. Were this to regress to "0 means
    // nothing", a user on a backend where 0 is a legal device id could never
    // save that choice.
    expectEq(valueToPersist(/*hasSelection=*/true, /*currentSelection=*/0, /*lastKnownSaved=*/129), 0,
             "device 0 GENUINELY selected: persist 0, overwriting the stored 129");
    expectEq(valueToPersist(true, 0, kNoPersistedValue), 0,
             "device 0 genuinely selected with nothing stored: persist 0");
    expectEq(valueToPersist(true, 0, 0), 0, "device 0 selected and already stored: stays 0");

    // --- 5. delayed enumeration eventually restores the saved selection ------
    // Only once a real selection exists does the field become writable.
    {
        int stored = 129;
        stored = valueToPersist(false, 0, stored);
        expectEq(stored, 129, "lifecycle: pending enumeration preserves the stored id");
        stored = valueToPersist(false, 0, stored);
        expectEq(stored, 129, "lifecycle: populated-but-unselected still preserves it");
        stored = valueToPersist(true, 129, stored);
        expectEq(stored, 129, "lifecycle: restored selection persists the same id");
        stored = valueToPersist(true, 42, stored);
        expectEq(stored, 42, "lifecycle: a real user change is written");
    }

    // Repeated opens with enumeration never completing must not decay the value.
    {
        int stored = 129;
        for (int i = 0; i < 5; ++i) {
            stored = valueToPersist(false, 0, stored);
        }
        expectEq(stored, 129, "five open/close cycles with no selection preserve the id");
    }

    // --- once something IS selected, the control is authoritative -------------
    expectEq(valueToPersist(true, 129, 129), 129, "selection persists");
    expectEq(valueToPersist(true, 42, 129), 42, "a NEW user selection persists");

    // --- fresh install: nothing stored, so nothing to protect ----------------
    expectEq(valueToPersist(false, 0, kNoPersistedValue), 0,
             "no stored value: current selection is written even with no selection");
    expectEq(valueToPersist(false, 3, kNoPersistedValue), 3, "no stored value: no-selection fallback");
    expectEq(valueToPersist(true, 5, kNoPersistedValue), 5, "no stored value: a selection is written");

    // --- repeated opens must be idempotent -----------------------------------
    // Open, close, open again with the list still pending: the stored value must
    // survive every cycle, not decay after the first.
    {
        int stored = 129;
        for (int i = 0; i < 5; ++i) {
            stored = valueToPersist(false, 0, stored);
        }
        expectEq(stored, 129, "five open/close cycles with a pending list preserve the id");
    }

    // --- population prefers stored intent over the active value --------------
    expectEq(selectionAfterPopulate(/*saved=*/129, /*available=*/true, /*active=*/2), 129,
             "saved device wins when it is present");
    expectEq(selectionAfterPopulate(129, /*available=*/false, /*active=*/2), 2,
             "saved device absent: fall back to the active device");
    expectEq(selectionAfterPopulate(kNoPersistedValue, false, 2), 2,
             "nothing saved: use the active device");
    expectEq(selectionAfterPopulate(kNoPersistedValue, true, 2), 2,
             "nothing saved: availability is irrelevant");
    expectEq(selectionAfterPopulate(0, /*available=*/true, /*active=*/5), 0,
             "a saved id of 0 that is present still wins");

    // A saved value that is present must not be discarded just because the
    // engine happens to be running on something else — that discard is what made
    // the next save persist the runtime value over stored intent.
    expectEq(selectionAfterPopulate(129, true, 129), 129, "saved and active agreeing is stable");

    if (g_failures != 0) {
        std::cerr << "[FAIL] SettingsPersistencePolicyTest: " << g_failures << " failure(s)\n";
        return 1;
    }
    std::cout << "[PASS] SettingsPersistencePolicyTest\n";
    return 0;
}
