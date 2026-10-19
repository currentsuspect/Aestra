// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The track header's second line (FD-50 board 8) says what plays the track and where it goes. Guards the
// wording rules: one Arsenal unit is named, several are counted, audio is added, Master is never spelled out,
// and an empty track routed to Master says nothing rather than "→ Master" or a dangling separator.

#include "TrackHeaderText.h"

#include <iostream>
#include <string>

namespace {
int failures = 0;

void expectLine(const Aestra::Audio::TrackHeader::LaneSource& source, const std::string& output,
                const std::string& expected) {
    const std::string got = Aestra::Audio::TrackHeader::sourceLine(source, output);
    if (got != expected) {
        std::cerr << "  FAIL: expected \"" << expected << "\", got \"" << got << "\"\n";
        ++failures;
    }
}
} // namespace

int main() {
    using Aestra::Audio::TrackHeader::LaneSource;
    expectLine(LaneSource{{"Kick - Hard"}, false}, "", "Kick - Hard");
    expectLine(LaneSource{{"Kick - Hard"}, false}, "Drum Bus", "Kick - Hard → Drum Bus");
    expectLine(LaneSource{{"Kick", "Clap", "Hats", "808"}, false}, "", "4 sounds");
    expectLine(LaneSource{{}, true}, "Space", "Audio → Space");
    expectLine(LaneSource{{"Keys"}, true}, "", "Keys + audio");
    expectLine(LaneSource{{}, false}, "Drum Bus", "→ Drum Bus");
    expectLine(LaneSource{{}, false}, "", "");

    if (!Aestra::Audio::TrackHeader::showsSourceLine(46.0f) || Aestra::Audio::TrackHeader::showsSourceLine(28.0f)) {
        std::cerr << "  FAIL: the second line shows on a 46 px row and drops away at 28 px\n";
        ++failures;
    }

    if (failures > 0) {
        std::cerr << "[FAIL] TrackHeaderTextTest: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "[PASS] TrackHeaderTextTest\n";
    return 0;
}
