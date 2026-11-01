// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PresetProjectRoundTripTest — V83-S2 slice 3 (FD-72): "This project only" presets travel inside the project file.
//   1. presets kept in the project come back after save and load, marked as the project's, sound intact;
//   2. a project with none writes no "presets" section, and loads with none;
//   3. loading another project drops the last one's presets (they never leak between projects);
//   4. a damaged entry is skipped and the rest still load.

#include "../../Source/Core/ProjectPresetsJson.h"
#include "../../Source/Core/ProjectSerializer.h"
#include "../Support/TestTempDirectory.h"
#include "Models/TrackManager.h"

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

using namespace Aestra;
using namespace Aestra::Audio;
using namespace Aestra::Audio::Presets;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    if (!ok) {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

std::shared_ptr<TrackManager> makeProject() {
    auto tm = std::make_shared<TrackManager>();
    tm->getPlaylistModel().setPatternManager(&tm->getPatternManager());
    tm->getPlaylistModel().setBPM(120.0);
    tm->addChannel("Lead Vox");
    return tm;
}

Preset preset(const char* name, const char* plugin, float mix, std::vector<uint8_t> state = {}) {
    Preset p;
    p.name = name;
    p.pluginId = plugin;
    p.pluginName = "Test";
    p.category = "Plates";
    p.tags = {"vocal"};
    p.params = {{7, "Mix", mix}};
    p.state = std::move(state);
    p.source = PresetSource::Project;
    return p;
}

std::string readAll(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

} // namespace

int main() {
    const Tests::ScopedTempDirectory dir{"PresetProjectRoundTrip"};
    const auto withPath = (dir.path() / "with.aes").string();
    const auto withoutPath = (dir.path() / "without.aes").string();

    auto tm1 = makeProject();
    tm1->projectPresets().push_back(preset("Night Drive Verb", "com.Aestrastudios.verb", 0.25f, {1, 2, 3}));
    tm1->projectPresets().push_back(preset("Night Drive Comp", "com.Aestrastudios.comp", 0.75f));
    auto ser = ProjectSerializer::serialize(tm1, 120.0, 0.0, 2);
    check(ser.ok && ProjectSerializer::writeAtomically(withPath, ser.contents), "project with presets saves");

    auto tm2 = makeProject();
    check(ProjectSerializer::load(withPath, tm2).ok, "it loads");
    const auto& kept = tm2->projectPresets();
    check(kept.size() == 2, "both presets come back");
    if (kept.size() == 2) {
        check(kept[0].name == "Night Drive Verb" && kept[0].pluginId == "com.Aestrastudios.verb", "name and plugin");
        check(kept[0].source == PresetSource::Project, "marked as the project's");
        check(kept[0].params.size() == 1 && kept[0].params[0].name == "Mix" && kept[0].params[0].value == 0.25f,
              "the sound survives");
        check(kept[0].state == std::vector<uint8_t>({1, 2, 3}), "a plugin's own state survives");
        check(kept[0].category == "Plates" && kept[0].tags.size() == 1, "category and tags survive");
    }

    auto tm3 = makeProject();
    auto ser3 = ProjectSerializer::serialize(tm3, 120.0, 0.0, 2);
    check(ser3.ok && ProjectSerializer::writeAtomically(withoutPath, ser3.contents), "project without presets saves");
    check(readAll(withoutPath).find("\"presets\"") == std::string::npos, "no presets section when there are none");

    // Loading another project into the same model drops the last one's presets.
    check(ProjectSerializer::load(withoutPath, tm2).ok && tm2->projectPresets().empty(),
          "presets never leak into the next project");

    // A damaged entry is skipped; the good one still loads.
    JSON list = projectPresetsToJson({preset("Good", "com.Aestrastudios.verb", 0.5f)});
    list.push(JSON(std::string("not a preset")));
    JSON bad = JSON::object();
    bad.set("aestraPreset", JSON(1.0));
    bad.set("name", JSON(std::string("No plugin")));
    list.push(bad);
    const auto read = projectPresetsFromJson(list);
    check(read.size() == 1 && read[0].name == "Good", "damaged entries are skipped, the rest load");

    if (g_failures) {
        std::printf("%d PresetProjectRoundTrip check(s) FAILED\n", g_failures);
        return 1;
    }
    std::printf("All PresetProjectRoundTrip checks passed\n");
    return 0;
}
