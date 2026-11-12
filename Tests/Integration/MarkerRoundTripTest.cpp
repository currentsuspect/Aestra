// © 2026 Aestra Studios — All Rights Reserved.
//
// MarkerRoundTripTest — markers survive save and load, and a project without them loads without them.
//
// Two properties matter more than the happy path:
//   1. A file with no "markers" section must not inherit the markers of whatever project was open before the load.
//   2. A marker with a malformed position or a duplicate id must be skipped, never crash the load or shadow a live one.

#include "../../Source/Core/MarkerJson.h"
#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Commands/MarkerCommands.h"
#include "Models/TrackManager.h"

#include <cstdio>
#include <memory>
#include <string>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

} // namespace

int main() {
    const Aestra::Tests::ScopedTempDirectory dir{"MarkerRoundTrip"};

    // --- markers survive a save/load ---
    auto tm1 = std::make_shared<TrackManager>();
    auto& history = tm1->getCommandHistory();
    history.pushAndExecute(std::make_shared<AddMarkerCommand>(*tm1, 0.0, "Intro"));
    history.pushAndExecute(std::make_shared<AddMarkerCommand>(*tm1, 16.0, "Verse"));
    history.pushAndExecute(std::make_shared<AddMarkerCommand>(*tm1, 48.0, "Hook"));
    const uint64_t verseId = tm1->markers().markers()[1].id;

    const auto path = dir.path() / "markers.aes";
    auto ser = ProjectSerializer::serialize(tm1, 120.0, 0.0, 2);
    check(ser.ok && ProjectSerializer::writeAtomically(path.string(), ser.contents), "project saves");

    auto tm2 = std::make_shared<TrackManager>();
    auto load = ProjectSerializer::load(path.string(), tm2);
    check(load.ok, "project loads");
    const auto& markers = tm2->markers().markers();
    check(markers.size() == 3, "AllMarkersComeBack");
    check(markers.size() == 3 && markers[0].name == "Intro" && markers[1].name == "Verse" && markers[2].name == "Hook" &&
              markers[1].beat == 16.0,
          "MarkerOrderBeatAndNamesSurvive");
    check(markers.size() == 3 && markers[1].id == verseId, "MarkerIdsSurvive");
    check(tm2->markers().nextAfter(16.0, false) != nullptr && tm2->markers().nextAfter(16.0, false)->name == "Hook",
          "NavigationWorksAfterLoad");

    // --- loading a project with no markers clears the previous project's ---
    const auto bare = dir.path() / "bare.aes";
    auto bareTm = std::make_shared<TrackManager>();
    auto bareSer = ProjectSerializer::serialize(bareTm, 120.0, 0.0, 2);
    check(bareSer.ok && ProjectSerializer::writeAtomically(bare.string(), bareSer.contents), "bare project saves");

    auto tm3 = std::make_shared<TrackManager>();
    tm3->markers().add(4.0, "LeftOver");
    auto bareLoad = ProjectSerializer::load(bare.string(), tm3);
    check(bareLoad.ok, "bare project loads");
    check(tm3->markers().empty(), "LoadingAProjectWithNoMarkersClearsTheOldOnes");

    // --- a damaged section is skipped, not fatal ---
    // Checked against the reader directly: rewriting the saved JSON would break the file's integrity hash, so the
    // malformed cases are fed to markersFromJson the way the loader feeds them.
    {
        const JSON parsed = JSON::parse(
            "[{\"id\":901,\"beat\":8,\"name\":\"Kept\"},"
            "{\"id\":902,\"beat\":-4,\"name\":\"Negative\"},"
            "{\"id\":903,\"name\":\"NoBeat\"},"
            "{\"id\":901,\"beat\":12,\"name\":\"Duplicate\"},"
            "\"junk\",{\"id\":904,\"beat\":1e300,\"name\":\"Absurd\"}]");
        MarkerModel model;
        markersFromJson(parsed, model);
        const auto& kept = model.markers();
        check(kept.size() == 1 && kept[0].name == "Kept" && kept[0].id == 901,
              "OnlyTheValidMarkerSurvives");
    }
    {
        MarkerModel model;
        model.add(4.0, "Existing");
        markersFromJson(JSON::parse("null"), model);
        check(model.empty(), "ANonArraySectionClearsRatherThanKeeps");
    }

    if (g_failures) {
        std::printf("%d MarkerRoundTrip test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All MarkerRoundTrip tests passed\n");
    return 0;
}
