// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Phase-3 workspace-state roundtrip: the optional viewFocus/pianoRollOpen/
// sequencerOpen UIState fields must survive serialize -> load, and legacy
// files (no workspace keys) must load with the historical defaults (empty
// viewFocus = Timeline, overlays closed) without failing.

#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/TrackManager.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

namespace {

void require(bool cond, const std::string& msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}

std::string serializeWithUI(const std::shared_ptr<Aestra::Audio::TrackManager>& tm,
                            const ProjectSerializer::UIState& ui) {
    auto ser = ProjectSerializer::serialize(tm, 120.0, 0.0, 2, &ui);
    require(ser.ok, "serialize with UIState failed");
    require(!ser.contents.empty(), "serialize produced empty contents");
    return ser.contents;
}

} // namespace

int main() {
    using namespace Aestra;
    using namespace Aestra::Audio;

    const Tests::ScopedTempDirectory tempDirScope{"WorkspaceStateRoundTrip"};
    const auto& tempDir = tempDirScope.path();

    auto makeManager = []() {
        auto tm = std::make_shared<TrackManager>();
        tm->getPlaylistModel().setPatternManager(&tm->getPatternManager());
        return tm;
    };

    // --- Roundtrip: timeline workspace + remembered-open editor flags.
    {
        ProjectSerializer::UIState ui;
        ui.viewFocus = "timeline";
        ui.pianoRollOpen = true;
        ui.sequencerOpen = true;

        const std::string contents = serializeWithUI(makeManager(), ui);
        const auto path = tempDir / "timeline-editors.aes";
        require(ProjectSerializer::writeAtomically(path.string(), contents), "atomic write failed");

        auto tm = makeManager();
        const auto result = ProjectSerializer::load(path.string(), tm);
        require(result.ok, "load failed for timeline editors file");
        require(result.ui.has_value(), "UIState missing from load result");
        require(result.ui->viewFocus == "timeline", "viewFocus did not roundtrip");
        require(result.ui->pianoRollOpen, "pianoRollOpen did not roundtrip");
        require(result.ui->sequencerOpen, "sequencerOpen did not roundtrip");
    }

    // --- Roundtrip: every persisted focus name.
    for (const std::string focusName : {"arsenal", "timeline", "audition", "routingMap"}) {
        ProjectSerializer::UIState ui;
        ui.viewFocus = focusName;
        const std::string contents = serializeWithUI(makeManager(), ui);
        const auto path = tempDir / ("focus-" + focusName + ".aes");
        require(ProjectSerializer::writeAtomically(path.string(), contents), "atomic write failed");

        auto tm = makeManager();
        const auto result = ProjectSerializer::load(path.string(), tm);
        require(result.ok, "load failed for focus name " + focusName);
        require(result.ui.has_value() && result.ui->viewFocus == focusName,
                "focus name " + focusName + " did not roundtrip");
    }

    // --- Legacy compatibility: file with no ui/workspace keys -> defaults.
    {
        // serialize(..., /*uiState=*/nullptr) omits the whole "ui" section —
        // exactly what a pre-phase-3 file looks like.
        auto ser = ProjectSerializer::serialize(makeManager(), 120.0, 0.0, 2, nullptr);
        require(ser.ok, "legacy serialize failed");
        const auto path = tempDir / "legacy-no-ui.aes";
        require(ProjectSerializer::writeAtomically(path.string(), ser.contents), "legacy atomic write failed");

        auto tm = makeManager();
        const auto result = ProjectSerializer::load(path.string(), tm);
        require(result.ok, "legacy file (no ui section) must load cleanly");
        require(!result.ui.has_value() || result.ui->viewFocus.empty(),
                "legacy file must not restore a workspace focus");
        require(!result.ui.has_value() || (!result.ui->pianoRollOpen && !result.ui->sequencerOpen),
                "legacy file must keep overlays closed");
    }

    // --- Legacy phase-3 file: viewFocus "pianoRoll" must degrade, not resurrect.
    {
        // Serialize with the phase-3 protocol string, which the loader no
        // longer recognizes as a workspace focus (the piano roll is now a
        // contextual editor, not a workspace).
        ProjectSerializer::UIState ui;
        ui.viewFocus = "pianoRoll";
        ui.pianoRollOpen = true;
        const std::string contents = serializeWithUI(makeManager(), ui);
        const auto path = tempDir / "legacy-pianoRoll-focus.aes";
        require(ProjectSerializer::writeAtomically(path.string(), contents), "atomic write failed");

        auto tm = makeManager();
        const auto result = ProjectSerializer::load(path.string(), tm);
        require(result.ok, "legacy pianoRoll-focus file must load cleanly");
        require(result.ui.has_value() && result.ui->viewFocus.empty(),
                "legacy pianoRoll focus must be dropped (default Timeline)");
        require(result.ui->pianoRollOpen, "remembered-open editor flag must survive alongside the dropped focus");
    }

    // --- Malformed focus name still restores flags (focus falls back to Timeline).
    {
        ProjectSerializer::UIState ui;
        ui.viewFocus = "notAFocus";
        ui.pianoRollOpen = true;
        const std::string contents = serializeWithUI(makeManager(), ui);
        const auto path = tempDir / "bad-focus.aes";
        require(ProjectSerializer::writeAtomically(path.string(), contents), "atomic write failed");

        auto tm = makeManager();
        const auto result = ProjectSerializer::load(path.string(), tm);
        require(result.ok, "file with malformed focus must load cleanly");
        require(!result.ui.has_value() || result.ui->viewFocus.empty(),
                "malformed focus must be dropped (default Timeline)");
        // The app restore path (AestraApp::applyUIState) is not headless;
        // the serializer contract is that the flag still survives alongside a
        // malformed focus, so the caller can apply it with the Timeline fallback.
        require(result.ui.has_value() && result.ui->pianoRollOpen,
                "pianoRollOpen must survive alongside a malformed focus");
    }

    std::cout << "[PASS] WorkspaceStateRoundTripTest\n";
    return 0;
}
