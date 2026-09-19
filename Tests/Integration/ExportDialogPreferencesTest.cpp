// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V8-C14 step 5d (FD-23): the export dialog's settings live in the surface
// store's single `dialog.export` record. These checks pin the step's decision
// points: a stored field wins over the dialog's computed default while an
// unset one defers, a stored index that does not address the running build's
// option list is refused rather than read out of bounds, only the output
// directory is remembered (never the file name), and the record survives a
// restart of the store.

#include "../../Source/Core/ExportDialogPreferences.h"

#include "../Support/TestTempDirectory.h"

#include <iostream>
#include <string>

namespace {

int g_failures = 0;

void expect(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        ++g_failures;
    }
}

/// The counts ExportDialog actually has today: 4 sample rates, 3 bit depths, 3 scopes.
Aestra::ExportOptionCounts dialogCounts() {
    Aestra::ExportOptionCounts counts;
    counts.sampleRateCount = 4;
    counts.bitDepthCount = 3;
    counts.scopeCount = 3;
    return counts;
}

/// Seeded the way ExportDialog::show() seeds it: engine-derived sample rate,
/// the dialog's own defaults, and the project folder.
Aestra::ExportDialogValues computedDefaults() {
    Aestra::ExportDialogValues live;
    live.sampleRateIndex = 1; // 48000, from the engine
    live.bitDepthIndex = 1;
    live.scopeIndex = 0;
    live.tailInput = "2.0";
    live.outputDirectory = "/projects/song";
    return live;
}

const auto acceptAnyDirectory = [](const std::string&) { return true; };
const auto rejectAnyDirectory = [](const std::string&) { return false; };

} // namespace

int main() {
    using namespace Aestra;

    {
        auto live = computedDefaults();
        expect(!applyStoredExportOptions(live, std::nullopt, dialogCounts(), acceptAnyDirectory),
               "an absent record reports no apply");
        expect(live.sampleRateIndex == 1 && live.bitDepthIndex == 1 && live.scopeIndex == 0,
               "an absent record leaves every computed default");
        expect(live.tailInput == "2.0" && live.outputDirectory == "/projects/song",
               "an absent record leaves the tail text and project folder");
    }
    {
        // Every field expressed: the whole record wins.
        auto live = computedDefaults();
        UIDialogExportOptions stored;
        stored.sampleRateIndex = 3; // 96000
        stored.bitDepthIndex = 2;   // 32-bit float
        stored.scopeIndex = 1;      // loop region
        stored.tailInput = "4.5";
        stored.lastOutputDirectory = "/renders";
        expect(applyStoredExportOptions(live, stored, dialogCounts(), acceptAnyDirectory),
               "a stored record reports apply");
        expect(live.sampleRateIndex == 3, "a stored sample rate beats the engine-derived default");
        expect(live.bitDepthIndex == 2, "a stored bit depth applies");
        expect(live.scopeIndex == 1, "a stored scope applies");
        expect(live.tailInput == "4.5", "a stored tail text applies");
        expect(live.outputDirectory == "/renders", "a stored output directory applies");
    }
    {
        // The schema's documented "unset" sentinel, per field.
        auto live = computedDefaults();
        UIDialogExportOptions stored;
        stored.scopeIndex = 2; // the only expressed field
        expect(applyStoredExportOptions(live, stored, dialogCounts(), acceptAnyDirectory),
               "a partially expressed record still reports apply");
        expect(live.scopeIndex == 2, "the one expressed field applies");
        expect(live.sampleRateIndex == 1 && live.bitDepthIndex == 1, "-1 indices defer to the computed defaults");
        expect(live.tailInput == "2.0", "an empty stored tail text defers to the computed default");
        expect(live.outputDirectory == "/projects/song", "an empty stored directory defers to the project folder");
    }
    {
        // Disk data indexing straight into an option vector: out of range must
        // not reach the dialog, or drawDropdown reads out of bounds.
        auto live = computedDefaults();
        UIDialogExportOptions stored;
        stored.sampleRateIndex = 4; // one past the last option
        stored.bitDepthIndex = 99;  // a future schema with more options
        stored.scopeIndex = -7;     // corrupt
        expect(!applyStoredExportOptions(live, stored, dialogCounts(), acceptAnyDirectory),
               "a record whose every field is out of range reports no apply");
        expect(live.sampleRateIndex == 1 && live.bitDepthIndex == 1 && live.scopeIndex == 0,
               "out-of-range stored indices fall back to the computed defaults");
        expect(!isUsableOptionIndex(4, 4) && !isUsableOptionIndex(-1, 4) && isUsableOptionIndex(3, 4),
               "the index bound is half-open");
    }
    {
        // A remembered folder that no longer exists must not strand the export.
        auto live = computedDefaults();
        UIDialogExportOptions stored;
        stored.lastOutputDirectory = "/mnt/unplugged-drive/renders";
        expect(!applyStoredExportOptions(live, stored, dialogCounts(), rejectAnyDirectory),
               "a vanished stored directory reports no apply");
        expect(live.outputDirectory == "/projects/song",
               "a vanished stored directory falls back to the project folder");
    }
    {
        // Capture is the directory only: the file name follows the project.
        Aestra::ExportDialogValues used;
        used.sampleRateIndex = 0;
        used.bitDepthIndex = 2;
        used.scopeIndex = 1;
        used.tailInput = "1.5";
        used.outputDirectory = "/renders/final";
        const auto record = captureExportOptions(used);
        expect(record.sampleRateIndex == 0 && record.bitDepthIndex == 2 && record.scopeIndex == 1,
               "capture carries every index");
        expect(record.tailInput == "1.5", "capture carries the tail text");
        expect(record.lastOutputDirectory == "/renders/final", "capture carries the directory");
        expect(record.lastOutputDirectory.find(".wav") == std::string::npos, "capture never carries a file name");
    }

    const Tests::ScopedTempDirectory tempDirScope{"ExportDialogPreferences"};
    const auto storePath = (tempDirScope.path() / "export-record.json").string();

    {
        UISurfaceStoreFile store(storePath);
        expect(!store.dialogExportOptions().has_value(), "a fresh store holds no export record");

        Aestra::ExportDialogValues used;
        used.sampleRateIndex = 3;
        used.bitDepthIndex = 2;
        used.scopeIndex = 1;
        used.tailInput = "4.5";
        used.outputDirectory = "/renders";
        expect(store.setDialogExportOptions(captureExportOptions(used)), "the export record saves");
        expect(store.setDialogExportOptions(captureExportOptions(used)),
               "re-exporting identical settings is a no-op rather than a failure");
    }
    {
        // Restart. The record is only a preference if it outlives the process,
        // and setDialogExportOptions keeps a value in memory even when its
        // write fails — so only a reopen proves the write reached disk.
        UISurfaceStoreFile reopened(storePath);
        const auto stored = reopened.dialogExportOptions();
        expect(stored.has_value(), "the export record survives a store reopen");
        if (stored.has_value()) {
            expect(stored->sampleRateIndex == 3 && stored->bitDepthIndex == 2 && stored->scopeIndex == 1,
                   "the reopened record keeps every index");
            expect(stored->tailInput == "4.5" && stored->lastOutputDirectory == "/renders",
                   "the reopened record keeps the tail text and directory");
        }

        // And it applies on the next open, which is the whole point of the step.
        auto live = computedDefaults();
        expect(applyStoredExportOptions(live, stored, dialogCounts(), acceptAnyDirectory),
               "the reopened record applies to a freshly computed dialog");
        expect(live.outputDirectory == "/renders", "the user does not re-choose the output folder after a restart");
    }

    if (g_failures == 0) {
        std::cout << "All ExportDialogPreferences tests passed.\n";
        return 0;
    }
    std::cerr << g_failures << " ExportDialogPreferences test(s) failed.\n";
    return 1;
}
