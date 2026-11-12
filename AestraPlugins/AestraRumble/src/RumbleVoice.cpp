// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumbleVoice.h"

#include <algorithm>
#include <cmath>

namespace Aestra {
namespace Plugins {

namespace {
constexpr double kTwoPi = 6.283185307179586;
// Where the peak is for soft and hard clipping: inside the hold, the first 30 ms of extra punch, and the
// filter settling. Fold is different: as a note fades it can fold back up louder, so it is searched whole.
constexpr double kPeakSearchAfterHold = 0.25;
constexpr double kFoldFrom = 0.67;

double shape(double x, double clipType) {
    if (clipType < 0.34) {
        return std::tanh(x);
    }
    if (clipType < 0.67) {
        return x > 1.0 ? 1.0 : x < -1.0 ? -1.0 : x;
    }
    return std::sin(x * 1.2);
}

double noteHz(int note) {
    return 440.0 * std::pow(2.0, (note - 69) / 12.0);
}

// One pass over the start of a note with the sketch's arithmetic. Shared by rawPeak and render so the
// two can never disagree.
struct Pass {
    double sr, f0, t60, hold, drop, pt, cv, drive, cut, ca, cd;
    double ph = 0.0, l1 = 0.0, l2 = 0.0, cl = 0.0;
    uint32_t seed;
    const RumbleSound& g;

    Pass(const RumbleSound& sound, int note, double sampleRate)
        : sr(sampleRate),
          f0(noteHz(note)),
          t60(RumbleVoice::tailSeconds(sound[RumbleSound::Tail])),
          hold(RumbleVoice::holdSeconds(sound[RumbleSound::Hold])),
          drop(RumbleVoice::dropSemitones(sound[RumbleSound::Punch])),
          pt(RumbleVoice::dropSeconds(sound)),
          cv(0.6 + sound[RumbleSound::Curve] * 1.2),
          drive(1.0 + sound[RumbleSound::Dirt] * sound[RumbleSound::Dirt] * 22.0),
          cut(RumbleVoice::toneHz(sound[RumbleSound::Tone])),
          ca(1.0 - std::exp(-kTwoPi * (400.0 + sound[RumbleSound::ClickTone] * 5000.0) / sampleRate)),
          cd(0.002 + sound[RumbleSound::ClickLength] * 0.02),
          seed(sound.seed),
          g(sound) {}

    double at(uint32_t i) {
        const double t = i / sr;
        const double f = f0 * std::pow(2.0, drop * std::exp(-t / pt) / 12.0);
        ph += kTwoPi * f / sr;
        const double te = t < hold ? 0.0 : (t - hold) / t60;
        const double env = std::exp(-6.9 * std::pow(te, cv));
        const double s = std::sin(ph) + g[RumbleSound::Second] * 0.6 * std::sin(2.0 * ph + 0.6) +
                         g[RumbleSound::Third] * 0.45 * std::sin(3.0 * ph);
        const double x = s * env * (1.0 + g[RumbleSound::Punch] * 0.6 * std::exp(-t / 0.03));
        const double dv = shape(x * drive, g[RumbleSound::ClipType]);
        const double a = 1.0 - std::exp(-kTwoPi * std::min(sr * 0.45, cut * (0.3 + 0.7 * env) + f0 * 1.5) / sr);
        l1 += a * (dv - l1);
        l2 += a * (l1 - l2);
        seed = seed * 1664525u + 1013904223u;
        const double nz = seed / 2147483648.0 - 1.0;
        cl += ca * (nz - cl);
        const double click = g[RumbleSound::Click];
        const double tone = g[RumbleSound::ClickTone];
        const double ck = (cl * (1.0 - tone) + (nz - cl) * tone * 0.5) * click * std::exp(-t / cd) * 0.9;
        const double weight = g[RumbleSound::Weight];
        return std::min(1.0, t / 0.0015) * (l2 * (1.0 - 0.45 * weight) + std::sin(ph) * env * weight * 0.9 + ck);
    }
};
} // namespace

double RumbleVoice::tailSeconds(double tail) {
    return 0.25 * std::pow(14.0, tail);
}
double RumbleVoice::toneHz(double tone) {
    return 90.0 * std::pow(60.0, tone);
}
double RumbleVoice::holdSeconds(double hold) {
    return 0.02 + hold * 0.18;
}
double RumbleVoice::dropSemitones(double punch) {
    return punch * 30.0;
}
double RumbleVoice::dropSeconds(const RumbleSound& g) {
    const double d = std::max(0.2, dropSemitones(g[RumbleSound::Punch]));
    return std::min(0.012 + g[RumbleSound::DropTime] * 0.11,
                    0.35 * (holdSeconds(g[RumbleSound::Hold]) + tailSeconds(g[RumbleSound::Tail])) / std::log(d / 0.1));
}
double RumbleVoice::lengthSeconds(const RumbleSound& g) {
    return std::min(3.9 + 0.3, holdSeconds(g[RumbleSound::Hold]) + tailSeconds(g[RumbleSound::Tail]) + 0.03);
}
uint32_t RumbleVoice::lengthSamples(const RumbleSound& g, double sampleRate) {
    return static_cast<uint32_t>(std::floor(lengthSeconds(g) * sampleRate));
}

double RumbleVoice::rawPeak(const RumbleSound& sound, int note, double sampleRate) {
    Pass pass(sound, note, sampleRate);
    const uint32_t n = lengthSamples(sound, sampleRate);
    const bool fold = sound[RumbleSound::ClipType] >= kFoldFrom;
    const uint32_t search =
        fold ? n
             : std::min<uint32_t>(n, static_cast<uint32_t>(
                                         (holdSeconds(sound[RumbleSound::Hold]) + kPeakSearchAfterHold) * sampleRate));
    double peak = 0.0;
    for (uint32_t i = 0; i < search; ++i) {
        peak = std::max(peak, std::fabs(pass.at(i)));
    }
    return peak;
}

std::vector<float> RumbleVoice::render(const RumbleSound& sound, int note, double sampleRate) {
    Pass pass(sound, note, sampleRate);
    const uint32_t n = lengthSamples(sound, sampleRate);
    std::vector<double> raw(n);
    double peak = 0.0;
    for (uint32_t i = 0; i < n; ++i) {
        raw[i] = pass.at(i);
        peak = std::max(peak, std::fabs(raw[i]));
    }
    const double gain = kLevelledPeak / (peak > 0.0 ? peak : 1.0);
    const uint32_t fade = static_cast<uint32_t>(std::floor(0.008 * sampleRate));
    std::vector<float> out(n);
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = static_cast<float>(raw[i] * gain * (i > n - fade ? static_cast<double>(n - i) / fade : 1.0));
    }
    return out;
}

void RumbleVoice::prepare(double sampleRate) {
    m_sampleRate = sampleRate > 1.0 ? sampleRate : 48000.0;
    m_active = false;
}

void RumbleVoice::start(const RumbleSound& sound, int note, double rawPeak, double level) {
    m_sound = sound;
    m_note = note;
    m_f0 = noteHz(note);
    m_tail = tailSeconds(sound[RumbleSound::Tail]);
    m_hold = holdSeconds(sound[RumbleSound::Hold]);
    m_drop = dropSemitones(sound[RumbleSound::Punch]);
    m_dropTime = dropSeconds(sound);
    m_curve = 0.6 + sound[RumbleSound::Curve] * 1.2;
    m_drive = 1.0 + sound[RumbleSound::Dirt] * sound[RumbleSound::Dirt] * 22.0;
    m_cut = toneHz(sound[RumbleSound::Tone]);
    m_clickAlpha = 1.0 - std::exp(-kTwoPi * (400.0 + sound[RumbleSound::ClickTone] * 5000.0) / m_sampleRate);
    m_clickDecay = 0.002 + sound[RumbleSound::ClickLength] * 0.02;
    m_length = lengthSamples(sound, m_sampleRate);
    m_fadeLength = std::max<uint32_t>(1, static_cast<uint32_t>(std::floor(0.008 * m_sampleRate)));
    m_gain = level * kLevelledPeak / (rawPeak > 0.0 ? rawPeak : 1.0);
    m_index = 0;
    m_time = 0.0;
    m_phase = 0.0;
    m_low1 = m_low2 = m_clickLow = 0.0;
    m_noise = sound.seed;
    m_rate = m_rateTarget = 1.0;
    m_rateCoeff = 0.0;
    m_cutFade = 1.0;
    m_cutFadeStep = 0.0;
    m_active = m_length > 0;
}

void RumbleVoice::slideTo(int note) {
    const double seconds = std::max(1.0e-3, m_sound[RumbleSound::Slide] * 0.1); // the sketch's time constant
    m_rateTarget = std::pow(2.0, (note - m_note) / 12.0);
    m_rateCoeff = 1.0 - std::exp(-1.0 / (seconds * m_sampleRate));
}

void RumbleVoice::stop() {
    if (m_active) {
        m_cutFadeStep = 1.0 / std::max(1.0, 0.006 * 3.0 * m_sampleRate); // about the sketch's 6 ms release
    }
}

double RumbleVoice::rawSample() {
    // The sketch's voice, run on its own clock: m_time moves at m_rate, so a slide bends pitch and pace
    // together as a sample played faster would.
    const double sr = m_sampleRate;
    const double t = m_time;
    const double rateHz = m_rate;
    const double f = m_f0 * std::pow(2.0, m_drop * std::exp(-t / m_dropTime) / 12.0);
    m_phase += kTwoPi * f * rateHz / sr;
    const double te = t < m_hold ? 0.0 : (t - m_hold) / m_tail;
    const double env = std::exp(-6.9 * std::pow(te, m_curve));
    const RumbleSound& g = m_sound;
    const double s = std::sin(m_phase) + g[RumbleSound::Second] * 0.6 * std::sin(2.0 * m_phase + 0.6) +
                     g[RumbleSound::Third] * 0.45 * std::sin(3.0 * m_phase);
    const double x = s * env * (1.0 + g[RumbleSound::Punch] * 0.6 * std::exp(-t / 0.03));
    const double dv = shape(x * m_drive, g[RumbleSound::ClipType]);
    const double a =
        1.0 - std::exp(-kTwoPi * std::min(sr * 0.45, (m_cut * (0.3 + 0.7 * env) + m_f0 * 1.5) * rateHz) / sr);
    m_low1 += a * (dv - m_low1);
    m_low2 += a * (m_low1 - m_low2);
    m_noise = m_noise * 1664525u + 1013904223u;
    const double nz = m_noise / 2147483648.0 - 1.0;
    m_clickLow += m_clickAlpha * (nz - m_clickLow);
    const double tone = g[RumbleSound::ClickTone];
    const double ck = (m_clickLow * (1.0 - tone) + (nz - m_clickLow) * tone * 0.5) * g[RumbleSound::Click] *
                      std::exp(-t / m_clickDecay) * 0.9;
    const double weight = g[RumbleSound::Weight];
    return std::min(1.0, t / 0.0015) * (m_low2 * (1.0 - 0.45 * weight) + std::sin(m_phase) * env * weight * 0.9 + ck);
}

float RumbleVoice::next() {
    if (!m_active) {
        return 0.0f;
    }
    // An unslid note runs on the sketch's own sample count so it matches it exactly.
    const bool unslid = m_rate == 1.0 && m_rateTarget == 1.0;
    if (unslid) {
        m_time = m_index / m_sampleRate;
    }
    const double raw = rawSample();
    const double position = unslid ? static_cast<double>(m_index) : m_time * m_sampleRate;
    const double fromEnd = static_cast<double>(m_length) - position;
    const double endFade = fromEnd < m_fadeLength ? std::max(0.0, fromEnd / m_fadeLength) : 1.0;
    const double out = raw * m_gain * endFade * m_cutFade;

    ++m_index;
    m_rate += m_rateCoeff * (m_rateTarget - m_rate);
    m_time += m_rate / m_sampleRate;
    if (m_cutFadeStep > 0.0) {
        m_cutFade -= m_cutFadeStep;
    }
    if (position + 1.0 >= m_length || m_cutFade <= 0.0) {
        m_active = false;
    }
    return static_cast<float>(out);
}

} // namespace Plugins
} // namespace Aestra
