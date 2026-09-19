// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "UISurfaceStore.h"

#include <optional>
#include <string>

namespace Aestra {

/**
 * @file ExportDialogPreferences.h
 * @brief V8-C14 step 5d (FD-23): the export dialog's settings live in the
 * shared surface store under the single `dialog.export` record, the last
 * reserved slot step 1 defined but nothing wrote to.
 *
 * The split matches step 5c's rail widths exactly:
 *   - applyStoredExportOptions() reads a preference and never writes one. Each
 *     field is independent — an unset one leaves whatever default the dialog
 *     just computed, and that default is never written back.
 *   - captureExportOptions() builds the record at the commit point, from the
 *     settings an export actually ran with.
 *
 * Two rules this file exists to make testable:
 *
 * 1. **A stored index is untrusted.** It comes off disk and indexes straight
 *    into the dialog's option vectors, so an out-of-range value would read out
 *    of bounds. `-1` is the schema's documented "the user never chose"; every
 *    other out-of-range value is corrupt or from a future schema with more
 *    options, and both fall back to the computed default rather than the store.
 * 2. **Only the output *directory* is a preference.** The file name always
 *    follows the current project, so remembering a full path would export the
 *    previous project's name into the new project's folder.
 */

/// The size of each of the dialog's three option lists, so a stored index can
/// be range-checked against the build that is reading it rather than against a
/// count baked in here.
struct ExportOptionCounts {
    int sampleRateCount{0};
    int bitDepthCount{0};
    int scopeCount{0};
};

/// The dialog's live settings, in the shape both operations work on. Seeded by
/// the caller with the computed defaults before applying, and read back from
/// the caller's own fields when capturing.
struct ExportDialogValues {
    int sampleRateIndex{0};
    int bitDepthIndex{0};
    int scopeIndex{0};
    std::string tailInput;
    std::string outputDirectory;
};

/// True when @p index is a usable selection for a list of @p count entries.
inline bool isUsableOptionIndex(int index, int count) noexcept {
    return index >= 0 && index < count;
}

/// Applies the stored record over @p live, field by field. Returns true when at
/// least one field came from the store.
///
/// @p directoryUsable answers "does this stored directory still exist?" and is
/// injected rather than called directly so this stays pure and testable without
/// touching a real filesystem — the same seam step 5b used for the editor
/// anchor provider.
template <typename DirectoryUsable>
bool applyStoredExportOptions(ExportDialogValues& live, const std::optional<UIDialogExportOptions>& stored,
                              const ExportOptionCounts& counts, DirectoryUsable&& directoryUsable) {
    if (!stored.has_value()) {
        return false;
    }

    bool appliedAny = false;
    if (isUsableOptionIndex(stored->sampleRateIndex, counts.sampleRateCount)) {
        live.sampleRateIndex = stored->sampleRateIndex;
        appliedAny = true;
    }
    if (isUsableOptionIndex(stored->bitDepthIndex, counts.bitDepthCount)) {
        live.bitDepthIndex = stored->bitDepthIndex;
        appliedAny = true;
    }
    if (isUsableOptionIndex(stored->scopeIndex, counts.scopeCount)) {
        live.scopeIndex = stored->scopeIndex;
        appliedAny = true;
    }
    if (!stored->tailInput.empty()) {
        live.tailInput = stored->tailInput;
        appliedAny = true;
    }
    if (!stored->lastOutputDirectory.empty() && directoryUsable(stored->lastOutputDirectory)) {
        live.outputDirectory = stored->lastOutputDirectory;
        appliedAny = true;
    }
    return appliedAny;
}

/// Builds the record to store from the settings an export actually ran with.
inline UIDialogExportOptions captureExportOptions(const ExportDialogValues& live) {
    UIDialogExportOptions options;
    options.sampleRateIndex = live.sampleRateIndex;
    options.bitDepthIndex = live.bitDepthIndex;
    options.scopeIndex = live.scopeIndex;
    options.tailInput = live.tailInput;
    options.lastOutputDirectory = live.outputDirectory;
    return options;
}

} // namespace Aestra
