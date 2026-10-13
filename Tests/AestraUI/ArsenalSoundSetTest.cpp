// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Sound sets (V81-AR2, FD-46): a set is a library collection of files and folders from anywhere on disk. A row that
// uses one walks all of its sounds; a member that is gone is reported, never skipped silently.

#include "ArsenalSoundSet.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using Aestra::ArsenalSoundSet;
using Aestra::ArsenalSoundStrip;
namespace fs = std::filesystem;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
void touch(const fs::path& p) { fs::create_directories(p.parent_path()); std::ofstream(p) << "x"; }
} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / "aestra_sound_set_test";
    fs::remove_all(root);
    touch(root / "drums" / "kick_01.wav");
    touch(root / "drums" / "Snare_02.wav");
    touch(root / "drums" / "readme.txt");
    touch(root / "elsewhere" / "Hat_closed.flac");
    touch(root / "elsewhere" / "Hat_open.flac");
    touch(root / "loose" / "clap.mp3");

    // A set reaches a folder's audio and loose files from other places, sorted by name, each once.
    auto r = ArsenalSoundSet::resolve({(root / "drums").string(), (root / "loose" / "clap.mp3").string(), (root / "elsewhere" / "Hat_open.flac").string()});
    check(r.files.size() == 4, "a folder and two loose files make four sounds");
    check(r.missing.empty(), "nothing is missing");
    check(ArsenalSoundStrip::displayName(r.files.front()) == "clap" && ArsenalSoundStrip::displayName(r.files.back()) == "Snare_02", "sorted by name across folders");

    // A text file is not a sound; a duplicate member counts once.
    r = ArsenalSoundSet::resolve({(root / "drums" / "readme.txt").string(), (root / "drums").string(), (root / "drums" / "kick_01.wav").string()});
    check(r.files.size() == 2, "readme.txt is not a sound, and kick_01 is listed once");

    // A member that has gone is reported and the rest still resolve.
    r = ArsenalSoundSet::resolve({(root / "drums").string(), (root / "gone").string(), (root / "elsewhere" / "nope.wav").string()});
    check(r.files.size() == 2 && r.missing.size() == 2, "two members are missing and two sounds still resolve");

    // An empty set resolves to nothing.
    check(ArsenalSoundSet::resolve({}).files.empty(), "an empty set has no sounds");

    // Stepping through a set: a sound outside it is stepped into from the ends; inside it, it wraps.
    const auto& list = ArsenalSoundSet::resolve({(root / "elsewhere").string(), (root / "drums").string()}).files;
    check(ArsenalSoundStrip::stepped(list, -1, 1, true) == list.front() && ArsenalSoundStrip::stepped(list, -1, -1, true) == list.back() && ArsenalSoundStrip::stepped(list, -1, 1).empty(), "from outside the set, Next enters at the start and Prev at the end");
    check(ArsenalSoundStrip::stepped(list, static_cast<int>(list.size()) - 1, 1) == list.front(), "inside the set, stepping wraps");

    // The strip walks the set's list, and goes back to the folder when told to.
    ArsenalSoundStrip strip;
    strip.follow((root / "drums" / "kick_01.wav").string());
    check(strip.list().size() == 2, "the strip starts on the sound's own folder");
    strip.useList("Trap kit", list);
    check(strip.isSet() && strip.setName() == "Trap kit" && strip.list().size() == list.size(), "the strip walks the set");
    strip.follow((root / "elsewhere" / "Hat_open.flac").string());
    check(strip.index() >= 0 && strip.neighbour(1) != strip.neighbour(-1), "following a sound inside the set finds its place");
    strip.useFolder();
    strip.follow((root / "drums" / "kick_01.wav").string());
    check(!strip.isSet() && strip.list().size() == 2, "back to the folder");

    fs::remove_all(root);
    std::cout << (g_failures ? "FAILED\n" : "ArsenalSoundSetTest passed\n");
    return g_failures ? 1 : 0;
}
