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
    // --- the regression, stated directly -------------------------------------
    // Saved device 129; dialog opens; list has not populated so the control
    // reports 0. Writing 0 here is the bug.
    expectEq(valueToPersist(/*listPopulated=*/false, /*currentSelection=*/0, /*lastKnownSaved=*/129), 129,
             "unpopulated list must not overwrite a stored device id");

    // Same shape for the input device and the driver.
    expectEq(valueToPersist(false, 0, 7), 7, "unpopulated list preserves stored input device");
    expectEq(valueToPersist(false, 0, 0), 0, "stored id of 0 is preserved as 0, not treated as absent");

    // --- once the list has loaded, the control is authoritative ---------------
    expectEq(valueToPersist(true, 129, 129), 129, "populated list persists the selection");
    expectEq(valueToPersist(true, 42, 129), 42, "populated list persists a NEW user selection");
    expectEq(valueToPersist(true, 0, 129), 0,
             "populated list persists 0 when 0 is genuinely selected");

    // --- fresh install: nothing stored, so nothing to protect ----------------
    expectEq(valueToPersist(false, 0, kNoPersistedValue), 0,
             "no stored value: current selection is written even unpopulated");
    expectEq(valueToPersist(false, 3, kNoPersistedValue), 3, "no stored value: unpopulated fallback");
    expectEq(valueToPersist(true, 5, kNoPersistedValue), 5, "no stored value: populated writes selection");

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
