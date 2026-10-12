// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V81-M2: the master meter reads momentary (400 ms) and short-term (3 s) loudness. LoudnessWindow is fed
// K-weighted energy per sample; with a constant energy the answer is known exactly, so each window can be
// checked against the formula, not against itself.

#include "DSP/LoudnessWindow.h"

#include <cmath>
#include <iostream>
#include <string>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

constexpr double kRate = 48000.0;

void feed(LoudnessWindow& w, double energy, double seconds) {
    const auto n = static_cast<uint64_t>(seconds * kRate);
    for (uint64_t i = 0; i < n; ++i)
        w.add(energy);
}

float lufsOf(double energy) { return static_cast<float>(-0.691 + 10.0 * std::log10(energy)); }

} // namespace

int main() {
    std::cout << "[1] silence until a full momentary window exists\n";
    {
        LoudnessWindow w;
        w.prepare(kRate);
        feed(w, 0.1, 0.3);
        check(w.momentaryLufs() == LoudnessWindow::kSilence, "300 ms in, momentary still reads silence");
        feed(w, 0.1, 0.11);
        check(std::fabs(w.momentaryLufs() - lufsOf(0.1)) < 0.01f, "at 400 ms it reads the formula's value");
    }

    std::cout << "[2] a constant level reads the same in both windows\n";
    {
        LoudnessWindow w;
        w.prepare(kRate);
        feed(w, 0.05, 4.0);
        check(std::fabs(w.momentaryLufs() - lufsOf(0.05)) < 0.01f, "momentary matches");
        check(std::fabs(w.shortTermLufs() - lufsOf(0.05)) < 0.01f, "short-term matches");
    }

    std::cout << "[3] short-term remembers 3 seconds, momentary only 400 ms\n";
    {
        LoudnessWindow w;
        w.prepare(kRate);
        feed(w, 0.2, 3.0);   // loud
        feed(w, 0.002, 0.5); // then quiet for half a second
        const float quiet = lufsOf(0.002);
        check(std::fabs(w.momentaryLufs() - quiet) < 0.01f, "momentary has already dropped to the quiet level");
        check(w.shortTermLufs() > quiet + 3.0f, "short-term still carries the loud stretch (" +
                                                    std::to_string(w.shortTermLufs()) + " vs " + std::to_string(quiet) + ")");
        // after a further 3 s of quiet the loud part has left the window
        feed(w, 0.002, 3.0);
        check(std::fabs(w.shortTermLufs() - quiet) < 0.01f, "three quiet seconds later short-term has caught up");
    }

    std::cout << "[4] a doubling of energy is 3 dB\n";
    {
        LoudnessWindow a, b;
        a.prepare(kRate);
        b.prepare(kRate);
        feed(a, 0.01, 1.0);
        feed(b, 0.02, 1.0);
        check(std::fabs((b.momentaryLufs() - a.momentaryLufs()) - 3.0103f) < 0.01f, "twice the energy reads 3.01 LU higher");
    }

    std::cout << "[5] a reset forgets the history\n";
    {
        LoudnessWindow w;
        w.prepare(kRate);
        feed(w, 0.2, 3.0);
        w.requestReset();
        check(w.momentaryLufs() == LoudnessWindow::kSilence && w.shortTermLufs() == LoudnessWindow::kSilence,
              "the readouts drop at once");
        feed(w, 0.002, 0.5);
        check(std::fabs(w.shortTermLufs() - lufsOf(0.002)) < 0.01f, "and the old loud stretch no longer counts");
    }

    std::cout << "[6] digital silence is silence, not -inf\n";
    {
        LoudnessWindow w;
        w.prepare(kRate);
        feed(w, 0.0, 2.0);
        check(w.momentaryLufs() == LoudnessWindow::kSilence && std::isfinite(w.shortTermLufs()), "finite, floor value");
    }

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
