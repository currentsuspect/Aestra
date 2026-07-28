// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Persistence policy for settings controls whose backing list is populated
// asynchronously (#648).
//
// The device, input-device and driver dropdowns are filled by a background
// hardware enumeration. Anything that writes the config before that finishes
// reads an unpopulated control, gets 0, and persists it over a real device id —
// which is how a user's saved output device was being destroyed by the act of
// opening the Settings dialog.
//
// Kept free of widget and audio dependencies so the decision itself can be
// tested without constructing a UI.

namespace Aestra {
namespace Settings {

/**
 * @brief Sentinel for "no value has ever been persisted for this control".
 *
 * Distinct from 0, which is a legal device id on some backends and is exactly
 * what an unpopulated dropdown reports.
 */
inline constexpr int kNoPersistedValue = -1;

/**
 * @brief Decide what to write for a control that may not have finished loading.
 *
 * @param listPopulated   Whether the control's item list has been filled in.
 * @param currentSelection The control's current selected value.
 * @param lastKnownSaved  The value previously read from (or written to) the
 *                        config, or kNoPersistedValue if there was none.
 *
 * An unpopulated list is not a user selection. When the list has not loaded,
 * the previously persisted value is preserved; only when there was never one
 * does the current selection get written, since there is then no user intent
 * to protect.
 */
inline int valueToPersist(bool listPopulated, int currentSelection, int lastKnownSaved) {
    if (listPopulated) {
        return currentSelection;
    }
    if (lastKnownSaved != kNoPersistedValue) {
        return lastKnownSaved;
    }
    return currentSelection;
}

/**
 * @brief Decide which entry a freshly-populated list should select.
 *
 * Stored intent wins over the currently active value whenever the stored value
 * is actually available. Otherwise the active value is the honest choice —
 * showing a device that is not present would make the dialog describe something
 * the engine is not doing.
 */
inline int selectionAfterPopulate(int savedValue, bool savedValueAvailable, int activeValue) {
    if (savedValue != kNoPersistedValue && savedValueAvailable) {
        return savedValue;
    }
    return activeValue;
}

} // namespace Settings
} // namespace Aestra
