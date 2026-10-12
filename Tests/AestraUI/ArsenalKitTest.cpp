// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// FD-27 point 6, "kits and the empty state": an Arsenal with no sounds offers a folder as a kit. The plan is
// the audio files only, in name order, capped, each named after its file, with duplicate names told apart.

#include "ArsenalKit.h"

#include <filesystem>
#include <fstream>
#include <iostream>

using Aestra::ArsenalKit;
using Aestra::ArsenalKitCard;
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
    std::cout << "[1] the plan\n";
    const auto plan = ArsenalKit::plan({"/k/Snare.wav", "/k/kick.wav", "/k/notes.txt", "/k/Hat.flac", "/k/cover.png"});
    check(plan.size() == 3, "only audio files become units (" + std::to_string(plan.size()) + ")");
    check(plan.size() == 3 && plan[0].name == "Hat" && plan[1].name == "kick" && plan[2].name == "Snare", "sorted by name ignoring case, named after the file");
    check(plan.size() == 3 && plan[2].path == "/k/Snare.wav", "each unit keeps its file");
    const auto dup = ArsenalKit::plan({"/a/kick.wav", "/b/kick.wav", "/c/kick.wav"});
    check(dup.size() == 3 && dup[0].name == "kick" && dup[1].name == "kick 2" && dup[2].name == "kick 3", "same name from different folders is told apart");
    std::vector<std::string> many;
    for (int i = 0; i < 40; ++i) many.push_back("/k/s" + std::string(i < 10 ? "0" : "") + std::to_string(i) + ".wav");
    const auto capped = ArsenalKit::plan(many);
    check(capped.size() == ArsenalKit::kMaxUnits && capped.front().name == "s00", "a big folder is capped to the first " + std::to_string(ArsenalKit::kMaxUnits));
    check(ArsenalKit::plan({}).empty(), "nothing in, nothing out");

    std::cout << "[2] reading a folder\n";
    const fs::path dir = fs::temp_directory_path() / "aestra_kit_test";
    fs::remove_all(dir);
    fs::create_directories(dir / "inner");
    for (const char* n : {"a.wav", "b.mp3", "readme.txt"}) touch(dir / n);
    touch(dir / "inner" / "deep.wav");
    check(ArsenalKit::filesIn(dir.string()).size() == 2, "two audio files, subfolders not walked");
    check(ArsenalKit::filesIn((dir / "missing").string()).empty(), "a missing folder is empty");
    fs::remove_all(dir);

    std::cout << "[3] the card\n";
    ArsenalKitCard card;
    const AestraUI::NUIRect slot{10, 300, 600, ArsenalKitCard::kReserve};
    card.place(true, slot);
    const auto c = card.cardRect(), b = card.buttonRect();
    check(c.x >= slot.x && c.right() <= slot.right() && c.y >= slot.y && c.bottom() <= slot.bottom(), "the card sits inside the space reserved for it");
    check(b.x >= c.x && b.right() <= c.right() && b.bottom() <= c.bottom(), "the button sits inside the card");
    check(card.hitButton({b.x + 5, b.y + 5}) && !card.hitButton({c.x + 2, c.y + 2}), "only the button is clickable");
    card.place(false, slot);
    check(!card.hitButton({b.x + 5, b.y + 5}), "a hidden card takes no clicks");

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
