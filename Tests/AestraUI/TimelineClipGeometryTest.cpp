// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guards the rules behind the FD-50 clip look that are easy to get silently wrong:
//
//   - A pattern clip's mini piano roll shows notes where they SOUND. The scheduler plays a note at
//     clip start - source offset + note start, only inside the clip, and never loops the pattern. The old
//     drawing stretched the whole pattern across the clip's width, so a 32-beat pattern in a 16-beat clip drew
//     every note at half its real position.
//   - The dashed "pattern ends here" mark appears only when the clip runs past its pattern.
//   - The header band stays a thin strip on a normal row and disappears on clips too small to hold it.

#include "TimelineClipGeometry.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {
int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "  FAIL: " << message << "\n";
        ++failures;
    }
}

bool near(double a, double b) { return std::abs(a - b) < 1e-9; }

using namespace Aestra::Audio::ClipPaint;

void testNotesSitWhereTheySound() {
    check(near(noteBeatInClip(8.0, 0.0), 8.0), "a note at beat 8 of the pattern sits 8 beats into an unoffset clip");
    check(near(noteBeatInClip(8.0, 4.0), 4.0), "trimming 4 beats off the clip's start moves the note 4 beats left");
    check(noteBeatInClip(2.0, 4.0) < 0.0, "a note before the source offset falls left of the clip (not drawn)");
}

void testPatternEndOnlyWhenTheClipRunsPastIt() {
    check(near(patternEndInClip(8.0, 0.0, 16.0), 8.0), "an 8-beat pattern in a 16-beat clip ends 8 beats in");
    check(near(patternEndInClip(8.0, 2.0, 16.0), 6.0), "the source offset shifts where the pattern ends");
    check(patternEndInClip(32.0, 0.0, 16.0) < 0.0, "a clip shorter than its pattern has no end mark (the starter kit's 32-beat pattern in 16-beat clips)");
    check(patternEndInClip(16.0, 0.0, 16.0) < 0.0, "a clip exactly as long as its pattern has no end mark");
    check(patternEndInClip(0.0, 0.0, 16.0) < 0.0, "an empty-length pattern has no end mark");
    check(patternEndInClip(4.0, 4.0, 16.0) < 0.0, "a source offset at or past the pattern's end has no end mark");
}

void testHeaderBand() {
    const float rowBand = headerBandHeight(42.0f); // a 46 px row less the clip's 2 px insets
    check(rowBand >= 11.0f && rowBand <= 14.0f, "the band on a normal row is a thin strip (11-14 px)");
    check(headerBandHeight(400.0f) <= 14.0f, "a tall row does not grow a tall band");
    check(showsHeaderBand(200.0f, 42.0f), "a normal clip shows its band");
    check(!showsHeaderBand(200.0f, 16.0f), "a clip too short for band plus content shows none");
    check(!showsHeaderBand(8.0f, 42.0f), "a sliver of a clip shows no band");
}
} // namespace

int main() {
    testNotesSitWhereTheySound();
    testPatternEndOnlyWhenTheClipRunsPastIt();
    testHeaderBand();
    if (failures > 0) {
        std::cerr << "[FAIL] TimelineClipGeometryTest: " << failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "[PASS] TimelineClipGeometryTest\n";
    return 0;
}
