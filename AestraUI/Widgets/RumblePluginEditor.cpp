// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumblePluginEditor.h"

#include "NUIDepth.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"
#include "Presets/PresetLibrary.h"
#include "RumbleFindSession.h"
#include "RumbleInstance.h"
#include "RumbleOutline.h"
#include "RumblePresetBank.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <vector>

namespace AestraUI {

static_assert(AestraUI::RumblePluginEditor::kPresetSlots == Aestra::Plugins::kRumbleFactoryPresets.size(),
              "one shelf row per starting sound");

using Aestra::Plugins::RumbleFindSession;
using Aestra::Plugins::RumbleOutline;
using Aestra::Plugins::RumbleSound;
using Aestra::Plugins::RumbleVoice;
namespace Variation = Aestra::Plugins::RumbleVariation;

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kPad = 12.0f;
constexpr float kShelfWidth = 150.0f;
constexpr float kKnobSize = 46.0f;
constexpr float kKnobRange = 180.0f;
// The picture's insets, as the sketch drew it.
constexpr float kPicX = 14.0f, kPicTop = 22.0f, kPicBottom = 18.0f;

// The six controls, in order, and the setting each one is.
constexpr uint32_t kControlIds[RumblePluginEditor::kControlCount] = {
    RumbleSound::Punch, RumbleSound::Tail, RumbleSound::Weight, RumbleSound::Tone, RumbleSound::Dirt, RumbleSound::Slide};
constexpr const char* kControlNames[RumblePluginEditor::kControlCount] = {"PUNCH", "TAIL", "WEIGHT", "TONE", "DIRT", "SLIDE"};
constexpr const char* kHoldNames[5] = {"Punch", "Tail", "Weight", "Tone", "Dirt"};
// The Everything page: the settings the six controls do not show, then how hard hits play.
constexpr uint32_t kEverythingIds[10] = {RumbleSound::DropTime, RumbleSound::Hold,     RumbleSound::Curve,
                                         RumbleSound::Second,   RumbleSound::Third,    RumbleSound::ClipType,
                                         RumbleSound::Click,    RumbleSound::ClickLength, RumbleSound::ClickTone,
                                         Aestra::Plugins::RumbleInstance::kVelocity};
constexpr const char* kEverythingNames[10] = {"DROP TIME", "HOLD", "CURVE", "2ND", "3RD",
                                              "CLIP", "CLICK", "CLICK LENGTH", "CLICK TONE", "VELOCITY"};
constexpr const char* kDistanceNames[3] = {"Close", "Further", "Far"};

NUIColor token(const char* name) {
    return NUIThemeManager::getInstance().getColor(name);
}

long long nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void textLeft(NUIRenderer& r, const std::string& text, const NUIRect& rect, float size, const NUIColor& color) {
    r.drawText(text, {std::round(rect.x), r.calculateOpticalTextY(rect, size)}, size, color);
}

void label(NUIRenderer& r, const std::string& text, const NUIRect& rect) {
    textLeft(r, text, rect, 10.0f, token("textMuted"));
}

// A pill button in the dense-row style: quiet hover, sunk when engaged.
void pill(NUIRenderer& r, const NUIRect& rect, const std::string& text, bool hovered, bool engaged, bool quiet = false) {
    const float radius = Depth::Radius::pill(rect.height);
    if (engaged) {
        Depth::bed(r, rect, radius);
    } else if (!quiet || hovered) {
        r.fillRoundedRect(rect, radius, token(hovered ? "surfaceRaised" : "surfaceTertiary"));
    }
    r.drawTextCentered(text, rect, 11.0f, token(engaged || hovered ? "textPrimary" : "textSecondary"));
}

void drawLock(NUIRenderer& r, NUIPoint at, const NUIColor& color) {
    r.fillRoundedRect({at.x - 3.5f, at.y - 0.5f, 7.0f, 5.0f}, 1.0f, color);
    std::array<NUIPoint, 9> arc{};
    for (size_t i = 0; i < arc.size(); ++i) {
        const float a = kPi + kPi * static_cast<float>(i) / static_cast<float>(arc.size() - 1);
        arc[i] = {at.x + std::cos(a) * 2.2f, at.y - 0.5f + std::sin(a) * 2.6f};
    }
    r.drawPolyline(arc.data(), static_cast<int>(arc.size()), 1.3f, color);
}

void drawArc(NUIRenderer& r, NUIPoint c, float radius, float from, float to, float thickness, const NUIColor& color) {
    if (to <= from + 0.001f) return;
    std::array<NUIPoint, 41> pts{};
    for (size_t i = 0; i < pts.size(); ++i) {
        const float a = from + (to - from) * static_cast<float>(i) / static_cast<float>(pts.size() - 1);
        pts[i] = {c.x + std::cos(a) * radius, c.y + std::sin(a) * radius};
    }
    r.drawPolyline(pts.data(), static_cast<int>(pts.size()), thickness, color);
}

bool near(const NUIPoint& a, const NUIPoint& b, float d) {
    return std::abs(a.x - b.x) <= d && std::abs(a.y - b.y) <= d;
}

std::string fmt(const char* f, double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}
} // namespace

RumblePluginEditor::RumblePluginEditor(std::shared_ptr<Aestra::Audio::IPluginInstance> instance)
    : m_instance(std::move(instance)), m_outlines(std::make_unique<Aestra::Plugins::RumbleOutlineWorker>(48000.0)) {
    setId("RumblePluginEditor");
    setPanelTitle("Aestra Rumble");
    setBadgeText("808");
    setSize(kWindowWidth, kWindowHeight);
    setEnforceParentBounds(true);

    m_keptStore = std::make_unique<Aestra::Plugins::RumbleKept>(Aestra::Audio::Presets::PresetLibrary::defaultUserRoot());
    refreshKept();
    startFromInstrument();
    layout();
}

namespace {
bool sameSound(const RumbleSound& a, const RumbleSound& b) {
    // The instrument keeps its settings as floats, so compare to well under a knob step.
    for (size_t id = 0; id < RumbleSound::Count; ++id) {
        if (!(std::abs(a[id] - b[id]) < 1.0e-4)) return false;
    }
    return a.seed == b.seed;
}
} // namespace

void RumblePluginEditor::startFromInstrument() {
    // Start from the sound the track already has, named after the starting sound it matches.
    const RumbleSound start = instrumentSound();
    std::string name = "Your sound";
    m_startPreset = kPresetSlots; // none of the starting sounds, unless it matches one
    for (size_t i = 0; i < Aestra::Plugins::kRumbleFactoryPresets.size(); ++i) {
        if (sameSound(Aestra::Plugins::rumbleSoundOf(Aestra::Plugins::kRumbleFactoryPresets[i]), start)) {
            name = std::string(Aestra::Plugins::kRumbleFactoryPresets[i].name);
            m_startPreset = i;
            break;
        }
    }
    m_startKept = -1; // or one of Your 808s, loaded from the preset bar
    for (size_t i = 0; i < m_kept.size() && m_startPreset == kPresetSlots; ++i) {
        if (sameSound(m_kept[i].sound, start)) {
            name = m_kept[i].name;
            m_startKept = static_cast<int>(i);
            break;
        }
    }
    m_outlines->forgetPending();
    m_session = std::make_unique<RumbleFindSession>(start, name, static_cast<uint64_t>(nowMs()));
    m_lastPicture.reset();
    soundChanged();
}

void RumblePluginEditor::followInstrument() {
    if (m_session && !sameSound(instrumentSound(), m_session->current())) startFromInstrument();
}

RumblePluginEditor::~RumblePluginEditor() = default;

RumbleSound RumblePluginEditor::instrumentSound() const {
    if (auto* rumble = dynamic_cast<Aestra::Plugins::RumbleInstance*>(m_instance.get())) {
        return rumble->sound();
    }
    RumbleSound sound;
    for (uint32_t id = 0; id < RumbleSound::Count; ++id) {
        sound[id] = m_instance ? m_instance->getParameter(id) : 0.5;
    }
    return sound;
}

void RumblePluginEditor::soundChanged() {
    const RumbleSound& sound = m_session->current();
    if (auto* rumble = dynamic_cast<Aestra::Plugins::RumbleInstance*>(m_instance.get())) {
        rumble->setSound(sound);
    } else if (m_instance) {
        for (uint32_t id = 0; id < RumbleSound::Count; ++id) m_instance->setParameter(id, static_cast<float>(sound[id]));
    }
    // Outlines: the six and the parent, then the picked sound last so it is made first.
    const auto& step = m_session->step(m_session->currentStep());
    for (const auto& s : step.six) m_outlines->request(s);
    m_outlines->request(step.parent);
    m_outlines->request(sound);
    setDirty(true);
}

void RumblePluginEditor::startFrom(size_t presetIndex) {
    const auto& preset = Aestra::Plugins::kRumbleFactoryPresets[presetIndex % Aestra::Plugins::kRumbleFactoryPresets.size()];
    m_startPreset = presetIndex;
    m_startKept = -1;
    m_outlines->forgetPending();
    m_session = std::make_unique<RumbleFindSession>(Aestra::Plugins::rumbleSoundOf(preset), std::string(preset.name),
                                                    static_cast<uint64_t>(nowMs()));
    m_lastPicture.reset();
    soundChanged();
}

void RumblePluginEditor::refreshKept() {
    m_kept = m_keptStore->list(); // a folder listing of small files: only on open, keep and undo
    setDirty(true);
}

void RumblePluginEditor::startFromKept(size_t index) {
    if (index >= m_kept.size()) return;
    m_startKept = static_cast<int>(index);
    m_outlines->forgetPending();
    m_session = std::make_unique<RumbleFindSession>(m_kept[index].sound, m_kept[index].name, static_cast<uint64_t>(nowMs()));
    m_lastPicture.reset();
    soundChanged();
}

void RumblePluginEditor::keep() {
    const auto kept = m_keptStore->keep(m_session->current(), m_session->currentName());
    if (!kept) {
        m_lastKept.reset(); // Undo must never reach back to an earlier keep
        m_editorSaid = "Could not keep it: the presets folder could not be written.";
    } else {
        m_lastKept = kept;
        m_editorSaid = "Kept " + kept->name + " in Your 808s. It opens in any project.";
        refreshKept();
    }
    setDirty(true);
}

void RumblePluginEditor::undoKeep() {
    if (!m_lastKept) return;
    if (!m_keptStore->unkeep(*m_lastKept)) {
        // Still kept: say so and leave Undo there to try again.
        m_editorSaid = "Could not take " + m_lastKept->name + " out of Your 808s. Try Undo again.";
        setDirty(true);
        return;
    }
    m_editorSaid = "Took " + m_lastKept->name + " back out of Your 808s.";
    m_lastKept.reset();
    refreshKept();
}

// ---------------------------------------------------------------------------------------------------------------
// Layout
// ---------------------------------------------------------------------------------------------------------------

void RumblePluginEditor::layout() {
    const NUIRect b = getBounds();
    const float top = b.y + headerHeight() + 10.0f;
    const float x0 = b.x + kPad;

    m_shelf = {x0, top, kShelfWidth, b.bottom() - top - kPad};
    for (size_t i = 0; i < m_shelfRows.size(); ++i) {
        m_shelfRows[i] = {x0, top + 22.0f + static_cast<float>(i) * 28.0f, kShelfWidth, 26.0f};
    }

    const float mx = x0 + kShelfWidth + 14.0f;
    const float mw = b.right() - kPad - mx;
    m_trail = {mx, top, mw, 24.0f};

    const float cy = m_trail.bottom() + 8.0f;
    const float gap = 6.0f, sep = 9.0f;
    const float cw = (mw - gap * 6.0f - sep) / 7.0f;
    float x = mx;
    for (size_t i = 0; i < m_cards.size(); ++i) {
        m_cards[i] = {x, cy, cw, 88.0f};
        x += cw + gap + (i == 0 ? sep : 0.0f);
    }

    const float ay = m_cards[0].bottom() + 10.0f;
    m_more = {mx, ay, 110.0f, 28.0f};
    float dx = m_more.right() + 10.0f;
    for (size_t i = 0; i < m_distance.size(); ++i) m_distance[i] = {dx + 3.0f + static_cast<float>(i) * 56.0f, ay + 3.0f, 56.0f, 22.0f};
    dx = m_distance.back().right() + 3.0f + 42.0f; // room for the HOLD label
    const float holdWidths[5] = {56.0f, 46.0f, 62.0f, 50.0f, 46.0f};
    for (size_t i = 0; i < m_holds.size(); ++i) {
        m_holds[i] = {dx, ay + 2.0f, holdWidths[i], 24.0f};
        dx += holdWidths[i] + 3.0f;
    }
    m_keep = {b.right() - kPad - 54.0f, ay, 54.0f, 28.0f};
    m_back = {m_keep.x - 54.0f, ay, 52.0f, 28.0f};

    m_picture = {mx, ay + 38.0f, mw, 176.0f};
    const float ky = m_picture.bottom() + 10.0f;
    const float colW = mw / static_cast<float>(kControlCount);
    for (size_t i = 0; i < m_controls.size(); ++i) m_controls[i] = {mx + colW * static_cast<float>(i), ky, colW, 92.0f};
    m_said = {mx, m_controls[0].bottom() + 4.0f, mw, 20.0f};
    m_everythingToggle = {b.right() - kPad - 92.0f, m_said.y - 2.0f, 92.0f, 24.0f};
    m_undo = {m_everythingToggle.x - 64.0f, m_said.y - 2.0f, 58.0f, 24.0f};
    const float keptTop = m_shelfRows.back().bottom() + 40.0f;
    for (size_t i = 0; i < m_keptRows.size(); ++i) m_keptRows[i] = {x0, keptTop + static_cast<float>(i) * 28.0f, kShelfWidth, 26.0f};
    const float ew = (m_picture.width - 16.0f) / 5.0f;
    for (size_t i = 0; i < m_everything.size(); ++i) {
        const float ex = m_picture.x + 8.0f + ew * static_cast<float>(i % 5);
        const float ey = m_picture.y + 26.0f + 72.0f * static_cast<float>(i / 5);
        m_everything[i] = {ex, ey, ew, 64.0f};
    }
}

void RumblePluginEditor::onResize(int width, int height) {
    AestraPanelWindow::onResize(width, height);
    layout();
}

float RumblePluginEditor::pictureX(double seconds) const {
    return m_picture.x + kPicX + (m_picture.width - kPicX * 2.0f) * static_cast<float>(RumbleOutline::xOf(seconds));
}

float RumblePluginEditor::pictureDropY(double semitones) const {
    const float bottom = m_picture.bottom() - kPicBottom - 4.0f;
    const float span = m_picture.height - kPicTop - kPicBottom - 8.0f;
    return bottom - static_cast<float>(semitones / RumbleVoice::dropSemitones(1.0)) * span;
}

// ---------------------------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------------------------

void RumblePluginEditor::drawContent(NUIRenderer& renderer, const NUIRect& contentRect) {
    (void)contentRect;
    followInstrument();
    layout();
    const NUIRect b = getBounds();
    renderer.fillRect({b.x, b.y + headerHeight(), b.width, b.height - headerHeight()}, token("backgroundSecondary"));
    drawShelf(renderer);
    drawTrail(renderer);
    drawRow(renderer);
    drawActions(renderer);
    if (m_showEverything) {
        drawEverything(renderer);
    } else {
        drawPicture(renderer);
    }
    drawControls(renderer);
    drawSaid(renderer);
}

void RumblePluginEditor::drawShelf(NUIRenderer& r) {
    label(r, "START FROM", {m_shelf.x + 2.0f, m_shelf.y, m_shelf.width, 16.0f});
    for (size_t i = 0; i < m_shelfRows.size(); ++i) {
        const NUIRect& row = m_shelfRows[i];
        const bool on = m_startKept < 0 && i == m_startPreset && m_session->stepCount() == 1 &&
                        m_session->selected() == RumbleFindSession::kParent;
        const bool hovered = row.contains(m_hover);
        if (on) {
            r.fillRoundedRect(row, Depth::Radius::medium, token("surfaceTertiary"));
            r.fillRoundedRect(row, Depth::Radius::medium, token("selected"));
        } else if (hovered) {
            r.fillRoundedRect(row, Depth::Radius::medium, token("hover"));
        }
        textLeft(r, std::string(Aestra::Plugins::kRumbleFactoryPresets[i].name), {row.x + 10.0f, row.y, row.width - 12.0f, row.height},
                 12.0f, token(on || hovered ? "textPrimary" : "textSecondary"));
    }
    const float y = m_shelfRows.back().bottom() + 18.0f;
    label(r, "YOUR 808S", {m_shelf.x + 2.0f, y, m_shelf.width, 16.0f});
    if (m_kept.empty()) {
        const NUIRect well{m_shelf.x, y + 22.0f, m_shelf.width, 64.0f};
        Depth::bed(r, well, Depth::Radius::medium);
        r.drawTextCentered("Keep a sound to find", {well.x, well.y + 16.0f, well.width, 14.0f}, 11.0f, token("textMuted"));
        r.drawTextCentered("it here in any project.", {well.x, well.y + 32.0f, well.width, 14.0f}, 11.0f, token("textMuted"));
        return;
    }
    for (size_t i = 0; i < m_keptRows.size() && i < m_kept.size(); ++i) {
        const NUIRect& row = m_keptRows[i];
        const bool on = static_cast<int>(i) == m_startKept && m_session->stepCount() == 1 && m_session->selected() == RumbleFindSession::kParent;
        const bool hovered = row.contains(m_hover);
        if (on) {
            r.fillRoundedRect(row, Depth::Radius::medium, token("surfaceTertiary"));
            r.fillRoundedRect(row, Depth::Radius::medium, token("selected"));
        } else if (hovered) {
            r.fillRoundedRect(row, Depth::Radius::medium, token("hover"));
        }
        textLeft(r, m_kept[i].name, {row.x + 10.0f, row.y, row.width - 12.0f, row.height}, 12.0f,
                 token(on || hovered ? "textPrimary" : "textSecondary"));
    }
    if (m_kept.size() > m_keptRows.size()) {
        const std::string more = std::to_string(m_kept.size() - m_keptRows.size()) + " more in the preset bar";
        textLeft(r, more, {m_shelf.x + 10.0f, m_keptRows.back().bottom() + 4.0f, m_shelf.width, 16.0f}, 10.0f, token("textMuted"));
    }
}

void RumblePluginEditor::drawTrail(NUIRenderer& r) {
    const size_t steps = m_session->stepCount();
    const auto textOf = [this](size_t i) {
        return i == 0 ? "Start: " + m_session->step(0).parentName : m_session->step(i).parentName;
    };
    // The steps that fit around the current one; a "..." either side reaches the hidden ones (kOverflow each).
    // The UI font has no single-angle quotes, so the ends are dots.
    constexpr float kOverflow = 40.0f;
    constexpr size_t kMaxSteps = 16;
    std::vector<float> widths(steps);
    for (size_t i = 0; i < steps; ++i) widths[i] = r.measureText(textOf(i), 11.0f).width + 20.0f + 14.0f;
    auto window = Aestra::Plugins::fitTrail(widths, m_trail.width - 120.0f - 2.0f * kOverflow, m_session->currentStep());
    if (window.end - window.first > kMaxSteps) {
        window.first = std::max(window.first, std::min(m_session->currentStep(), window.end - kMaxSteps));
        window.end = std::min(window.end, window.first + kMaxSteps);
    }

    float x = m_trail.x;
    m_trailPillCount = 0;
    const auto pill = [&](const std::string& text, size_t step, bool current) {
        const float w = r.measureText(text, 11.0f).width + 20.0f;
        const NUIRect pillRect{x, m_trail.y + 1.0f, w, 22.0f};
        m_trailPills[m_trailPillCount] = pillRect;
        m_trailSteps[m_trailPillCount++] = step;
        const bool hovered = pillRect.contains(m_hover);
        if (current || hovered) r.fillRoundedRect(pillRect, 11.0f, token(current ? "surfaceRaised" : "surfaceTertiary"));
        r.drawTextCentered(text, pillRect, 11.0f, token(current || hovered ? "textPrimary" : "textMuted"));
        x += w + 2.0f;
    };
    const auto arrow = [&]() {
        r.drawText(">", {x + 1.0f, r.calculateOpticalTextY(m_trail, 10.0f)}, 10.0f, token("textMuted"));
        x += 12.0f;
    };
    if (window.first > 0) {
        pill("...", window.first - 1, false); // one step further back
        arrow();
    }
    for (size_t i = window.first; i < window.end; ++i) {
        if (i > window.first) arrow();
        pill(textOf(i), i, i == m_session->currentStep());
    }
    if (window.end < steps) {
        arrow();
        pill("...", window.end, false); // the next hidden step
    }
    const std::string count = std::to_string(steps) + (steps == 1 ? " step, " : " steps, ") +
                              std::to_string(m_session->soundsMade()) + " sounds";
    const float cw = r.measureText(count, 11.0f).width;
    textLeft(r, count, {m_trail.right() - cw, m_trail.y, cw, m_trail.height}, 11.0f, token("textMuted"));
}

bool RumblePluginEditor::drawOutline(NUIRenderer& r, const NUIRect& rect, const RumbleSound& sound, const NUIColor& color,
                                     bool picture) {
    std::shared_ptr<const RumbleOutline> outline = m_outlines->find(sound);
    if (picture) {
        if (outline) {
            m_lastPicture = outline;
        } else {
            outline = m_lastPicture; // the shaped sound's outline is on its way; keep the last one meanwhile
            setDirty(true);          // and keep drawing until it arrives
        }
    }
    if (!outline) return false;
    std::array<NUIPoint, RumbleOutline::kBins> top{}, bottom{};
    const float mid = rect.y + rect.height * 0.5f;
    const float amp = rect.height * 0.5f;
    for (size_t j = 0; j < RumbleOutline::kBins; ++j) {
        const float x = rect.x + rect.width * static_cast<float>(RumbleOutline::xOfBin(j));
        const float a = amp * std::min(1.0f, outline->bins[j] / static_cast<float>(RumbleVoice::kLevelledPeak));
        top[j] = {x, mid - a};
        bottom[j] = {x, mid + a};
    }
    r.fillWaveform(top.data(), bottom.data(), static_cast<int>(RumbleOutline::kBins), color);
    return true;
}

void RumblePluginEditor::drawCard(NUIRenderer& r, const NUIRect& rect, int index) {
    const bool parent = index == RumbleFindSession::kParent;
    const bool selected = index == m_session->selected();
    const bool hovered = rect.contains(m_hover);
    const auto& step = m_session->step(m_session->currentStep());
    const RumbleSound& sound = parent ? step.parent : step.six[static_cast<size_t>(index)];

    if (parent) {
        Depth::bed(r, rect, Depth::Radius::medium);
        if (selected) r.fillRoundedRect(rect, Depth::Radius::medium, token("selected"));
        r.drawText("FROM", {rect.right() - 34.0f, rect.y + 6.0f}, 9.0f, token("textMuted"));
    } else {
        r.fillRoundedRect(rect, Depth::Radius::medium, token(hovered && !selected ? "surfaceRaised" : "surfaceTertiary"));
        if (selected) r.fillRoundedRect(rect, Depth::Radius::medium, token("selected"));
    }
    const NUIRect art{rect.x + 8.0f, rect.y + 10.0f, rect.width - 16.0f, 46.0f};
    if (!drawOutline(r, art, sound, token(selected ? "primary" : "textMuted"), false)) {
        // Still being made: a sunk slot with a sweep, since how long it takes is not known.
        const NUIRect slot{art.x, art.y + art.height * 0.5f - 3.0f, art.width, 6.0f};
        Depth::bed(r, slot, 3.0f, token("meterBackground"), Depth::Cut::Slot);
        const float phase = static_cast<float>(nowMs() % 1200) / 1200.0f;
        const float w = slot.width * 0.32f;
        const float sx = slot.x - w + (slot.width + w) * phase;
        const float l = std::max(slot.x, sx), rr = std::min(slot.right(), sx + w);
        if (rr > l) r.fillRoundedRect({l, slot.y + 1.0f, rr - l, 4.0f}, 2.0f, token("primary"));
        setDirty(true);
    }
    const std::string name = m_session->nameOf(index);
    const NUIRect nameRect{rect.x + 8.0f, rect.bottom() - 24.0f, rect.width - 16.0f, 16.0f};
    textLeft(r, name, nameRect, 11.0f, token(selected || hovered ? "textPrimary" : "textSecondary"));
    if (m_session->shaped(index)) {
        const float nx = nameRect.x + r.measureText(name, 11.0f).width + 7.0f;
        r.fillCircle({nx, nameRect.y + nameRect.height * 0.5f}, 2.5f, token("secondary"));
    }
}

void RumblePluginEditor::drawRow(NUIRenderer& r) {
    drawCard(r, m_cards[0], RumbleFindSession::kParent);
    const float sx = m_cards[0].right() + 7.5f;
    r.fillRect({sx, m_cards[0].y + 8.0f, 1.0f, m_cards[0].height - 16.0f}, token("divider"));
    for (size_t i = 0; i < RumbleFindSession::kSix; ++i) drawCard(r, m_cards[i + 1], static_cast<int>(i));
}

void RumblePluginEditor::drawActions(NUIRenderer& r) {
    // More like this: the one accent action.
    const bool moreHover = m_more.contains(m_hover);
    r.fillRoundedRect(m_more, Depth::Radius::pill(m_more.height), token(moreHover ? "primaryHover" : "primary"));
    r.drawTextCentered("More like this", m_more, 11.0f, token("textOnPrimary"));

    // Distance: a segment switch, a lit thumb in a sunk track.
    const NUIRect track{m_distance[0].x - 3.0f, m_distance[0].y - 3.0f, m_distance.back().right() - m_distance[0].x + 6.0f, 28.0f};
    Depth::bed(r, track, Depth::Radius::pill(track.height));
    const size_t chosen = static_cast<size_t>(m_session->distance());
    Depth::object(r, m_distance[chosen], Depth::Radius::pill(m_distance[chosen].height));
    for (size_t i = 0; i < m_distance.size(); ++i) {
        const bool on = i == chosen;
        r.drawTextCentered(kDistanceNames[i], m_distance[i], 11.0f,
                           token(on || m_distance[i].contains(m_hover) ? "textPrimary" : "textMuted"));
    }

    label(r, "HOLD", {m_holds[0].x - 37.0f, m_holds[0].y, 34.0f, m_holds[0].height});
    for (size_t i = 0; i < m_holds.size(); ++i) {
        const bool held = m_session->held(static_cast<Variation::Quality>(i));
        const NUIRect& chip = m_holds[i];
        if (held) {
            Depth::bed(r, chip, Depth::Radius::pill(chip.height));
            drawLock(r, {chip.x + 11.0f, chip.y + chip.height * 0.5f}, token("secondary"));
            r.drawTextCentered(kHoldNames[i], {chip.x + 8.0f, chip.y, chip.width - 6.0f, chip.height}, 11.0f, token("textPrimary"));
        } else {
            pill(r, chip, kHoldNames[i], chip.contains(m_hover), false);
        }
    }
    pill(r, m_back, "Back", m_back.contains(m_hover), false, true);
    pill(r, m_keep, "Keep", m_keep.contains(m_hover), false);
}

void RumblePluginEditor::drawPicture(NUIRenderer& r) {
    Depth::bed(r, m_picture, 6.0f, token("editorWell"), Depth::Cut::Deep);
    const RumbleSound& sound = m_session->current();
    const float x0 = m_picture.x + kPicX;
    const float w = m_picture.width - kPicX * 2.0f;
    const float yTop = m_picture.y + kPicTop, yBottom = m_picture.bottom() - kPicBottom;

    for (double t : {0.1, 0.5, 1.0, 2.0, 3.0}) {
        const float x = std::round(pictureX(t)) + 0.5f;
        r.drawLine({x, yTop - 6.0f}, {x, yBottom + 4.0f}, 1.0f, token("gridMinor"));
        r.drawText(t < 1.0 ? fmt("%.0f ms", t * 1000.0) : fmt("%.0f s", t), {x + 3.0f, m_picture.bottom() - 13.0f}, 9.0f,
                   token("textMuted"));
    }
    drawOutline(r, {x0, yTop, w, yBottom - yTop}, sound, token("primary"), true);

    // The pitch line over the front.
    std::array<NUIPoint, 120> pitch{};
    for (size_t i = 0; i < pitch.size(); ++i) {
        const double t = 0.8 * static_cast<double>(i) / static_cast<double>(pitch.size() - 1);
        pitch[i] = {pictureX(t), pictureDropY(RumbleOutline::pitchAbove(sound, t))};
    }
    r.drawPolyline(pitch.data(), static_cast<int>(pitch.size()), 1.5f, token("textPrimary"));

    // The two handles: the only objects on the picture.
    const double length = RumbleVoice::holdSeconds(sound[RumbleSound::Hold]) + RumbleVoice::tailSeconds(sound[RumbleSound::Tail]);
    const NUIPoint sq{pictureX(length), yTop + (yBottom - yTop) * 0.5f};
    const NUIPoint dot{x0, pictureDropY(RumbleVoice::dropSemitones(sound[RumbleSound::Punch]))};
    Depth::object(r, {sq.x - 6.0f, sq.y - 6.0f, 12.0f, 12.0f}, 3.0f, m_dragging == Handle::Length);
    r.fillRoundedRect({sq.x - 2.0f, sq.y - 2.0f, 4.0f, 4.0f}, 1.0f, token("textPrimary"));
    Depth::object(r, {dot.x - 6.0f, dot.y - 6.0f, 12.0f, 12.0f}, 6.0f, m_dragging == Handle::Drop);
    r.fillCircle(dot, 2.0f, token("textPrimary"));
    r.drawText("pitch falls", {x0 + 12.0f, std::max(yTop, dot.y - 16.0f)}, 9.0f, token("textSecondary"));

    // What Rumble already did, and the numbers.
    const std::string info = m_session->currentName() + "  ·  " + fmt("%.2f s", length) + "  ·  falls " +
                             fmt("%.1f st", RumbleVoice::dropSemitones(sound[RumbleSound::Punch])) + " in " +
                             fmt("%.0f ms", RumbleVoice::dropSeconds(sound) * 1000.0);
    const float iw = r.measureText(info, 11.0f).width;
    r.drawText(info, {m_picture.right() - 12.0f - iw, m_picture.y + 7.0f}, 11.0f, token("textSecondary"));
    r.drawText("Levelled  ·  Sub in mono  ·  Lands on its note", {x0, m_picture.y + 7.0f}, 10.0f, token("textMuted"));
}

void RumblePluginEditor::drawControls(NUIRenderer& r) {
    const auto& step = m_session->step(m_session->currentStep());
    const int sel = m_session->selected();
    const RumbleSound& arrived = sel == RumbleFindSession::kParent ? step.parentArrived : step.arrived[static_cast<size_t>(sel)];
    const RumbleSound& sound = m_session->current();
    constexpr float start = kPi * 0.75f, sweep = kPi * 1.5f;
    for (size_t i = 0; i < kControlCount; ++i) {
        const uint32_t id = kControlIds[i];
        const NUIRect& col = m_controls[i];
        const float v = static_cast<float>(sound[id]);
        const bool shaped = std::abs(sound[id] - arrived[id]) > 1.0e-9;
        const float lw = r.measureText(kControlNames[i], 10.0f).width;
        const float lx = col.x + (col.width - lw) * 0.5f;
        r.drawText(kControlNames[i], {lx, col.y}, 10.0f, token("textMuted"));
        if (shaped) r.fillCircle({lx + lw + 6.0f, col.y + 6.0f}, 2.5f, token("secondary"));

        const NUIPoint c{col.x + col.width * 0.5f, col.y + 16.0f + kKnobSize * 0.5f};
        const float radius = kKnobSize * 0.5f - 2.0f;
        drawArc(r, c, radius, start, start + sweep, 3.2f, token("meterBackground"));
        drawArc(r, c, radius, start, start + sweep * v, 3.2f, token("primary"));
        const float capR = kKnobSize * 0.29f;
        Depth::object(r, {c.x - capR, c.y - capR, capR * 2.0f, capR * 2.0f}, capR, knobDragParam() == static_cast<int>(id));
        const float a = start + sweep * v;
        r.drawLine({c.x + std::cos(a) * capR * 0.25f, c.y + std::sin(a) * capR * 0.25f},
                   {c.x + std::cos(a) * capR * 0.85f, c.y + std::sin(a) * capR * 0.85f}, 2.0f, token("textPrimary"));
        const std::string value = m_instance ? m_instance->getParameterDisplay(id) : std::string();
        r.drawTextCentered(value, {col.x, c.y + kKnobSize * 0.5f + 4.0f, col.width, 14.0f}, 11.0f, token("textPrimary"));
    }
}

void RumblePluginEditor::drawSaid(NUIRenderer& r) {
    textLeft(r, m_editorSaid.empty() ? m_session->said() : m_editorSaid, m_said, 12.0f, token("textSecondary"));
    if (m_lastKept && !m_editorSaid.empty()) pill(r, m_undo, "Undo", m_undo.contains(m_hover), false);
    pill(r, m_everythingToggle, "Everything", m_everythingToggle.contains(m_hover), m_showEverything);
}

// The rest of the sound's settings on one flat page, in the picture's place (SPEC-025 R8).
void RumblePluginEditor::drawEverything(NUIRenderer& r) {
    Depth::bed(r, m_picture, 6.0f, token("editorWell"), Depth::Cut::Deep);
    r.drawText("Everything else about the sound. The six controls below stay as they are.", {m_picture.x + 14.0f, m_picture.y + 7.0f},
               10.0f, token("textMuted"));
    const auto& step = m_session->step(m_session->currentStep());
    const int sel = m_session->selected();
    const RumbleSound& arrived = sel == RumbleFindSession::kParent ? step.parentArrived : step.arrived[static_cast<size_t>(sel)];
    const RumbleSound& sound = m_session->current();
    constexpr float start = kPi * 0.75f, sweep = kPi * 1.5f;
    for (size_t i = 0; i < m_everything.size(); ++i) {
        const uint32_t id = kEverythingIds[i];
        const NUIRect& cell = m_everything[i];
        const bool velocity = id >= RumbleSound::Count;
        const float v = velocity ? (m_instance ? m_instance->getParameter(id) : 0.0f) : static_cast<float>(sound[id]);
        const NUIPoint c{cell.x + 22.0f, cell.y + 26.0f};
        drawArc(r, c, 15.0f, start, start + sweep, 2.6f, token("meterBackground"));
        drawArc(r, c, 15.0f, start, start + sweep * v, 2.6f, token("primary"));
        Depth::object(r, {c.x - 9.0f, c.y - 9.0f, 18.0f, 18.0f}, 9.0f, knobDragParam() == static_cast<int>(id));
        const float a = start + sweep * v;
        r.drawLine({c.x + std::cos(a) * 3.0f, c.y + std::sin(a) * 3.0f}, {c.x + std::cos(a) * 8.0f, c.y + std::sin(a) * 8.0f}, 1.6f,
                   token("textPrimary"));
        r.drawText(kEverythingNames[i], {cell.x + 44.0f, cell.y + 12.0f}, 10.0f, token("textMuted"));
        if (!velocity && std::abs(sound[id] - arrived[id]) > 1.0e-9) {
            r.fillCircle({cell.x + 44.0f + r.measureText(kEverythingNames[i], 10.0f).width + 6.0f, cell.y + 18.0f}, 2.5f, token("secondary"));
        }
        r.drawText(m_instance ? m_instance->getParameterDisplay(id) : std::string(), {cell.x + 44.0f, cell.y + 28.0f}, 11.0f,
                   token("textPrimary"));
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Pointer
// ---------------------------------------------------------------------------------------------------------------

bool RumblePluginEditor::handlePress(const NUIPoint& p) {
    if (m_keep.contains(p)) {
        keep();
        return true;
    }
    if (m_lastKept && !m_editorSaid.empty() && m_undo.contains(p)) {
        undoKeep();
        return true;
    }
    if (m_everythingToggle.contains(p)) {
        m_showEverything = !m_showEverything;
        setDirty(true);
        return true;
    }
    moved();
    for (size_t i = 0; i < m_keptRows.size() && i < m_kept.size(); ++i) {
        if (m_keptRows[i].contains(p)) {
            startFromKept(i);
            return true;
        }
    }
    for (size_t i = 0; i < m_shelfRows.size(); ++i) {
        if (m_shelfRows[i].contains(p)) {
            startFrom(i);
            return true;
        }
    }
    for (size_t k = 0; k < m_trailPillCount; ++k) {
        if (m_trailPills[k].contains(p)) {
            m_session->goToStep(m_trailSteps[k]);
            soundChanged();
            return true;
        }
    }
    for (size_t i = 0; i < m_cards.size(); ++i) {
        if (m_cards[i].contains(p)) {
            m_session->select(static_cast<int>(i) - 1);
            soundChanged();
            return true;
        }
    }
    if (m_more.contains(p)) {
        m_outlines->forgetPending();
        m_session->more();
        soundChanged();
        return true;
    }
    for (size_t i = 0; i < m_distance.size(); ++i) {
        if (m_distance[i].contains(p)) {
            m_session->setDistance(static_cast<Variation::Distance>(i));
            setDirty(true);
            return true;
        }
    }
    for (size_t i = 0; i < m_holds.size(); ++i) {
        if (m_holds[i].contains(p)) {
            m_session->toggleHold(static_cast<Variation::Quality>(i));
            setDirty(true);
            return true;
        }
    }
    if (m_back.contains(p)) {
        m_session->back();
        soundChanged();
        return true;
    }
    if (!m_showEverything && m_picture.contains(p)) {
        const RumbleSound& sound = m_session->current();
        const double length = RumbleVoice::holdSeconds(sound[RumbleSound::Hold]) + RumbleVoice::tailSeconds(sound[RumbleSound::Tail]);
        const NUIPoint sq{pictureX(length), m_picture.y + kPicTop + (m_picture.height - kPicTop - kPicBottom) * 0.5f};
        const NUIPoint dot{m_picture.x + kPicX, pictureDropY(RumbleVoice::dropSemitones(sound[RumbleSound::Punch]))};
        if (near(p, sq, 10.0f)) m_dragging = Handle::Length;
        else if (near(p, dot, 10.0f)) m_dragging = Handle::Drop;
        setDirty(true);
        return true;
    }
    return false;
}

void RumblePluginEditor::dragHandle(const NUIPoint& p) {
    const RumbleSound& sound = m_session->current();
    if (m_dragging == Handle::Length) {
        const float x = (p.x - m_picture.x - kPicX) / (m_picture.width - kPicX * 2.0f);
        const double seconds = RumbleOutline::secondsAt(x);
        const double tail = std::max(0.26, seconds - RumbleVoice::holdSeconds(sound[RumbleSound::Hold]));
        m_session->shape(RumbleSound::Tail, std::log(tail / RumbleVoice::tailSeconds(0.0)) / std::log(RumbleVoice::tailSeconds(1.0) / RumbleVoice::tailSeconds(0.0)));
    } else if (m_dragging == Handle::Drop) {
        const float bottom = m_picture.bottom() - kPicBottom - 4.0f;
        const float span = m_picture.height - kPicTop - kPicBottom - 8.0f;
        m_session->shape(RumbleSound::Punch, (bottom - p.y) / span);
    }
    moved();
    soundChanged();
}

bool RumblePluginEditor::onKeyEvent(const NUIKeyEvent& event) {
    if (!isVisible()) return false;
    if (routeKeyToAccessory(event)) return true; // the preset bar's browser or Save form takes typing first
    if (!event.pressed) return AestraPanelWindow::onKeyEvent(event);
    followInstrument(); // a key must not move from a sound the track no longer has
    const bool moves = event.keyCode == NUIKeyCode::Left || event.keyCode == NUIKeyCode::Right ||
                       event.keyCode == NUIKeyCode::Down || event.keyCode == NUIKeyCode::Up;
    // A drag (picture handle or knob) owns the picked sound until it ends: moving away mid-drag would hand the
    // rest of the drag to another sound.
    if (moves && (m_dragging != Handle::None || knobDragParam() >= 0)) return true;
    switch (event.keyCode) {
    case NUIKeyCode::Left:
        m_session->moveSelection(-1);
        break;
    case NUIKeyCode::Right:
        m_session->moveSelection(1);
        break;
    case NUIKeyCode::Down:
        m_outlines->forgetPending();
        m_session->more();
        break;
    case NUIKeyCode::Up:
        m_session->back();
        break;
    case NUIKeyCode::Enter:
        keep();
        return true;
    default:
        return AestraPanelWindow::onKeyEvent(event);
    }
    moved();
    soundChanged();
    return true;
}

bool RumblePluginEditor::onMouseEvent(const NUIMouseEvent& event) {
    if (!isVisible()) return false;
    if (AestraPanelWindow::onMouseEvent(event)) return true;

    followInstrument();

    if (m_dragging != Handle::None) {
        // A release that never arrives (focus loss) leaves no button held: stop the drag.
        if (event.released || (!event.pressed && !NUIComponent::isPointerButtonHeld())) {
            m_dragging = Handle::None;
            setDirty(true);
        } else {
            dragHandle(event.position);
        }
        return true;
    }

    const bool contains = getBounds().contains(event.position);
    if (!contains && knobDragParam() < 0) return false;
    if (contains && event.pressed) setFocused(true); // so the arrow keys come here (R8)

    // Double-click on a control puts its setting back to how the sound arrived.
    if (event.pressed && event.button == NUIMouseButton::Left) {
        for (size_t i = 0; i < kControlCount + (m_showEverything ? m_everything.size() : 0); ++i) {
            const bool everything = i >= kControlCount;
            const NUIRect& rect = everything ? m_everything[i - kControlCount] : m_controls[i];
            const uint32_t id = everything ? kEverythingIds[i - kControlCount] : kControlIds[i];
            if (!rect.contains(event.position) || id >= RumbleSound::Count) continue;
            const long long now = nowMs();
            const bool twice = event.doubleClick || (now - m_lastClickMs < 400 && near(event.position, m_lastClickPos, 5.0f));
            m_lastClickMs = twice ? 0 : now;
            m_lastClickPos = event.position;
            if (twice) {
                m_session->putBack(id);
                moved();
                soundChanged();
                return true;
            }
        }
    }

    std::array<KnobTarget, kControlCount + 10> knobs{};
    size_t knobCount = 0;
    for (size_t i = 0; i < kControlCount; ++i) {
        // Centred on the drawn knob (16 px down, kKnobSize across), so the cursor comes back to the knob.
        const NUIRect& col = m_controls[i];
        knobs[knobCount++] = {{col.x, col.y, col.width, 2.0f * (16.0f + kKnobSize * 0.5f)}, kControlIds[i]};
    }
    if (m_showEverything) {
        for (size_t i = 0; i < m_everything.size(); ++i) knobs[knobCount++] = {m_everything[i], kEverythingIds[i]};
    }
    if (handleKnobDrag(event, knobs.data(), knobCount, kKnobRange,
                       [this](uint32_t id) {
                           if (id >= RumbleSound::Count) return m_instance ? m_instance->getParameter(id) : 0.0f;
                           return static_cast<float>(m_session->current()[id]);
                       },
                       [this](uint32_t id, float v) {
                           if (id >= RumbleSound::Count) { // Velocity: how the sound is played, not what it is
                               if (m_instance) m_instance->setParameter(id, v);
                               setDirty(true);
                               return;
                           }
                           m_session->shape(id, v);
                           moved(); // the kept sound is not this one any more
                           soundChanged();
                       })) {
        return true;
    }

    if (event.pressed && event.button == NUIMouseButton::Left && handlePress(event.position)) return true;

    if (!event.pressed && !event.released && !event.cursorCaptured) {
        m_hover = event.position;
        setDirty(true);
    }
    return contains;
}

} // namespace AestraUI
