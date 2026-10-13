// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The reference measurements behind Audition's tools (FD-26, V81-AU2): PLR, stereo width and
// mono problems, section detection, and level matching, against signals whose answers are known.

#include "Analysis/ReferenceAnalysis.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace Aestra::Audio;

namespace {

int g_failures = 0;
void check(bool c, const char* m) { if (!c) { std::printf("[FAIL] %s\n", m); ++g_failures; } }
void checkNear(double a, double e, double tol, const char* m) {
    if (std::fabs(a - e) > tol) { std::printf("[FAIL] %s: expected %.3f ±%.3f, got %.3f\n", m, e, tol, a); ++g_failures; }
}

constexpr double kPi = 3.14159265358979323846;
constexpr uint32_t kRate = 48000;

// Stereo tone: left = a·sin, right = sign·b·sin.
std::vector<float> tone(double hz, double seconds, double left, double right) {
    const size_t n = static_cast<size_t>(seconds * kRate);
    std::vector<float> out(n * 2);
    for (size_t i = 0; i < n; ++i) {
        const double s = std::sin(2.0 * kPi * hz * static_cast<double>(i) / kRate);
        out[i * 2] = static_cast<float>(left * s);
        out[i * 2 + 1] = static_cast<float>(right * s);
    }
    return out;
}

void append(std::vector<float>& a, const std::vector<float>& b) { a.insert(a.end(), b.begin(), b.end()); }

} // namespace

int main() {
    // PLR is true peak minus integrated loudness, and 0 when either cannot be measured.
    {
        AudioAnalysis a;
        a.truePeakDbtp = -1.0; a.integratedLufs = -9.0;
        checkNear(peakToLoudnessRatioDb(a), 8.0, 1e-9, "PLR = true peak - loudness");
        check(peakToLoudnessRatioDb(AudioAnalysis{}) == 0.0, "PLR of nothing is 0");
    }

    // Width: identical channels are all mid; opposite channels are all side; a window of out-of-phase audio is flagged.
    {
        const auto mono = tone(1000.0, 4.0, 0.5, 0.5);
        const auto d = analyzeStereoDetail(mono.data(), mono.size() / 2, 2, kRate);
        check(d.bandWidth[AudioAnalysis::Mid] < 0.01, "L=R has no width");
        check(d.correlationOverTime.size() == 4, "one correlation value per second");
        check(d.monoProblemStartsSec.empty(), "L=R has no mono problem");

        auto mixed = tone(1000.0, 2.0, 0.5, 0.5);
        append(mixed, tone(1000.0, 2.0, 0.5, -0.5));
        const auto m = analyzeStereoDetail(mixed.data(), mixed.size() / 2, 2, kRate);
        check(m.bandWidth[AudioAnalysis::Mid] > 0.4 && m.bandWidth[AudioAnalysis::Mid] < 0.6, "half the signal is side");
        check(m.monoProblemStartsSec.size() == 2 && m.monoProblemStartsSec[0] == 2.0, "the out-of-phase seconds are flagged at 2 s and 3 s");
        check(m.correlationOverTime[0] > 0.99f && m.correlationOverTime[3] < -0.99f, "correlation follows the signal");

        const std::vector<float> oneChannel(48000, 0.1f);
        check(analyzeStereoDetail(oneChannel.data(), oneChannel.size(), 1, kRate).correlationOverTime.empty(), "mono has no stereo detail");
    }

    // Sections: a quiet low tone, then a loud bright one, split near 10 s; a steady tone is one section.
    {
        auto song = tone(200.0, 10.0, 0.05, 0.05);
        append(song, tone(3000.0, 10.0, 0.5, 0.5));
        const auto sections = detectSections(song.data(), song.size() / 2, 2, kRate);
        check(sections.size() == 2, "a change of level and colour makes two sections");
        if (sections.size() == 2) {
            checkNear(sections[0].endSec, 10.0, 1.0, "the boundary lands near the change");
            check(sections[0].startSec == 0.0 && sections[1].endSec == 20.0, "sections cover the whole signal");
            check(sections[1].rmsDb > sections[0].rmsDb + 15.0, "the loud section measures louder");
            checkNear(sections[1].crestDb, 3.0, 0.3, "a sine's crest factor is about 3 dB");
        }
        const auto steady = tone(440.0, 20.0, 0.3, 0.3);
        check(detectSections(steady.data(), steady.size() / 2, 2, kRate).size() == 1, "a steady tone is one section");
        check(detectSections(nullptr, 0, 2, kRate).empty(), "nothing in, no sections");
    }

    // Level match: the gain that brings the mix to the reference, whole song or a stretch.
    {
        checkNear(levelMatchGainDb(-12.0, -9.0), 3.0, 1e-9, "a quieter mix is raised");
        check(levelMatchGainDb(AudioAnalysis::kSilenceDb, -9.0) == 0.0, "no gain from a silent mix");

        const auto quiet = tone(1000.0, 6.0, 0.1, 0.1);
        auto loud = tone(1000.0, 3.0, 0.1, 0.1);
        append(loud, tone(1000.0, 3.0, 0.4, 0.4)); // second half +12 dB
        const double whole = levelMatchGainDbForRange(quiet.data(), quiet.size() / 2, 2, kRate, 0, 6, loud.data(), loud.size() / 2, 2, kRate, 0, 6);
        const double loudHalf = levelMatchGainDbForRange(quiet.data(), quiet.size() / 2, 2, kRate, 0, 3, loud.data(), loud.size() / 2, 2, kRate, 3, 6);
        checkNear(loudHalf, 12.0, 0.3, "matching a loud section needs +12 dB");
        check(whole > 3.0 && whole < loudHalf, "the whole song sits between the halves");
    }

    // Tonal balance: a mix with more low end than the reference reads positive there.
    {
        AudioAnalysis mix, ref;
        mix.bandShare = {0.4, 0.2, 0.2, 0.1, 0.1};
        ref.bandShare = {0.2, 0.2, 0.2, 0.2, 0.2};
        const auto delta = tonalBalanceDeltaDb(mix, ref);
        checkNear(delta[AudioAnalysis::Low], 3.01, 0.05, "double the share is +3 dB");
        checkNear(delta[AudioAnalysis::Mid], 0.0, 1e-9, "equal share is 0 dB");
        checkNear(delta[AudioAnalysis::High], -3.01, 0.05, "half the share is -3 dB");
    }

    if (g_failures) { std::printf("%d failed\n", g_failures); return 1; }
    std::printf("ReferenceAnalysisTest passed\n");
    return 0;
}
