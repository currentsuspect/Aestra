// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The Audition hub's page of one mix or reference, and the comparison between a mix and a reference.
#include "AuditionHub.h"
#include "AuditionCover.h"

#include "../AestraUI/Core/NUIThemeSystem.h"
#include "../AestraUI/Graphics/NUIRenderer.h"
#include "../../AestraCore/include/AestraLog.h"
#include "ClipSource.h"
#include "IO/MiniAudioDecoder.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <thread>

using namespace AestraUI;

namespace Aestra {

namespace {

std::string fmt(const char* f, double v) {
    char buf[48];
    std::snprintf(buf, sizeof buf, f, v);
    return buf;
}

std::string mmss(double seconds) {
    const int total = static_cast<int>(std::max(0.0, seconds));
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return buf;
}

std::string signedDb(double v) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%+.1f dB", v);
    return buf;
}

const NUIColor kRef = NUIColor::fromHex(0xfff5a83a);

} // namespace

// ---------------------------------------------------------------------------------------------------------------
// Measuring and comparing
// ---------------------------------------------------------------------------------------------------------------

void AuditionHub::ensureSide(const std::string& path) {
    if (path.empty() || m_sides.count(path) || std::find(m_requested.begin(), m_requested.end(), path) != m_requested.end()) return;
    m_requested.push_back(path);
    auto shared = m_sideShared;
    ++shared->working;
    std::thread([shared, path]() {
        std::vector<float> audio;
        uint32_t rate = 0, channels = 0;
        std::shared_ptr<const AuditionCompare::Side> side;
        if (Audio::decodeAudioFile(path, audio, rate, channels) && channels > 0 && rate > 0 && !audio.empty())
            side = std::make_shared<const AuditionCompare::Side>(AuditionCompare::measure(path, std::move(audio), channels, rate));
        std::lock_guard<std::mutex> lock(shared->mutex);
        if (side) shared->done.push_back(std::move(side));
        else shared->failed.push_back(path);
        --shared->working;
    }).detach();
}

void AuditionHub::applySides() {
    std::vector<std::shared_ptr<const AuditionCompare::Side>> done;
    std::vector<std::string> failed;
    {
        std::lock_guard<std::mutex> lock(m_sideShared->mutex);
        done.swap(m_sideShared->done);
        failed.swap(m_sideShared->failed);
    }
    for (const auto& path : failed) {
        m_requested.erase(std::remove(m_requested.begin(), m_requested.end(), path), m_requested.end());
        Log::warning("[Audition] Could not read " + path);
    }
    if (done.empty() && failed.empty()) return;
    for (auto& side : done) m_sides[side->path] = std::move(side);
    rebuildResult();
    setDirty(true);
}

void AuditionHub::rebuildResult() {
    m_result.reset();
    if (!m_hasItem || !m_ref) return;
    const auto mix = m_sides.find(m_item.path), ref = m_sides.find(m_ref->path);
    if (mix == m_sides.end() || ref == m_sides.end()) return;
    auto result = std::make_shared<AuditionCompare::Result>(AuditionCompare::compare(*mix->second, *ref->second));
    if (!result->ok) { Log::warning("[Audition] These two cannot be compared (one is silent)"); return; }
    m_result = std::move(result);
    applyToEngine();
}

void AuditionHub::applyToEngine() {
    if (!m_engine || !m_result) return;
    const double pos = m_engine->getPositionSeconds();
    const bool playing = m_engine->isPlaying();
    const auto source = [](const AuditionCompare::Side& side, const std::string& name) {
        auto s = std::make_shared<Audio::ClipSource>(Audio::ClipSourceID{1}, name);
        s->setBuffer(side.buffer);
        return s;
    };
    m_engine->setMixSource(source(m_result->mix, m_item.title));
    m_engine->setReferenceSource(source(m_result->ref, m_ref ? AuditionLibrary::displayName(*m_ref) : "reference"));
    m_engine->setMixGainDb(0.0f);
    m_engine->setReferenceGainDb(static_cast<float>(m_result->referenceGainDb + m_nudgeDb));
    m_engine->setListenSide(Audio::AuditionEngine::ListenSide::Mix);
    m_engine->seekSeconds(pos);
    if (playing) m_engine->play();
}

void AuditionHub::nudgeReference(double db) {
    m_nudgeDb = std::clamp(m_nudgeDb + db, -12.0, 12.0);
    if (m_engine && m_result) m_engine->setReferenceGainDb(static_cast<float>(m_result->referenceGainDb + m_nudgeDb));
    setDirty(true);
}

void AuditionHub::openItem(const AuditionLibrary::Item& item) {
    const bool sameItem = m_hasItem && m_item.path == item.path;
    m_item = item;
    m_hasItem = true;
    if (!sameItem) { m_ref.reset(); m_result.reset(); m_nudgeDb = 0.0; if (m_engine) m_engine->clearReferenceSource(); }
    ensureSide(item.path);
    setPage(Page::Item);
    if (!sameItem || !(m_engine && m_engine->isPlaying())) play(item);
}

void AuditionHub::chooseReference(const AuditionLibrary::Item& reference) {
    if (!m_hasItem || reference.path == m_item.path) return;
    m_ref = reference;
    m_nudgeDb = 0.0;
    m_result.reset();
    ensureSide(reference.path);
    rebuildResult();  // at once, if both are already measured
    setDirty(true);
}

void AuditionHub::clearReference() {
    m_ref.reset();
    m_result.reset();
    m_nudgeDb = 0.0;
    if (m_engine) { m_engine->clearReferenceSource(); m_engine->setListenSide(Audio::AuditionEngine::ListenSide::Mix); }
    setDirty(true);
}

void AuditionHub::openReferenceMenu(const NUIPoint& at) {
    if (m_menu) { if (auto* p = m_menu->getParent()) p->removeChild(m_menu); }
    m_menu = std::make_shared<NUIContextMenu>();
    const auto add = [&](const AuditionLibrary::Item& item) {
        if (item.path == m_item.path) return;
        auto entry = std::make_shared<NUIContextMenuItem>(AuditionLibrary::displayName(item));
        entry->setShortcut(item.hasLufs ? fmt("%.1f LUFS", item.lufs) : std::string());
        entry->setChecked(m_ref && m_ref->path == item.path);
        entry->setOnClick([this, item]() { chooseReference(item); });
        m_menu->addItem(entry);
    };
    for (const auto& r : m_references) add(r);
    if (!m_references.empty() && !m_versions.empty()) m_menu->addSeparator();
    for (const auto& v : m_versions) add(v);
    if (m_ref) {
        m_menu->addSeparator();
        m_menu->addItem("Stop comparing", [this]() { clearReference(); });
    }
    if (m_menu->getItemCount() == 0) { m_menu.reset(); return; }
    NUIComponent* root = this;
    while (root->getParent()) root = root->getParent();
    root->addChild(m_menu);
    m_menu->showAt(at);
    root->repaint();
}

// ---------------------------------------------------------------------------------------------------------------
// The page
// ---------------------------------------------------------------------------------------------------------------

void AuditionHub::renderItemPage(NUIRenderer& r, const AuditionHubLayout::Frame& f) {
    auto& theme = NUIThemeManager::getInstance();
    const NUIColor text = theme.getColor("textPrimary"), muted = theme.getColor("textSecondary"), rule = theme.getColor("borderSubtle");
    const NUIColor violet = theme.getColor("accentPrimary").lightened(0.25f);
    const bool withRail = m_result != nullptr;
    const auto p = AuditionHubLayout::itemFrame(f.content, withRail);
    r.setClipRect(f.content);

    // The hero: cover, what this is, its name, and the two things you do from here.
    r.fillRect(p.hero, NUIColor::fromHex(0xff1d1536));
    const bool ref = m_item.kind == AuditionLibrary::Item::Kind::Reference;
    drawCover(r, p.cover, m_item.seed);
    const float tx = p.cover.right() + 32.0f;
    const std::string kicker = ref ? "REFERENCE" : "MIX \xc2\xb7 VERSION " + m_item.badge.substr(m_item.badge.empty() ? 0 : 1) + " OF " + std::to_string(m_versions.size());
    r.drawText(kicker + (m_ref ? "  \xc2\xb7  COMPARING WITH " + AuditionLibrary::displayName(*m_ref) : ""), {tx, p.cover.y + 4.0f}, 10.5f, violet);
    r.drawText(m_item.title, {tx, p.cover.y + 28.0f}, 52.0f, text);
    std::string meta = m_item.subtitle;
    if (m_item.seconds > 0.0) meta += "  \xc2\xb7  " + mmss(m_item.seconds);
    r.drawText(meta, {tx, p.cover.y + 98.0f}, 12.0f, muted);

    const bool playingThis = m_engine && m_engine->isPlaying() && m_hasNow && m_now.path == m_item.path;
    const NUIPoint pc{p.play.x + p.play.width * 0.5f, p.play.y + p.play.height * 0.5f};
    r.fillCircle(pc, 28.0f, theme.getColor("accentPrimary"));
    if (playingThis) {
        r.fillRect({pc.x - 8.0f, pc.y - 9.0f, 5.0f, 18.0f}, NUIColor::white());
        r.fillRect({pc.x + 3.0f, pc.y - 9.0f, 5.0f, 18.0f}, NUIColor::white());
    } else {
        for (int i = 0; i < 14; ++i) {
            const float half = 10.0f * (1.0f - static_cast<float>(i) / 14.0f);
            r.fillRect({pc.x - 5.0f + static_cast<float>(i), pc.y - half, 1.0f, half * 2.0f}, NUIColor::white());
        }
    }
    const bool hot = m_hoverControl == "compare";
    r.fillRect(p.compare, hot ? NUIColor::white().withAlpha(0.06f) : NUIColor(0, 0, 0, 0));
    r.strokeRect(p.compare, 1.0f, m_ref ? kRef.withAlpha(0.9f) : muted.withAlpha(0.6f));
    if (m_ref) {
        r.drawText("Comparing with", {p.compare.x + 16.0f, std::round(r.calculateTextY(p.compare, 12.0f))}, 12.0f, muted);
        const float lw = r.measureText("Comparing with ", 12.0f).width;
        r.drawText(AuditionLibrary::displayName(*m_ref), {p.compare.x + 16.0f + lw, std::round(r.calculateTextY(p.compare, 12.0f))}, 12.0f, kRef);
    } else {
        r.drawText("Compare with a reference...", {p.compare.x + 16.0f, std::round(r.calculateTextY(p.compare, 12.0f))}, 12.0f, text);
    }

    // The waveform: the mix, filled; the reference over it, as an outline, drawn at the level it is heard at.
    r.drawText("WAVEFORM", {p.wave.x, p.wave.y - 24.0f}, 9.5f, muted.withAlpha(0.8f));
    float lx = p.wave.x + 78.0f;
    r.fillRect({lx, p.wave.y - 21.0f, 8.0f, 8.0f}, theme.getColor("accentPrimary"));
    r.drawText(m_item.badge.empty() ? "Mix" : "Mix " + m_item.badge, {lx + 14.0f, p.wave.y - 24.0f}, 10.5f, violet);
    if (m_ref && m_result) {
        lx += 90.0f;
        r.fillRect({lx, p.wave.y - 18.0f, 14.0f, 2.0f}, kRef);
        r.drawText(AuditionLibrary::displayName(*m_ref) + ", level-matched " + signedDb(m_result->referenceGainDb + m_nudgeDb), {lx + 20.0f, p.wave.y - 24.0f}, 10.5f, kRef);
    }
    r.fillRect(p.wave, theme.getColor("backgroundSecondary"));
    const auto sideIt = m_sides.find(m_item.path);
    const auto* mixSide = sideIt != m_sides.end() ? sideIt->second.get() : nullptr;
    if (!mixSide) {
        r.drawTextCentered(m_sideShared->working.load() > 0 ? "Measuring..." : "Could not read this file", p.wave, 12.0f, muted.withAlpha(0.7f));
    } else {
        const float mid = p.wave.y + p.wave.height * 0.5f, half = p.wave.height * 0.5f - 4.0f;
        const size_t n = mixSide->peaks.size();
        const float colW = p.wave.width / static_cast<float>(n);
        for (size_t i = 0; i < n; ++i) {
            const float h = std::max(1.0f, mixSide->peaks[i] * half);
            r.fillRect({p.wave.x + colW * static_cast<float>(i), mid - h, std::max(1.0f, colW), h * 2.0f}, theme.getColor("accentPrimary"));
        }
        if (m_result) {
            const auto& rp = m_result->ref.peaks;
            const float rcw = p.wave.width / static_cast<float>(rp.size());
            for (size_t i = 1; i < rp.size(); ++i) {
                const float h0 = rp[i - 1] * half, h1 = rp[i] * half;
                const float x0 = p.wave.x + rcw * static_cast<float>(i - 1), x1 = p.wave.x + rcw * static_cast<float>(i);
                r.drawLine({x0, mid - h0}, {x1, mid - h1}, 1.2f, kRef);
                r.drawLine({x0, mid + h0}, {x1, mid + h1}, 1.2f, kRef);
            }
        }
        const double duration = m_engine && m_engine->getDurationSeconds() > 0.0 ? m_engine->getDurationSeconds() : mixSide->seconds;
        const bool current = m_hasNow && m_now.path == m_item.path;
        if (current && duration > 0.0) {
            const float x = p.wave.x + p.wave.width * static_cast<float>(std::clamp(m_engine->getPositionSeconds() / duration, 0.0, 1.0));
            r.fillRect({x, p.wave.y, 1.5f, p.wave.height}, NUIColor::white());
        }
        // The sections the signal falls into, so a switch can be made to land in the matching one.
        for (size_t i = 0; i < mixSide->sections.size(); ++i) {
            const auto& sec = mixSide->sections[i];
            const float x = p.wave.x + p.wave.width * static_cast<float>(sec.startSec / std::max(0.001, mixSide->seconds));
            r.fillRect({x, p.sections.y, 1.0f, 12.0f}, muted.withAlpha(0.6f));
            r.drawText(std::to_string(i + 1), {x + 4.0f, p.sections.y + 1.0f}, 10.0f, muted.withAlpha(0.8f));
        }
    }

    // The numbers, side by side.
    r.drawText("COMPARE", {p.table.x, p.table.y}, 9.5f, muted.withAlpha(0.8f));
    if (mixSide) {
        const std::string mixHead = m_item.badge.empty() ? "Mix" : "Mix " + m_item.badge;
        const float col1 = p.table.x + p.table.width * 0.46f, col2 = p.table.x + p.table.width * 0.68f;
        r.drawText(mixHead, {col1, p.table.y + 24.0f}, 10.0f, violet.withAlpha(0.85f));
        if (m_result) r.drawText("Ref", {col2, p.table.y + 24.0f}, 10.0f, kRef.withAlpha(0.85f));
        struct Row { const char* name; std::string a, b; std::string note; };
        const auto width = [](const AuditionCompare::Side& s) { double sum = 0; for (double w : s.stereo.bandWidth) sum += w; return sum / s.stereo.bandWidth.size(); };
        std::vector<Row> rows;
        const auto& A = mixSide->analysis;
        const AuditionCompare::Side* refSide = m_result ? &m_result->ref : nullptr;
        const auto lufs = [](double v) { return v > -150.0 ? fmt("%.1f LUFS", v) : std::string("-"); };
        rows.push_back({"Integrated", lufs(A.integratedLufs), refSide ? lufs(refSide->analysis.integratedLufs) : "",
                        refSide ? "ref is " + fmt("%.1f", std::fabs(refSide->analysis.integratedLufs - A.integratedLufs)) + " dB " + (refSide->analysis.integratedLufs > A.integratedLufs ? "louder" : "quieter") : ""});
        rows.push_back({"Short-term max", lufs(A.maxShortTermLufs), refSide ? lufs(refSide->analysis.maxShortTermLufs) : "", ""});
        rows.push_back({"True peak", fmt("%.1f dBTP", A.truePeakDbtp), refSide ? fmt("%.1f dBTP", refSide->analysis.truePeakDbtp) : "",
                        refSide && A.truePeakDbtp > refSide->analysis.truePeakDbtp + 0.1 ? "yours clips on encode" : ""});
        rows.push_back({"PLR", fmt("%.1f dB", Audio::peakToLoudnessRatioDb(A)), refSide ? fmt("%.1f dB", Audio::peakToLoudnessRatioDb(refSide->analysis)) : "",
                        refSide ? (Audio::peakToLoudnessRatioDb(A) > Audio::peakToLoudnessRatioDb(refSide->analysis) ? "yours more dynamic" : "yours more compressed") : ""});
        rows.push_back({"Stereo width", fmt("%.2f", width(*mixSide)), refSide ? fmt("%.2f", width(*refSide)) : "", ""});
        rows.push_back({"Below 120 Hz", fmt("%.0f%%", A.bandShare[0] * 100.0), refSide ? fmt("%.0f%%", refSide->analysis.bandShare[0] * 100.0) : "",
                        refSide ? (A.bandShare[0] > refSide->analysis.bandShare[0] ? "yours heavier" : "yours lighter") : ""});
        float y = p.table.y + 46.0f;
        for (const auto& row : rows) {
            r.fillRect({p.table.x, y - 6.0f, p.table.width, 1.0f}, rule);
            r.drawText(row.name, {p.table.x, y + 8.0f}, 11.5f, muted);
            r.drawText(row.a, {col1, y + 8.0f}, 12.5f, violet);
            if (!row.b.empty()) r.drawText(row.b, {col2, y + 8.0f}, 12.5f, kRef);
            if (!row.note.empty()) {
                const float nw = r.measureText(row.note, 10.0f).width;
                r.drawText(row.note, {p.table.right() - nw, y + 10.0f}, 10.0f, muted.withAlpha(0.8f));
            }
            y += 36.0f;
        }
    }
    if (m_ref && !m_result) r.drawText(m_sideShared->working.load() > 0 ? "Measuring both..." : "Could not compare these two", {p.table.x, p.table.bottom() + 8.0f}, 11.5f, muted);

    // The rail: how the reference is matched to the mix.
    if (withRail) {
        r.fillRect({p.rail.x, p.rail.y, 1.0f, p.rail.height}, rule);
        const float rx = p.rail.x + 24.0f, rw = p.rail.width - 48.0f;
        r.drawText("LEVEL MATCH", {rx, p.rail.y + 28.0f}, 9.5f, muted.withAlpha(0.8f));
        const float aw = r.measureText("AUTO", 9.5f).width;
        r.drawText("AUTO", {rx + rw - aw, p.rail.y + 28.0f}, 9.5f, theme.getColor("accentCyan"));
        r.drawText(signedDb(m_result->referenceGainDb + m_nudgeDb), {rx, p.rail.y + 56.0f}, 34.0f, kRef);
        r.drawText("on the reference", {rx, p.rail.y + 100.0f}, 11.0f, muted);
        r.drawText("Match by", {rx, p.rail.y + 140.0f}, 11.5f, muted);
        const float mw = r.measureText("Integrated LUFS", 11.5f).width;
        r.drawText("Integrated LUFS", {rx + rw - mw, p.rail.y + 140.0f}, 11.5f, text);
        r.drawText("Nudge", {rx, p.rail.y + 176.0f}, 11.5f, muted);
        const NUIRect minus{rx + rw - 90.0f, p.rail.y + 168.0f, 42.0f, 28.0f}, plus{rx + rw - 42.0f, p.rail.y + 168.0f, 42.0f, 28.0f};
        for (const auto& b : {std::make_pair(minus, std::string("-0.5")), std::make_pair(plus, std::string("+0.5"))}) {
            r.fillRect(b.first, theme.getColor("surfaceTertiary"));
            const float tw = r.measureText(b.second, 11.0f).width;
            r.drawText(b.second, {b.first.x + (b.first.width - tw) * 0.5f, std::round(r.calculateTextY(b.first, 11.0f))}, 11.0f, text);
        }
        r.drawText("SWITCH", {rx, p.rail.y + 232.0f}, 9.5f, muted.withAlpha(0.8f));
        r.drawText("Same position, no gap", {rx, p.rail.y + 256.0f}, 11.5f, muted);
        r.drawText("A and B run together; a switch is a short ramp", {rx, p.rail.y + 276.0f}, 10.0f, muted.withAlpha(0.7f));
    }
    r.clearClipRect();
}

bool AuditionHub::itemPageMouse(const NUIMouseEvent& event, const AuditionHubLayout::Frame& f) {
    const auto p = AuditionHubLayout::itemFrame(f.content, m_result != nullptr);
    const NUIPoint pos = event.position;
    if (event.type == NUIMouseEventType::Move) {
        const std::string before = m_hoverControl;
        if (p.compare.contains(pos)) m_hoverControl = "compare";
        else if (m_hoverControl == "compare") m_hoverControl.clear();
        if (before != m_hoverControl) setDirty(true);
    }
    if (!(event.pressed && event.button == NUIMouseButton::Left) || !f.content.contains(pos)) return false;
    if (p.play.contains(pos)) {
        if (m_engine && m_engine->isPlaying() && m_hasNow && m_now.path == m_item.path) m_engine->pause();
        else play(m_item);
        setDirty(true);
        return true;
    }
    if (p.compare.contains(pos)) { openReferenceMenu({p.compare.x, p.compare.bottom() + 4.0f}); return true; }
    if (p.wave.contains(pos) && m_engine && m_hasNow && m_now.path == m_item.path) {
        m_engine->seekNormalized(std::clamp((pos.x - p.wave.x) / p.wave.width, 0.0f, 1.0f));
        return true;
    }
    if (m_result) {
        const float rx = p.rail.x + 24.0f, rw = p.rail.width - 48.0f;
        if (NUIRect(rx + rw - 90.0f, p.rail.y + 168.0f, 42.0f, 28.0f).contains(pos)) { nudgeReference(-0.5); return true; }
        if (NUIRect(rx + rw - 42.0f, p.rail.y + 168.0f, 42.0f, 28.0f).contains(pos)) { nudgeReference(0.5); return true; }
    }
    return true;
}

} // namespace Aestra
