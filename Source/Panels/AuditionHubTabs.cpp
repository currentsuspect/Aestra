// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Audition item page's tabs other than Overview (FD-26, V81-A): Tonal, Dynamics, Stereo and Sections. Each shows
// the open mix alone, or the mix beside the chosen reference (the reference as it is heard: level-matched).
#include "AuditionHub.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace AestraUI;

namespace Aestra {

namespace {

const NUIColor kRef = NUIColor::fromHex(0xfff5a83a);
const NUIColor kWarn = NUIColor::fromHex(0xffe5534b);

std::string fmt(const char* f, double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

/// A section level, or a dash where the section is silent (the tail after the music).
std::string levelDb(double db) { return db > -90.0 ? fmt("%.1f dB", db) : std::string("-"); }

std::string mmss(double seconds) {
    const int total = static_cast<int>(std::max(0.0, seconds));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return buf;
}

struct Ink {
    NUIColor text, muted, rule, mix, mixFill, panel, cyan;
};

Ink ink() {
    auto& theme = NUIThemeManager::getInstance();
    return {theme.getColor("textPrimary"), theme.getColor("textSecondary"), theme.getColor("borderSubtle"),
            theme.getColor("accentPrimary").lightened(0.25f), theme.getColor("accentPrimary"),
            theme.getColor("backgroundSecondary"), theme.getColor("accentCyan")};
}

void heading(NUIRenderer& r, const std::string& title, float x, float y, const Ink& c) {
    r.drawText(title, {x, y}, 9.5f, c.muted.withAlpha(0.8f));
}

/// A legend entry: a swatch and a label; returns the x after it.
float legend(NUIRenderer& r, float x, float y, const NUIColor& color, const std::string& label, bool line) {
    if (line) r.fillRect({x, y + 5.0f, 14.0f, 2.0f}, color);
    else r.fillRect({x, y + 2.0f, 8.0f, 8.0f}, color);
    r.drawText(label, {x + (line ? 20.0f : 14.0f), y}, 10.5f, color);
    return x + (line ? 20.0f : 14.0f) + r.measureText(label, 10.5f).width + 18.0f;
}

/// A line through the points whose value is above @p floor; a gap wherever one is not.
void plot(NUIRenderer& r, const std::vector<NUIPoint>& pts, const std::vector<bool>& valid, float thickness, const NUIColor& color) {
    std::vector<NUIPoint> run;
    const auto flush = [&]() { if (run.size() >= 2) r.drawPolyline(run.data(), static_cast<int>(run.size()), thickness, color); run.clear(); };
    for (size_t i = 0; i < pts.size(); ++i) {
        if (valid[i]) run.push_back(pts[i]);
        else flush();
    }
    flush();
}

const char* kBandNames[] = {"Low", "Low mid", "Mid", "High mid", "High"};
const char* kBandRanges[] = {"below 120 Hz", "120-500 Hz", "500 Hz-2 kHz", "2-6 kHz", "above 6 kHz"};

/// The shape of a spectrum: each band relative to the average of 63 Hz-8 kHz, so two tracks at different levels compare.
std::vector<double> spectrumShape(const Audio::Spectrum& s, std::vector<bool>& valid) {
    const auto& c = Audio::Spectrum::centersHz();
    double sum = 0.0;
    int n = 0;
    for (size_t b = 0; b < Audio::Spectrum::kBands; ++b)
        if (c[b] >= 63.0 && c[b] <= 8000.0 && s.levelDb[b] > -150.0) { sum += s.levelDb[b]; ++n; }
    std::vector<double> out(Audio::Spectrum::kBands, 0.0);
    valid.assign(Audio::Spectrum::kBands, false);
    if (n == 0) return out;
    const double mean = sum / n;
    for (size_t b = 0; b < Audio::Spectrum::kBands; ++b)
        if (s.levelDb[b] > -150.0) { out[b] = s.levelDb[b] - mean; valid[b] = true; }
    return out;
}

/// Where each section sits across a lane: one block per section, as wide as its share of the track.
std::vector<NUIRect> sectionBlocks(const NUIRect& lane, const std::vector<Audio::Section>& sections, double seconds) {
    std::vector<NUIRect> out;
    if (seconds <= 0.0) return out;
    for (const auto& s : sections) {
        const float x0 = lane.x + lane.width * static_cast<float>(s.startSec / seconds);
        const float x1 = lane.x + lane.width * static_cast<float>(std::min(s.endSec, seconds) / seconds);
        out.push_back({x0 + 1.0f, lane.y, std::max(2.0f, x1 - x0 - 2.0f), lane.height});
    }
    return out;
}

struct SectionLanes {
    NUIRect mix, ref;
};
SectionLanes sectionLanes(const NUIRect& panel) {
    return {{panel.x, panel.y + 52.0f, panel.width, 40.0f}, {panel.x, panel.y + 128.0f, panel.width, 40.0f}};
}

} // namespace

void AuditionHub::renderItemTab(NUIRenderer& r, const AuditionHubLayout::ItemFrame& p, const AuditionCompare::Side* mix) {
    const Ink c = ink();
    m_tabContentH = 0.0f;
    if (!mix) {
        r.drawTextCentered(m_sideShared->working.load() > 0 ? "Measuring..." : "Could not read this file", p.panel, 12.0f, c.muted.withAlpha(0.7f));
        return;
    }
    // A tab is as tall as it needs; it scrolls inside the body (a laptop screen leaves little room under the hero).
    const NUIRect panel{p.panel.x, p.panel.y - m_tabScroll, p.panel.width, 4000.0f};
    r.setClipRect(p.body);
    float bottom = panel.y;
    const AuditionCompare::Side* ref = m_result ? &m_result->ref : nullptr;
    const double refGain = m_result ? m_result->referenceGainDb + m_nudgeDb : 0.0;
    const std::string mixName = m_item.kind == AuditionLibrary::Item::Kind::Reference ? m_item.title : (m_item.badge.empty() ? "Mix" : "Mix " + m_item.badge);
    const std::string refName = m_ref ? AuditionLibrary::displayName(*m_ref) : std::string("Reference");
    const bool current = m_hasNow && m_now.path == m_item.path && m_engine;
    const double position = current ? m_engine->getPositionSeconds() : -1.0;

    switch (m_tab) {
    case ItemTab::Tonal: {
        // The curve: each track's third-octave balance, as a shape (the level difference is taken out).
        heading(r, "TONAL BALANCE", panel.x, panel.y, c);
        float lx = panel.x + r.measureText("TONAL BALANCE", 9.5f).width + 24.0f;
        lx = legend(r, lx, panel.y - 2.0f, c.mix, mixName, true);
        if (ref) legend(r, lx, panel.y - 2.0f, kRef, refName, true);
        const NUIRect g{panel.x, panel.y + 26.0f, panel.width, 220.0f};
        r.fillRect(g, c.panel);
        std::vector<bool> mixValid, refValid;
        const auto mixShape = spectrumShape(mix->spectrum, mixValid);
        const auto refShape = ref ? spectrumShape(ref->spectrum, refValid) : std::vector<double>{};
        double maxAbs = 6.0;
        for (size_t b = 0; b < mixShape.size(); ++b) if (mixValid[b]) maxAbs = std::max(maxAbs, std::fabs(mixShape[b]));
        for (size_t b = 0; b < refShape.size(); ++b) if (refValid[b]) maxAbs = std::max(maxAbs, std::fabs(refShape[b]));
        const double range = std::min(36.0, std::ceil(maxAbs / 6.0) * 6.0);
        const auto& centers = Audio::Spectrum::centersHz();
        const double lo = std::log10(20.0), hi = std::log10(16000.0);
        const auto xAt = [&](double hz) { return g.x + g.width * static_cast<float>((std::log10(hz) - lo) / (hi - lo)); };
        const auto yAt = [&](double db) { return g.y + g.height * 0.5f - static_cast<float>(db / range) * (g.height * 0.5f - 8.0f); };
        r.fillRect({g.x, yAt(0.0), g.width, 1.0f}, c.rule);
        for (double hz : {50.0, 100.0, 250.0, 500.0, 1000.0, 2500.0, 5000.0, 10000.0}) {
            const float x = xAt(hz);
            r.fillRect({x, g.y, 1.0f, g.height}, c.rule.withAlpha(0.5f));
            const std::string label = hz >= 1000.0 ? fmt(hz == 2500.0 ? "%.1fk" : "%.0fk", hz / 1000.0) : fmt("%.0f", hz);
            r.drawText(label, {x - r.measureText(label, 9.5f).width * 0.5f, g.bottom() + 6.0f}, 9.5f, c.muted.withAlpha(0.7f));
        }
        r.drawText(fmt("+%.0f dB", range), {g.x + 6.0f, g.y + 4.0f}, 9.5f, c.muted.withAlpha(0.6f));
        r.drawText(fmt("-%.0f dB", range), {g.x + 6.0f, g.bottom() - 16.0f}, 9.5f, c.muted.withAlpha(0.6f));
        const auto points = [&](const std::vector<double>& shape) {
            std::vector<NUIPoint> pts;
            for (size_t b = 0; b < shape.size(); ++b) pts.push_back({xAt(centers[b]), yAt(std::clamp(shape[b], -range, range))});
            return pts;
        };
        if (ref) plot(r, points(refShape), refValid, 1.6f, kRef);
        plot(r, points(mixShape), mixValid, 2.2f, c.mix);

        // The five bands: mix minus reference, or (alone) the mix's share of the energy.
        const float by = g.bottom() + 40.0f;
        heading(r, ref ? "MIX AGAINST THE REFERENCE, PER BAND" : "SHARE OF THE ENERGY, PER BAND", panel.x, by, c);
        const float colW = panel.width / 5.0f, barH = 44.0f, mid = by + 24.0f + barH;
        std::vector<std::string> notes;
        for (size_t b = 0; b < 5; ++b) {
            const float x = panel.x + colW * static_cast<float>(b), cx = x + colW * 0.5f;
            if (ref) {
                const double d = m_result->tonalDeltaDb[b];
                const float h = std::min(barH, static_cast<float>(std::fabs(d) / 6.0) * barH);
                const bool big = std::fabs(d) >= 1.5;
                r.fillRect({cx - 14.0f, mid, 28.0f, 1.0f}, c.rule);
                r.fillRect({cx - 10.0f, d > 0 ? mid - h : mid, 20.0f, std::max(1.0f, h)}, big ? kRef : c.muted.withAlpha(0.5f));
                const std::string v = fmt("%+.1f", d);
                r.drawText(v, {cx - r.measureText(v, 12.0f).width * 0.5f, mid + barH + 8.0f}, 12.0f, big ? kRef : c.text);
                if (big) notes.push_back(std::string(d > 0 ? "more " : "less ") + kBandNames[b] + fmt(" (%+.1f dB)", d));
            } else {
                const double share = mix->analysis.bandShare[b];
                const float h = static_cast<float>(share) * barH * 2.0f;
                r.fillRect({cx - 10.0f, mid + barH - h, 20.0f, std::max(1.0f, h)}, c.mixFill);
                const std::string v = fmt("%.0f%%", share * 100.0);
                r.drawText(v, {cx - r.measureText(v, 12.0f).width * 0.5f, mid + barH + 8.0f}, 12.0f, c.text);
            }
            r.drawText(kBandNames[b], {cx - r.measureText(kBandNames[b], 11.0f).width * 0.5f, mid + barH + 28.0f}, 11.0f, c.muted);
            r.drawText(kBandRanges[b], {cx - r.measureText(kBandRanges[b], 9.5f).width * 0.5f, mid + barH + 44.0f}, 9.5f, c.muted.withAlpha(0.7f));
        }
        std::string summary;
        if (!ref) summary = "Pick a reference above to see where your mix has more or less than it.";
        else if (notes.empty()) summary = "Within 1.5 dB of the reference in every band.";
        else {
            summary = "Your mix has ";
            for (size_t i = 0; i < notes.size(); ++i) summary += (i ? (i + 1 == notes.size() ? " and " : ", ") : "") + notes[i];
            summary += " than the reference.";
        }
        r.drawText(summary, {panel.x, mid + barH + 72.0f}, 11.5f, c.muted);
        bottom = mid + barH + 90.0f;
        break;
    }

    case ItemTab::Dynamics: {
        // Short-term loudness over time: the reference as it is heard, after the level match.
        heading(r, "SHORT-TERM LOUDNESS", panel.x, panel.y, c);
        float lx = panel.x + r.measureText("SHORT-TERM LOUDNESS", 9.5f).width + 24.0f;
        lx = legend(r, lx, panel.y - 2.0f, c.mix, mixName, true);
        if (ref) legend(r, lx, panel.y - 2.0f, kRef, refName + fmt(", level-matched %+.1f dB", refGain), true);
        const NUIRect g{panel.x, panel.y + 26.0f, panel.width, 200.0f};
        r.fillRect(g, c.panel);
        const auto& a = mix->analysis.shortTermLufs;
        const std::vector<float> none;
        const auto& b = ref ? ref->analysis.shortTermLufs : none;
        double top = -200.0, low = 0.0;
        const auto scan = [&](const std::vector<float>& v, double gain) {
            for (float x : v) if (x > -70.0f) { top = std::max(top, x + gain); low = std::min(low, x + gain); }
        };
        scan(a, 0.0);
        scan(b, refGain);
        if (top < -150.0) { r.drawTextCentered("Too quiet or too short to measure", g, 12.0f, c.muted); break; }
        top = std::ceil((top + 1.0) / 3.0) * 3.0;
        low = std::max(top - 36.0, std::floor((low - 1.0) / 3.0) * 3.0);
        const double span = std::max(mix->seconds, ref ? ref->seconds : 0.0);
        const auto xAt = [&](double sec) { return g.x + g.width * static_cast<float>(std::clamp(sec / std::max(0.001, span), 0.0, 1.0)); };
        const auto yAt = [&](double lufs) { return g.bottom() - 6.0f - (g.height - 12.0f) * static_cast<float>((std::clamp(lufs, low, top) - low) / (top - low)); };
        for (double v = top; v >= low; v -= 6.0) {
            r.fillRect({g.x, yAt(v), g.width, 1.0f}, c.rule.withAlpha(0.5f));
            r.drawText(fmt("%.0f", v), {g.x + 6.0f, yAt(v) + 2.0f}, 9.0f, c.muted.withAlpha(0.6f));
        }
        const auto series = [&](const std::vector<float>& v, double gain, std::vector<bool>& ok) {
            std::vector<NUIPoint> pts;
            ok.clear();
            for (size_t i = 0; i < v.size(); ++i) { pts.push_back({xAt(0.1 * static_cast<double>(i)), yAt(v[i] + gain)}); ok.push_back(v[i] > -70.0f); }
            return pts;
        };
        std::vector<bool> okA, okB;
        if (ref) plot(r, series(b, refGain, okB), okB, 1.4f, kRef);
        plot(r, series(a, 0.0, okA), okA, 2.0f, c.mix);
        if (position >= 0.0) r.fillRect({xAt(position), g.y, 1.5f, g.height}, NUIColor::white());

        // The four numbers that describe the dynamics, mix against reference.
        const float cy = g.bottom() + 24.0f, cw = (panel.width - 36.0f) / 4.0f;
        struct Card { const char* name; double a, b; const char* unit; };
        const auto& A = mix->analysis;
        const Card cards[] = {
            {"Integrated", A.integratedLufs, ref ? ref->analysis.integratedLufs : 0.0, "LUFS"},
            {"Loudness range", A.loudnessRangeLu, ref ? ref->analysis.loudnessRangeLu : 0.0, "LU"},
            {"PLR", Audio::peakToLoudnessRatioDb(A), ref ? Audio::peakToLoudnessRatioDb(ref->analysis) : 0.0, "dB"},
            {"True peak", A.truePeakDbtp, ref ? ref->analysis.truePeakDbtp : 0.0, "dBTP"}};
        for (int i = 0; i < 4; ++i) {
            const NUIRect card{panel.x + static_cast<float>(i) * (cw + 12.0f), cy, cw, 70.0f};
            r.fillRect(card, c.panel);
            r.drawText(cards[i].name, {card.x + 12.0f, card.y + 10.0f}, 10.0f, c.muted);
            const std::string av = cards[i].a > -150.0 ? fmt("%.1f", cards[i].a) : std::string("-");
            r.drawText(av, {card.x + 12.0f, card.y + 30.0f}, 22.0f, c.mix);
            float x = card.x + 12.0f + r.measureText(av, 22.0f).width + 8.0f;
            if (ref) {
                r.drawText("vs", {x, card.y + 40.0f}, 10.0f, c.muted);
                x += r.measureText("vs", 10.0f).width + 8.0f;
                const std::string bv = cards[i].b > -150.0 ? fmt("%.1f", cards[i].b) : std::string("-");
                r.drawText(bv, {x, card.y + 30.0f}, 22.0f, kRef);
                x += r.measureText(bv, 22.0f).width + 6.0f;
            }
            r.drawText(cards[i].unit, {x, card.y + 40.0f}, 10.0f, c.muted.withAlpha(0.8f));
        }

        // Each section's level and crest (peak over RMS: how much the peaks stand out), mix and reference side by side.
        const float ty = cy + 96.0f;
        bottom = ty;
        {
            const float cols[] = {panel.x, panel.x + panel.width * 0.22f, panel.x + panel.width * 0.40f, panel.x + panel.width * 0.62f, panel.x + panel.width * 0.80f};
            const char* heads[] = {"SECTION", "MIX RMS", "MIX CREST", "REF RMS", "REF CREST"};
            for (int i = 0; i < (ref ? 5 : 3); ++i) r.drawText(heads[i], {cols[i], ty}, 9.5f, i >= 3 ? kRef.withAlpha(0.8f) : c.muted.withAlpha(0.8f));
            const size_t rows = std::max(mix->sections.size(), ref ? ref->sections.size() : size_t{0});
            float y = ty + 20.0f;
            for (size_t i = 0; i < rows; ++i, y += 24.0f) {
                r.fillRect({panel.x, y - 4.0f, panel.width, 1.0f}, c.rule);
                r.drawText(std::to_string(i + 1), {cols[0], y + 2.0f}, 11.5f, c.muted);
                if (i < mix->sections.size()) {
                    const auto& s = mix->sections[i];
                    r.drawText(mmss(s.startSec), {cols[0] + 24.0f, y + 2.0f}, 11.5f, c.muted.withAlpha(0.8f));
                    r.drawText(levelDb(s.rmsDb), {cols[1], y + 2.0f}, 11.5f, c.mix);
                    r.drawText(s.rmsDb > -90.0 ? fmt("%.1f dB", s.crestDb) : std::string("-"), {cols[2], y + 2.0f}, 11.5f, c.mix);
                }
                if (ref && i < ref->sections.size()) {
                    const auto& s = ref->sections[i];
                    r.drawText(levelDb(s.rmsDb > -90.0 ? s.rmsDb + refGain : s.rmsDb), {cols[3], y + 2.0f}, 11.5f, kRef);
                    r.drawText(s.rmsDb > -90.0 ? fmt("%.1f dB", s.crestDb) : std::string("-"), {cols[4], y + 2.0f}, 11.5f, kRef);
                }
            }
            bottom = y + 8.0f;
        }
        break;
    }

    case ItemTab::Stereo: {
        if (mix->stereo.correlationOverTime.empty()) {
            r.drawTextCentered("This file is mono: it has no stereo image to measure.", p.panel, 12.0f, c.muted);
            break;
        }
        // Width per band: side energy as a share of the whole, drawn out from the centre.
        heading(r, "WIDTH BY BAND", panel.x, panel.y, c);
        float lx = panel.x + r.measureText("WIDTH BY BAND", 9.5f).width + 24.0f;
        lx = legend(r, lx, panel.y - 2.0f, c.mix, mixName, false);
        if (ref) legend(r, lx, panel.y - 2.0f, kRef, refName, false);
        const float labelW = 110.0f, valueW = 96.0f;
        const NUIRect track{panel.x + labelW, panel.y + 28.0f, panel.width - labelW - valueW, 5.0f * 30.0f};
        const float cx = track.x + track.width * 0.5f;
        r.fillRect({cx, track.y - 4.0f, 1.0f, track.height + 4.0f}, c.rule);
        for (size_t b = 0; b < 5; ++b) {
            const float y = track.y + 30.0f * static_cast<float>(b);
            r.drawText(kBandNames[b], {panel.x, y + 4.0f}, 11.5f, c.muted);
            r.fillRect({track.x, y + 12.0f, track.width, 1.0f}, c.rule.withAlpha(0.5f));
            const float wm = static_cast<float>(mix->stereo.bandWidth[b]) * track.width * 0.5f;
            r.fillRect({cx - wm, y + 6.0f, wm * 2.0f, 6.0f}, c.mixFill);
            std::string v = fmt("%.2f", mix->stereo.bandWidth[b]);
            if (ref && !ref->stereo.correlationOverTime.empty()) {
                const float wr = static_cast<float>(ref->stereo.bandWidth[b]) * track.width * 0.5f;
                r.fillRect({cx - wr, y + 15.0f, wr * 2.0f, 3.0f}, kRef);
                v += " / " + fmt("%.2f", ref->stereo.bandWidth[b]);
            }
            r.drawText(v, {track.right() + 16.0f, y + 4.0f}, 11.0f, c.text);
        }
        r.drawText("0 = mono, 1 = all side. Low end usually sits near the centre.", {panel.x, track.bottom() + 4.0f}, 10.0f, c.muted.withAlpha(0.75f));

        // Correlation over time: below zero the two sides cancel when they are summed to mono.
        const float gy = track.bottom() + 44.0f;
        heading(r, "CORRELATION OVER TIME", panel.x, gy, c);
        const NUIRect g{panel.x, gy + 22.0f, panel.width, 120.0f};
        r.fillRect(g, c.panel);
        const double span = std::max(mix->seconds, ref ? ref->seconds : 0.0);
        const auto xAt = [&](double sec) { return g.x + g.width * static_cast<float>(std::clamp(sec / std::max(0.001, span), 0.0, 1.0)); };
        const auto yAt = [&](double v) { return g.y + g.height * 0.5f - static_cast<float>(std::clamp(v, -1.0, 1.0)) * (g.height * 0.5f - 6.0f); };
        r.fillRect({g.x, yAt(0.0), g.width, 1.0f}, c.rule);
        r.drawText("+1", {g.x + 4.0f, g.y + 2.0f}, 9.0f, c.muted.withAlpha(0.6f));
        r.drawText("-1", {g.x + 4.0f, g.bottom() - 14.0f}, 9.0f, c.muted.withAlpha(0.6f));
        const double win = Audio::StereoDetail::kWindowSeconds;
        for (double start : mix->stereo.monoProblemStartsSec)
            r.fillRect({xAt(start), g.y, std::max(2.0f, xAt(start + win) - xAt(start)), g.height}, kWarn.withAlpha(0.25f));
        const auto series = [&](const std::vector<float>& v) {
            std::vector<NUIPoint> pts;
            for (size_t i = 0; i < v.size(); ++i) pts.push_back({xAt((static_cast<double>(i) + 0.5) * win), yAt(v[i])});
            return pts;
        };
        if (ref && !ref->stereo.correlationOverTime.empty()) {
            const auto pts = series(ref->stereo.correlationOverTime);
            plot(r, pts, std::vector<bool>(pts.size(), true), 1.2f, kRef.withAlpha(0.8f));
        }
        const auto pts = series(mix->stereo.correlationOverTime);
        plot(r, pts, std::vector<bool>(pts.size(), true), 1.8f, c.mix);
        if (position >= 0.0) r.fillRect({xAt(position), g.y, 1.5f, g.height}, NUIColor::white());

        // The stretches where the mix cancels in mono, merged into ranges.
        std::vector<std::pair<double, double>> ranges;
        for (double s : mix->stereo.monoProblemStartsSec) {
            if (!ranges.empty() && s <= ranges.back().second + 0.01) ranges.back().second = s + win;
            else ranges.push_back({s, s + win});
        }
        float ny = g.bottom() + 14.0f;
        if (ranges.empty()) {
            r.drawText("No stretch of your mix cancels in mono.", {panel.x, ny}, 11.5f, c.muted);
        } else {
            for (size_t i = 0; i < ranges.size() && i < 3; ++i, ny += 20.0f) {
                r.fillRect({panel.x, ny + 3.0f, 8.0f, 8.0f}, kWarn);
                r.drawText(mmss(ranges[i].first) + " to " + mmss(ranges[i].second) + ": the two sides work against each other, so this part loses level in mono.", {panel.x + 16.0f, ny}, 11.5f, c.muted);
            }
            if (ranges.size() > 3) { r.drawText(fmt("and %.0f more", static_cast<double>(ranges.size() - 3)), {panel.x + 16.0f, ny}, 11.0f, c.muted.withAlpha(0.7f)); ny += 20.0f; }
        }
        bottom = ny + 20.0f;
        break;
    }

    case ItemTab::Sections: {
        // The sections each track falls into. Click one to hear it: A and B run together, so the other track is at
        // the same moment, and a switch lands there.
        heading(r, "SECTION MAP", panel.x, panel.y, c);
        r.drawText("Found where the level and colour change. Click a section to play from it.", {panel.x + r.measureText("SECTION MAP", 9.5f).width + 24.0f, panel.y - 1.0f}, 10.5f, c.muted.withAlpha(0.8f));
        const auto lanes = sectionLanes(panel);
        const auto drawLane = [&](const NUIRect& lane, const AuditionCompare::Side& side, const NUIColor& color, const std::string& name, bool listening) {
            r.drawText(name, {lane.x, lane.y - 18.0f}, 10.5f, color);
            const auto blocks = sectionBlocks(lane, side.sections, side.seconds);
            for (size_t i = 0; i < blocks.size(); ++i) {
                const auto& s = side.sections[i];
                const bool here = listening && position >= s.startSec && position < s.endSec;
                r.fillRect(blocks[i], here ? color : color.withAlpha(0.12f));
                r.strokeRect(blocks[i], 1.0f, color.withAlpha(0.8f));
                const std::string label = std::to_string(i + 1) + "  " + mmss(s.startSec);
                if (r.measureText(label, 11.0f).width + 12.0f < blocks[i].width)
                    r.drawText(label, {blocks[i].x + 8.0f, std::round(r.calculateTextY(blocks[i], 11.0f))}, 11.0f, here ? NUIColor::black() : c.text);
            }
        };
        const bool onRef = m_engine && m_engine->getListenSide() == Audio::AuditionEngine::ListenSide::Reference;
        drawLane(lanes.mix, *mix, c.mix, "A  " + mixName, current && !(ref && onRef));
        if (ref) drawLane(lanes.ref, *ref, kRef, "B  " + refName, current && onRef);
        else r.drawText("Pick a reference above to set its sections beside these.", {lanes.ref.x, lanes.ref.y + 12.0f}, 11.5f, c.muted.withAlpha(0.8f));
        if (position >= 0.0) {
            const float x = lanes.mix.x + lanes.mix.width * static_cast<float>(std::clamp(position / std::max(0.001, mix->seconds), 0.0, 1.0));
            r.fillRect({x, lanes.mix.y - 4.0f, 1.5f, lanes.mix.height + 8.0f}, NUIColor::white());
        }
        bottom = lanes.ref.bottom() + 24.0f;
        break;
    }

    case ItemTab::Overview:
        break;
    }
    m_tabContentH = bottom - panel.y;
}

bool AuditionHub::itemTabMouse(const NUIMouseEvent& event, const AuditionHubLayout::ItemFrame& p) {
    if (m_tab != ItemTab::Sections || !(event.pressed && event.button == NUIMouseButton::Left) || !m_engine) return false;
    const auto mixIt = m_sides.find(m_item.path);
    if (mixIt == m_sides.end()) return false;
    const auto lanes = sectionLanes({p.panel.x, p.panel.y - m_tabScroll, p.panel.width, p.panel.height});
    if (!p.body.contains(event.position)) return false;
    const auto hit = [&](const NUIRect& lane, const AuditionCompare::Side& side, Audio::AuditionEngine::ListenSide listen) {
        const auto blocks = sectionBlocks(lane, side.sections, side.seconds);
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (!blocks[i].contains(event.position)) continue;
            if (!(m_hasNow && m_now.path == m_item.path)) { play(m_item); if (m_result) applyToEngine(); }  // start it, with the reference beside it
            if (m_result) m_engine->setListenSide(listen);
            m_engine->seekSeconds(side.sections[i].startSec);
            if (!m_engine->isPlaying()) m_engine->play();
            setDirty(true);
            return true;
        }
        return false;
    };
    if (hit(lanes.mix, *mixIt->second, Audio::AuditionEngine::ListenSide::Mix)) return true;
    return m_result && hit(lanes.ref, m_result->ref, Audio::AuditionEngine::ListenSide::Reference);
}

} // namespace Aestra
