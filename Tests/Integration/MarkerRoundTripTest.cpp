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
#include <filesystem>
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


// Save a project and load it into a fresh TrackManager, the way Save then Open does.
std::shared_ptr<TrackManager> roundTrip(const std::shared_ptr<TrackManager>& source, const std::filesystem::path& file) {
    auto ser = ProjectSerializer::serialize(source, 120.0, 0.0, 2);
    if (!ser.ok || !ProjectSerializer::writeAtomically(file.string(), ser.contents)) return nullptr;
    auto loaded = std::make_shared<TrackManager>();
    return ProjectSerializer::load(file.string(), loaded).ok ? loaded : nullptr;
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


    // --- QA: persistence ---
    {
        // 50 markers, with names that stress the file format: quotes, backslashes, a newline, a tab, emoji, non-Latin.
        auto tm = std::make_shared<TrackManager>();
        auto& h = tm->getCommandHistory();
        const char* nasty[] = {"Say \"hi\"", "back\\slash", "two\nlines", "tab\there", "<b>&amp;</b>",
                               "\xF0\x9F\x8E\xB5 song", "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E", "caf\xC3\xA9"};
        for (int i = 0; i < 50; ++i) {
            const std::string name = i < 8 ? std::string(nasty[i]) : "Section " + std::to_string(i);
            h.pushAndExecute(std::make_shared<AddMarkerCommand>(*tm, i * 4.0, name));
        }
        auto back = roundTrip(tm, dir.path() / "fifty.aes");
        check(back != nullptr, "FiftyMarkerProjectRoundTrips");
        if (back) {
            const auto& a = tm->markers().markers();
            const auto& b = back->markers().markers();
            bool same = a.size() == b.size();
            for (size_t i = 0; same && i < a.size(); ++i)
                same = a[i].id == b[i].id && a[i].name == b[i].name && a[i].beat == b[i].beat;
            check(b.size() == 50, "AllFiftyMarkersComeBack");
            check(same, "NamesWithQuotesNewlinesAndUnicodeSurviveByteForByte");
        }
    }
    {
        // Rename, then save at once; delete, then save at once; undo, then save: the file holds what the UI showed.
        auto tm = std::make_shared<TrackManager>();
        auto& h = tm->getCommandHistory();
        auto a = std::make_shared<AddMarkerCommand>(*tm, 4.0, "A");
        auto b = std::make_shared<AddMarkerCommand>(*tm, 8.0, "B");
        h.pushAndExecute(a);
        h.pushAndExecute(b);
        h.pushAndExecute(std::make_shared<RenameMarkerCommand>(*tm, a->markerId(), "A renamed"));
        auto r1 = roundTrip(tm, dir.path() / "renamed.aes");
        check(r1 && r1->markers().find(a->markerId()) && r1->markers().find(a->markerId())->name == "A renamed",
              "RenameThenSaveImmediatelyKeepsTheNewName");
        h.pushAndExecute(std::make_shared<DeleteMarkerCommand>(*tm, b->markerId()));
        auto r2 = roundTrip(tm, dir.path() / "deleted.aes");
        check(r2 && r2->markers().count() == 1 && r2->markers().find(b->markerId()) == nullptr,
              "DeleteThenSaveImmediatelyDropsTheMarker");
        h.undo(); // brings B back
        auto r3 = roundTrip(tm, dir.path() / "undone.aes");
        check(r3 && r3->markers().count() == 2 && r3->markers().find(b->markerId()) != nullptr,
              "UndoThenSaveWritesTheUndoneState");
    }
    {
        // An older file with two markers on one beat (the model used to allow it) loads with both, in a stable order.
        MarkerModel model;
        markersFromJson(JSON::parse("[{\"id\":1,\"beat\":8,\"name\":\"One\"},{\"id\":2,\"beat\":8,\"name\":\"Two\"}]"), model);
        check(model.count() == 2 && model.markers()[0].name == "One" && model.markers()[1].name == "Two",
              "SameBeatMarkersInAnOldFileBothLoadInOrder");
    }
    {
        // After a load, a new unnamed marker must not reuse a default name already in the project.
        MarkerModel model;
        markersFromJson(JSON::parse("[{\"id\":1,\"beat\":0,\"name\":\"Marker 1\"},{\"id\":2,\"beat\":4,\"name\":\"Marker 2\"}]"), model);
        const uint64_t fresh = model.add(8.0);
        const Marker* m = model.find(fresh);
        check(m != nullptr && m->name != "Marker 1" && m->name != "Marker 2",
              "ANewDefaultNameAfterLoadDoesNotRepeatOne");
        check(fresh > 2, "ANewIdAfterLoadDoesNotCollide");
    }

    if (g_failures) {
        std::printf("%d MarkerRoundTrip test(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All MarkerRoundTrip tests passed\n");
    return 0;
}
