// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

// #747: varispeed tempo-fit math. Pitch follows tempo by definition — these
// cases pin the span/rate relationship, the varispeed clamp, and input guards.

#include "Models/ClipFit.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

using namespace Aestra;

int g_failures = 0;

void expect(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "[FAIL] " << what << "\n";
        ++g_failures;
    }
}

void expectNear(double actual, double expected, double tol, const std::string& what) {
    expect(std::abs(actual - expected) <= tol, what + " (got " + std::to_string(actual) + ")");
}

} // namespace

int main() {
    // Identity fit: 2 s of content into 1 bar at 120 BPM (4/4 = 2 s).
    {
        const auto fit = Audio::computeFitToBars(2.0, 120.0, 1);
        expect(fit.has_value(), "identity fit computes");
        if (fit) {
            expectNear(fit->durationBeats, 4.0, 1e-9, "1 bar = 4 beats");
            expectNear(fit->durationSeconds, 2.0, 1e-9, "1 bar @120 = 2 s");
            expect(std::abs(fit->playbackRate - 1.0f) < 1e-6, "rate 1.0 when content equals span");
            expect(!fit->rateClamped, "identity fit not clamped");
        }
    }

    // Faster: long content squeezed into a short span.
    {
        const auto fit = Audio::computeFitToBars(8.0, 120.0, 1);
        expect(fit.has_value(), "squeeze fit computes");
        if (fit) {
            expect(std::abs(fit->playbackRate - 4.0f) < 1e-6, "8 s into 2 s = 4x");
            expect(!fit->rateClamped, "4x is exactly at the bound, not clamped");
        }
    }

    // Slower: short content stretched across a long span.
    {
        const auto fit = Audio::computeFitToBars(1.0, 120.0, 2);
        expect(fit.has_value(), "stretch fit computes");
        if (fit) {
            // 2 bars @120 = 4 s; 1 s content → 0.25x.
            expect(std::abs(fit->playbackRate - 0.25f) < 1e-6, "1 s into 4 s = 0.25x");
            expect(!fit->rateClamped, "0.25x inside bounds");
        }
    }

    // Clamp high: 20 s into 1 bar @120 needs 10x — clamped to 4x, flagged.
    {
        const auto fit = Audio::computeFitToBars(20.0, 120.0, 1);
        expect(fit.has_value(), "clamped-high fit computes");
        if (fit) {
            expect(std::abs(fit->playbackRate - 4.0f) < 1e-6, "clamped to 4x");
            expect(fit->rateClamped, "clamp flagged");
            expectNear(fit->durationSeconds, 2.0, 1e-9, "span still applied when clamped");
        }
    }

    // Clamp low: 0.25 s across 8 bars @120 (16 s) needs 0.015625x.
    {
        const auto fit = Audio::computeFitToBars(0.25, 120.0, 8);
        expect(fit.has_value(), "clamped-low fit computes");
        if (fit) {
            expect(std::abs(fit->playbackRate - 0.25f) < 1e-6, "clamped to 0.25x");
            expect(fit->rateClamped, "low clamp flagged");
        }
    }

    // Tempo scaling: same content, different BPM changes the span seconds.
    {
        const auto fit = Audio::computeFitToBars(3.0, 60.0, 1);
        expect(fit.has_value(), "60 bpm fit computes");
        if (fit) {
            expectNear(fit->durationSeconds, 4.0, 1e-9, "1 bar @60 = 4 s");
            expect(std::abs(fit->playbackRate - 0.75f) < 1e-6, "3 s into 4 s = 0.75x");
        }
    }

    // Guards.
    {
        expect(!Audio::computeFitToBars(0.0, 120.0, 1).has_value(), "zero content rejected");
        expect(!Audio::computeFitToBars(-1.0, 120.0, 1).has_value(), "negative content rejected");
        expect(!Audio::computeFitToBars(2.0, 0.0, 1).has_value(), "zero bpm rejected");
        expect(!Audio::computeFitToBars(2.0, -5.0, 1).has_value(), "negative bpm rejected");
        expect(!Audio::computeFitToBars(2.0, 120.0, 0).has_value(), "zero bars rejected");
        expect(!Audio::computeFitToBars(2.0, 120.0, -2).has_value(), "negative bars rejected");
    }

    if (g_failures == 0) {
        std::cout << "[PASS] ClipFitToBarsTest\n";
        return 0;
    }
    std::cerr << "[FAIL] ClipFitToBarsTest: " << g_failures << " failure(s)\n";
    return 1;
}
