// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 5, "swap the sound while it plays": an open sampler row steps to the previous or next sound in the
// same folder. The neighbours are the audio files beside the current one, sorted by name, and stepping wraps.

#include "ArsenalSoundStrip.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using Aestra::ArsenalSoundStrip;
namespace fs = std::filesystem;

namespace {
int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok) ++g_failures;
}
void touch(const fs::path& p) { std::ofstream(p) << "x"; }
} // namespace

int main() {
    const fs::path dir = fs::temp_directory_path() / "aestra_sound_strip_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "sub");
    for (const char* n : {"Snare_02.wav", "kick_01.wav", "Kick_10.flac", "hat.mp3", "notes.txt", "cover.png"}) touch(dir / n);
    touch(dir / "sub" / "deeper.wav");

    std::cout << "[1] neighbours are the audio files beside the current one\n";
    const auto list = ArsenalSoundStrip::siblings((dir / "kick_01.wav").string());
    check(list.size() == 4, "four audio files (text, images and subfolders left out): " + std::to_string(list.size()));
    check(list.size() == 4 && fs::path(list[0]).filename() == "hat.mp3" && fs::path(list[1]).filename() == "kick_01.wav", "sorted by name, ignoring case");
    check(ArsenalSoundStrip::indexOf(list, (dir / "Snare_02.wav").string()) == 3, "the current sound's place in the list");
    check(ArsenalSoundStrip::indexOf(list, (dir / "nope.wav").string()) == -1, "a sound that is not in the folder is not found");
    check(ArsenalSoundStrip::siblings((dir / "missing" / "x.wav").string()).empty(), "a missing folder lists nothing");

    std::cout << "[2] stepping wraps and has nothing to do with one sound\n";
    check(fs::path(ArsenalSoundStrip::stepped(list, 3, 1)).filename() == "hat.mp3", "next from the last wraps to the first");
    check(fs::path(ArsenalSoundStrip::stepped(list, 0, -1)).filename() == "Snare_02.wav", "previous from the first wraps to the last");
    check(fs::path(ArsenalSoundStrip::stepped(list, 1, 1)).filename() == "Kick_10.flac", "next steps one place");
    check(ArsenalSoundStrip::stepped({"a.wav"}, 0, 1).empty(), "a folder with one sound has nowhere to step");
    check(ArsenalSoundStrip::stepped(list, -1, 1).empty(), "an unknown current sound steps nowhere");

    std::cout << "[3] the strip follows the unit's sound and answers clicks\n";
    ArsenalSoundStrip strip;
    strip.follow((dir / "kick_01.wav").string());
    check(strip.index() == 1 && strip.list().size() == 4, "following a path scans its folder once");
    check(fs::path(strip.neighbour(1)).filename() == "Kick_10.flac", "neighbour(+1) is the next sound");
    const AestraUI::NUIRect r{10, 100, 400, ArsenalSoundStrip::kHeight};
    const auto pr = strip.prevRect(r), nx = strip.nextRect(r);
    check(strip.hitTest(r, {pr.x + 3, pr.y + 3}) == ArsenalSoundStrip::Hit::Prev, "the left button is previous");
    check(strip.hitTest(r, {nx.x + 3, nx.y + 3}) == ArsenalSoundStrip::Hit::Next, "the right button is next");
    check(strip.hitTest(r, {r.x + 150, r.y + 10}) == ArsenalSoundStrip::Hit::None, "the name is not a button");
    check(ArsenalSoundStrip::displayName((dir / "kick_01.wav").string()) == "kick_01", "the name shown has no extension");

    fs::remove_all(dir);
    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
