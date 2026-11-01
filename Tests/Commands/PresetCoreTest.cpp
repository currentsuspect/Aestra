// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// PresetCoreTest — V83-S2 (FD-72): one preset system for every plugin.
//   1. capture, file round trip and apply restore a built-in plugin's sound; bypass is never touched;
//   2. "N changes" counts the knobs that moved; a damaged or foreign file is refused, not half-loaded;
//   3. the library saves to the producer's folder, refuses a duplicate name unless updating, and never deletes
//      outside it; factory presets are listed first and are not files;
//   4. Save's suggested name ("Lead Vox" + "Vocal Plate" = "Lead Vox Plate") and track-first ordering;
//   5. ApplyPresetCommand is one undo back to the sound before;
//   6. the session behind the bar: pick and step, browse (Esc puts back, Enter is one undo), A/B, save to Yours or
//      to the project, and undo puts the right name back on the bar.

#include "Commands/ApplyPresetCommand.h"
#include "Plugin/BuiltInPlugins.h"
#include "Plugin/InternalPluginRegistry.h"
#include "Presets/Preset.h"
#include "Presets/PresetLibrary.h"
#include "Presets/PresetSession.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

using namespace Aestra::Audio;
using namespace Aestra::Audio::Presets;
namespace fs = std::filesystem;

namespace {

int g_failures = 0;
int g_checks = 0;

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::cerr << "FAIL: " << what << "\n";
    }
}

bool near(float a, float b) { return std::fabs(a - b) < 1.0e-4f; }

PluginInstancePtr makeVerb() {
    BuiltInPlugins::registerCoreBuiltIns();
    auto p = InternalPluginRegistry::instance().createInstance(BuiltInPlugins::verbInfo().id);
    if (p) p->initialize(48000.0, 512);
    return p;
}

const PluginParameter* paramNamed(const IPluginInstance& plugin, const std::string& name,
                                  std::vector<PluginParameter>& storage) {
    storage = plugin.getParameters();
    for (const auto& p : storage)
        if (p.name == name) return &p;
    return nullptr;
}

uint32_t idOf(const IPluginInstance& plugin, const std::string& name) {
    std::vector<PluginParameter> s;
    const auto* p = paramNamed(plugin, name, s);
    return p ? p->id : 0xFFFFFFFFu;
}

uint32_t bypassId(const IPluginInstance& plugin) {
    for (const auto& p : plugin.getParameters())
        if (p.isBypass) return p.id;
    return 0xFFFFFFFFu;
}

fs::path scratchDir(const char* leaf) {
    const fs::path dir = fs::temp_directory_path() / ("aestra-preset-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())) / leaf;
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    return dir;
}

void testCaptureApplyRoundTrip() {
    auto verb = makeVerb();
    check(verb != nullptr, "the Verb plugin is created");
    if (!verb) return;
    const uint32_t decay = idOf(*verb, "Decay"), mix = idOf(*verb, "Mix"), byp = bypassId(*verb);
    check(decay != 0xFFFFFFFFu && mix != 0xFFFFFFFFu && byp != 0xFFFFFFFFu, "Verb has Decay, Mix and a bypass");

    verb->setParameter(decay, 0.81f);
    verb->setParameter(mix, 0.22f);
    const Preset saved = capture(*verb, "  Long Tail  ");
    check(saved.name == "Long Tail", "capture trims the name");
    check(saved.pluginId == BuiltInPlugins::verbInfo().id, "capture records the plugin id");
    check(saved.state.empty(), "a built-in plugin's preset is its params, no opaque state");
    bool hasBypass = false;
    for (const auto& p : saved.params) hasBypass |= p.id == byp;
    check(!hasBypass, "bypass is not part of a preset");

    const auto reread = fromJson(toJson(saved));
    check(reread.has_value(), "the preset file reads back");
    if (!reread) return;
    check(reread->name == "Long Tail" && reread->params.size() == saved.params.size(), "name and params survive");

    verb->setParameter(decay, 0.10f);
    verb->setParameter(mix, 0.90f);
    verb->setParameter(byp, 1.0f); // the producer has the plugin bypassed
    check(countChanges(*verb, *reread) == 2, "two knobs moved = 2 changes");
    check(apply(*verb, *reread), "apply succeeds on its own plugin");
    check(near(verb->getParameter(decay), 0.81f) && near(verb->getParameter(mix), 0.22f), "apply restores the sound");
    check(near(verb->getParameter(byp), 1.0f), "apply leaves bypass as the producer had it");
    check(countChanges(*verb, *reread) == 0, "after apply nothing has changed");

    Preset foreign = *reread;
    foreign.pluginId = "com.example.other";
    check(!apply(*verb, foreign), "a preset for another plugin is refused");
}

void testFilesRefused() {
    check(!fromJson("not json").has_value(), "garbage is not a preset");
    check(!fromJson(R"({"aestraPreset":99,"name":"x","plugin":{"id":"a"},"params":[{"name":"Mix","value":0.5}]})"),
          "a newer format is refused");
    check(!fromJson(R"({"aestraPreset":1,"plugin":{"id":"a"},"params":[{"name":"Mix","value":0.5}]})"),
          "a preset with no name is refused");
    check(!fromJson(R"({"aestraPreset":1,"name":"x","plugin":{"id":"a"},"params":[],"state":"@@@@"})"),
          "a damaged state is refused, not loaded as empty");
    const auto blob = fromJson(R"({"aestraPreset":1,"name":"x","plugin":{"id":"a"},"state":"AAEC/w=="})");
    check(blob && blob->state == std::vector<uint8_t>({0x00, 0x01, 0x02, 0xFF}), "a state-only preset decodes");
    if (blob) {
        const auto again = fromJson(toJson(*blob));
        check(again && again->state == blob->state, "state survives a round trip");
    }

    // Factory tables name params without ids; a misspelt name must not land on parameter 0.
    auto verb = makeVerb();
    if (!verb) return;
    const float before = verb->getParameter(0);
    Preset typo;
    typo.name = "Typo";
    typo.pluginId = verb->getInfo().id;
    typo.params.push_back({PresetParam::kNoId, "Dekay", 0.99f});
    check(!apply(*verb, typo), "a preset whose only param is unknown applies nothing");
    check(near(verb->getParameter(0), before), "an unknown name does not fall back to id 0");
}

void testLibrary() {
    const fs::path root = scratchDir("library");
    PresetLibrary lib(root);

    Preset factory;
    factory.name = "Vocal Plate";
    factory.params.push_back({PresetParam::kNoId, "Mix", 0.2f});
    registerFactoryPresets("com.test.verb", {factory});

    Preset mine;
    mine.name = "Lead Vox Plate";
    mine.pluginId = "com.test.verb";
    mine.pluginName = "Test Verb";
    mine.params.push_back({3, "Mix", 0.3f});

    std::string error;
    const auto written = lib.saveUser(mine, false, &error);
    check(written.has_value(), "save writes a new preset: " + error);
    if (!written) return;
    check(written->source == PresetSource::User && fs::exists(written->path), "the file exists, marked Yours");
    check(fs::path(written->path).parent_path().filename() == "Test Verb", "it lands in the plugin's folder");

    check(!lib.saveUser(mine, false, &error).has_value(), "the same name is refused without Update");
    mine.params[0].value = 0.4f;
    const auto updated = lib.saveUser(mine, true, &error);
    check(updated && updated->path == written->path, "Update replaces the same file");

    // A file a friend sent, dropped anywhere under the folder, is found by what it says.
    Preset shared = mine;
    shared.name = "From A Friend";
    {
        std::ofstream(root / "loose.aepreset") << toJson(shared);
        std::ofstream(root / "notes.txt") << "not a preset";
    }
    const auto list = lib.list("com.test.verb");
    check(list.size() == 3, "factory + two of yours are listed (got " + std::to_string(list.size()) + ")");
    if (list.size() == 3) {
        check(list[0].source == PresetSource::Factory && list[0].name == "Vocal Plate", "factory comes first");
        check(list[1].name == "From A Friend" && list[2].name == "Lead Vox Plate", "yours follow, by name");
    }
    check(lib.list("com.test.other").empty(), "another plugin's presets are not listed");

    check(!lib.removeUser(list.empty() ? Preset{} : list[0]), "a factory preset cannot be deleted");
    Preset outside = *written;
    outside.path = (root.parent_path() / "elsewhere.aepreset").string();
    std::ofstream(outside.path) << "x";
    check(!lib.removeUser(outside), "nothing outside the presets folder is deleted");
    check(fs::exists(outside.path), "the outside file is still there");
    check(lib.removeUser(*written) && !fs::exists(written->path), "your own preset is deleted");

    std::error_code ec;
    fs::remove_all(root.parent_path(), ec);
}

void testSuggestAndOrder() {
    check(suggestName("Lead Vox", "Vocal Plate", {}) == "Lead Vox Plate", "Lead Vox + Vocal Plate");
    check(suggestName("Drums", "Big Hall", {}) == "Drums Big Hall", "Drums + Big Hall");
    check(suggestName("", "Big Hall", {}) == "Big Hall", "no track name keeps the preset's");
    check(suggestName("Keys", "", {}) == "Keys", "no preset gives the track name");
    check(suggestName("", "", {}) == "My Preset", "nothing to go on");
    check(suggestName("Lead Vox", "Vocal Plate", {"lead vox plate"}) == "Lead Vox Plate 2", "a taken name gets 2");
    check(suggestName("Lead Vox", "Vocal Plate", {"Lead Vox Plate", "Lead Vox Plate 2"}) == "Lead Vox Plate 3",
          "then 3");

    auto named = [](const char* n, const char* cat = "") {
        Preset p;
        p.name = n;
        p.category = cat;
        return p;
    };
    const auto ordered = orderForTrack({named("Big Hall"), named("Snare Room"), named("Vocal Plate"),
                                        named("Wide", "Vocals"), named("Drum Bus")},
                                       "Lead Vox");
    check(ordered.size() == 5 && ordered[0].name == "Vocal Plate" && ordered[1].name == "Wide",
          "on a vocal track, vocal presets come first");
    check(ordered.size() == 5 && ordered[2].name == "Big Hall" && ordered[4].name == "Drum Bus",
          "the rest keep their order, nothing hidden");
}

void testCommandIsOneUndo() {
    auto verb = makeVerb();
    if (!verb) return;
    const uint32_t decay = idOf(*verb, "Decay");
    verb->setParameter(decay, 0.30f);
    const Preset before = capture(*verb, "before");

    Preset target = before;
    for (auto& p : target.params)
        if (p.name == "Decay") p.value = 0.90f;

    // Browsing auditioned it live; Enter keeps it and records one step.
    apply(*verb, target);
    int applied = 0;
    ApplyPresetCommand cmd(verb, before, target, "Load Preset \"Long\"", [&](bool) { ++applied; });
    cmd.undo();
    check(near(verb->getParameter(decay), 0.30f), "undo returns the sound before the browse");
    cmd.redo();
    check(near(verb->getParameter(decay), 0.90f), "redo brings the kept preset back");
    check(applied == 2, "the editor is told each time");
    check(cmd.getName() == "Load Preset \"Long\"" && cmd.changesProjectState(), "named and dirties the project");

    std::weak_ptr<IPluginInstance> gone;
    {
        auto temp = makeVerb();
        gone = temp;
    }
    ApplyPresetCommand orphan(gone, before, target, "");
    orphan.undo(); // the plugin was removed: nothing to do, nothing to crash
    check(orphan.getName() == "Load Preset", "default label");
}


void testSession() {
    auto verb = makeVerb();
    if (!verb) return;
    const std::string id = verb->getInfo().id;
    const uint32_t decay = idOf(*verb, "Decay");
    const fs::path root = scratchDir("session");
    PresetLibrary lib(root);

    Preset plate;
    plate.name = "Vocal Plate";
    plate.category = "Plates";
    plate.tags = {"vocal", "short"};
    plate.params = {{PresetParam::kNoId, "Decay", 0.40f}};
    Preset hall;
    hall.name = "Big Hall";
    hall.params = {{PresetParam::kNoId, "Decay", 0.95f}};
    Preset room;
    room.name = "Vocal Room";
    room.params = {{PresetParam::kNoId, "Decay", 0.20f}};
    registerFactoryPresets(id, {hall, plate, room});

    std::vector<std::shared_ptr<Aestra::Audio::ICommand>> history;
    PresetSession s(verb, lib, "Lead Vox", [&](std::shared_ptr<Aestra::Audio::ICommand> c) { history.push_back(c); });
    int redraws = 0;
    s.setOnSoundChanged([&] { ++redraws; });
    check(s.presets().size() == 3 && s.presets()[0].name == "Vocal Plate" && s.presets()[1].name == "Vocal Room",
          "on Lead Vox the vocal presets come first");
    check(!s.current() && s.changes() == 0, "nothing picked yet");
    check(s.suggestedName() == "Lead Vox Verb", "with no preset, Save names the plugin: " + s.suggestedName());
    check(!s.canSaveToProject(), "no project presets until the project can keep them");

    verb->setParameter(decay, 0.55f);
    check(s.pick(0) && s.currentName() == "Vocal Plate" && near(verb->getParameter(decay), 0.40f), "pick loads");
    check(history.size() == 1, "a pick is one undo");
    verb->setParameter(decay, 0.47f);
    check(s.changes() == 1, "one knob moved = 1 change");
    history.back()->undo();
    check(near(verb->getParameter(decay), 0.55f) && !s.current(), "undo returns the sound and the bar to before");
    history.back()->redo();
    check(s.currentName() == "Vocal Plate", "redo puts the name back");

    check(s.step(+1) && s.currentName() == "Vocal Room", "› steps to the next");
    check(s.step(+1) && s.currentName() == "Big Hall", "› again");
    check(s.step(+1) && s.currentName() == "Vocal Plate", "› wraps around");
    check(s.step(-1) && s.currentName() == "Big Hall", "‹ wraps back");
    const size_t afterSteps = history.size();

    // Browse: Esc puts back, records nothing.
    s.beginBrowse();
    s.audition(0);
    s.audition(1);
    check(near(verb->getParameter(decay), 0.20f), "each browse step is heard live");
    s.cancelBrowse();
    check(near(verb->getParameter(decay), 0.95f) && s.currentName() == "Big Hall", "Esc puts back what was there");
    check(history.size() == afterSteps, "a cancelled browse records nothing");

    // Browse: Enter keeps, one undo back to before the browse.
    s.beginBrowse();
    s.audition(0);
    s.audition(1);
    check(s.keepBrowse() && s.currentName() == "Vocal Room", "Enter keeps the one heard");
    check(history.size() == afterSteps + 1, "a kept browse is one undo however many were heard");
    history.back()->undo();
    check(near(verb->getParameter(decay), 0.95f) && s.currentName() == "Big Hall", "undo goes to before the browse");
    history.back()->redo();

    // A/B.
    check(s.beginCompare() && s.activeSlot() == 0, "compare starts on A");
    s.flip();
    check(s.activeSlot() == 1, "flip hears B");
    verb->setParameter(decay, 0.70f); // shape B
    s.flip();
    check(near(verb->getParameter(decay), 0.20f), "A is as it was");
    s.flip();
    check(near(verb->getParameter(decay), 0.70f), "B kept its edit");
    const size_t beforeKeep = history.size();
    check(s.keepActive() && !s.comparing(), "keep B ends comparing");
    check(history.size() == beforeKeep + 1, "keeping B is one undo");
    history.back()->undo();
    check(near(verb->getParameter(decay), 0.20f), "undo returns A");
    history.back()->redo();
    s.beginCompare();
    s.flip();
    verb->setParameter(decay, 0.11f);
    s.endCompare();
    check(near(verb->getParameter(decay), 0.70f) && !s.comparing(), "ending without keeping returns to A");

    // Save.
    s.pick(0); // Vocal Plate, a factory preset
    verb->setParameter(decay, 0.44f);
    check(!s.canUpdateCurrent(), "a factory preset is never updated");
    check(s.suggestedName() == "Lead Vox Plate", "Save offers Lead Vox Plate, got " + s.suggestedName());
    std::string error;
    const auto mine = s.save(s.suggestedName(), PresetSession::Place::Yours, false, &error);
    check(mine && mine->source == PresetSource::User && fs::exists(mine->path), "saved to Yours: " + error);
    check(mine && mine->category == "Plates" && mine->tags.size() == 2, "it keeps the category and tags it came from");
    check(s.currentName() == "Lead Vox Plate" && s.canUpdateCurrent(), "now the bar shows yours, and Update is offered");
    check(s.presets().size() == 4, "the list has it");

    std::vector<Preset> projectKept;
    s.setProjectPresets([&](const std::string&) { return projectKept; },
                        [&](const Preset& p) {
                            projectKept.push_back(p);
                            return true;
                        });
    const auto local = s.save("Night Drive Verb", PresetSession::Place::Project, false, &error);
    check(local && local->source == PresetSource::Project && projectKept.size() == 1, "saved to this project only");
    check(!fs::exists(root / "Aestra Verb" / "Night Drive Verb.aepreset"), "a project preset is not a file in Yours");
    check(s.presets().size() == 5, "the list has the project's preset too");
    check(redraws > 0, "the editor was told to redraw");

    registerFactoryPresets(id, {});
    std::error_code ec;
    fs::remove_all(root.parent_path(), ec);
}

} // namespace

int main() {
    testCaptureApplyRoundTrip();
    testFilesRefused();
    testLibrary();
    testSuggestAndOrder();
    testCommandIsOneUndo();
    testSession();
    std::cout << "PresetCoreTest: " << (g_checks - g_failures) << "/" << g_checks << " checks passed\n";
    return g_failures == 0 ? 0 : 1;
}
