// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Commands/EditPatternNotesCommand.h"
#include "Models/TrackManager.h"
#include "NUIRenderer.h"
#include "NUIThemeSystem.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace Aestra {

/**
 * @brief Step lanes under an open Arsenal row (V81-AR1, FD-27 point 4): the values behind the pads, drawn as
 * bars you drag, lined up with the pads above.
 *
 * Lanes are fitted to the unit (FD-27 "type-fitted lanes"): every unit gets Velocity; a pitched sampler also
 * gets Pitch (semitones from the root) and Length (the gate), because those are the fields its playback
 * reads. The value rules and the geometry are pure; one drag is one undo step.
 */
class ArsenalStepLanes {
public:
    enum class Lane { Velocity, Pitch, Length, Chance, Ratchet };

    static constexpr float kLaneH = 30.0f;
    static constexpr float kLaneGap = 4.0f;
    static constexpr float kPad = 8.0f;
    static constexpr float kStepBeats = 0.25f;
    static constexpr int kPitchRange = 12; ///< semitones either side of the root
    static constexpr float kGateMin = 0.1f, kGateMax = 2.0f;
    static constexpr float kVelMin = 0.05f;

    static constexpr int kRatchetMax = 4;

    static std::vector<Lane> lanesFor(Audio::UnitType type) {
        if (type == Audio::UnitType::Audio) return {};
        if (type == Audio::UnitType::PitchedSampler)
            return {Lane::Velocity, Lane::Pitch, Lane::Length, Lane::Chance, Lane::Ratchet};
        return {Lane::Velocity, Lane::Chance, Lane::Ratchet};
    }
    static const char* label(Lane l) {
        switch (l) {
        case Lane::Velocity: return "VEL";
        case Lane::Pitch: return "PITCH";
        case Lane::Length: return "LENGTH";
        case Lane::Chance: return "CHANCE";
        case Lane::Ratchet: return "RATCHET";
        }
        return "";
    }
    static float heightFor(size_t laneCount) {
        return laneCount == 0 ? 0.0f : kPad * 2.0f + static_cast<float>(laneCount) * kLaneH + static_cast<float>(laneCount - 1) * kLaneGap;
    }

    /// 0 (bottom) .. 1 (top) position of a value within its lane.
    static float toFraction(Lane l, float v) {
        switch (l) {
        case Lane::Velocity: return std::clamp(v, 0.0f, 1.0f);
        case Lane::Pitch: return (std::clamp(v, -static_cast<float>(kPitchRange), static_cast<float>(kPitchRange)) + kPitchRange) / (2.0f * kPitchRange);
        case Lane::Length: return (std::clamp(v, kGateMin, kGateMax) - kGateMin) / (kGateMax - kGateMin);
        case Lane::Chance: return std::clamp(v, 0.0f, 1.0f);
        case Lane::Ratchet: return (std::clamp(v, 1.0f, static_cast<float>(kRatchetMax)) - 1.0f) / (kRatchetMax - 1);
        }
        return 0.0f;
    }
    /// The value a pointer at fraction f (0 bottom .. 1 top) means; pitch snaps to whole semitones.
    static float fromFraction(Lane l, float f) {
        f = std::clamp(f, 0.0f, 1.0f);
        switch (l) {
        case Lane::Velocity: return std::max(kVelMin, f);
        case Lane::Pitch: return std::round(f * 2.0f * kPitchRange - kPitchRange);
        case Lane::Length: return kGateMin + f * (kGateMax - kGateMin);
        case Lane::Chance: return f;
        case Lane::Ratchet: return std::round(1.0f + f * (kRatchetMax - 1));
        }
        return 0.0f;
    }

    /// What the playback engine will use for this note (a pitched sampler resolves pitch from pitchOffset, or from
    /// `pitch` when no offset is set), so the lane shows what you hear.
    static float readValue(const Audio::MidiNote& n, Lane l, int rootMidi) {
        switch (l) {
        case Lane::Velocity: return n.velocity;
        case Lane::Pitch: return static_cast<float>(n.pitchOffset == 0 && n.pitch > 0 && n.pitch != rootMidi ? n.pitch - rootMidi : n.pitchOffset);
        case Lane::Length: return n.gate;
        case Lane::Chance: return n.chance;
        case Lane::Ratchet: return static_cast<float>(n.ratchet);
        }
        return 0.0f;
    }
    static void writeValue(Audio::MidiNote& n, Lane l, float v, int rootMidi) {
        switch (l) {
        case Lane::Velocity: n.velocity = std::clamp(v, kVelMin, 1.0f); break;
        case Lane::Pitch: {
            const int semis = std::clamp(static_cast<int>(std::lround(v)), -kPitchRange, kPitchRange);
            n.pitchOffset = static_cast<int8_t>(semis);
            n.pitch = std::clamp(rootMidi + semis, 0, 127);
            break;
        }
        case Lane::Length: n.gate = std::clamp(v, kGateMin, kGateMax); break;
        case Lane::Chance: n.chance = std::clamp(v, 0.0f, 1.0f); break;
        case Lane::Ratchet: n.ratchet = static_cast<uint8_t>(std::clamp(static_cast<int>(std::lround(v)), 1, kRatchetMax)); break;
        }
    }

    /// The step column under x, using the pads' own geometry (4-step groups carry a gap); -1 if none.
    static int stepAtX(float x, float stripX, float stripW, float stepW, float groupGap, float scrollX, int steps) {
        const float rel = x - stripX + scrollX;
        if (stepW <= 0.0f || x < stripX || x >= stripX + stripW || rel < 0.0f) return -1;
        for (int s = 0; s < steps; ++s) {
            const float left = static_cast<float>(s) * stepW + static_cast<float>(s / 4) * groupGap;
            if (rel >= left && rel < left + stepW) return s;
        }
        return -1;
    }
    static float stepCentreX(int step, float stripX, float stepW, float groupGap, float scrollX) {
        return stripX + static_cast<float>(step) * stepW + static_cast<float>(step / 4) * groupGap - scrollX + stepW * 0.5f;
    }

    /// What one row's lanes need to know; rebuilt by the panel for each draw and pointer event.
    struct Context {
        Audio::TrackManager* tm{nullptr};
        Audio::PatternID pattern{};
        Audio::UnitID unit{0};
        Audio::UnitType type{Audio::UnitType::Sampler};
        int root{60};
        int steps{16};
        AestraUI::NUIRect area{};   ///< the lanes' rectangle under the row
        AestraUI::NUIRect strip{};  ///< the row's step strip (x and width are what lines the columns up)
        float stepW{0.0f}, groupGap{2.0f}, scrollX{0.0f};
        AestraUI::NUIColor accent{};
    };

    void draw(AestraUI::NUIRenderer& renderer, const Context& c) const {
        const auto lanes = lanesFor(c.type);
        if (lanes.empty() || !c.tm) return;
        auto& theme = AestraUI::NUIThemeManager::getInstance();
        const auto& tp = theme.getCurrentTheme();
        const auto notes = notesOf(c);
        renderer.fillRoundedRect(c.area, tp.radiusS, theme.getColor("backgroundPrimary").withAlpha(0.9f));
        for (size_t i = 0; i < lanes.size(); ++i) {
            const auto r = laneRect(c, i);
            renderer.drawText(label(lanes[i]), {c.area.x + 12.0f, r.y + r.height * 0.5f - 5.0f}, tp.fontSizeMicro, theme.getColor("textSecondary").withAlpha(0.8f));
            renderer.setClipRect({c.strip.x, r.y, c.strip.width, r.height});
            renderer.fillRect({c.strip.x, r.y + r.height * 0.5f, c.strip.width, 1.0f}, theme.getColor("border").withAlpha(lanes[i] == Lane::Pitch ? 0.5f : 0.0f));
            for (int s = 0; s < c.steps; ++s) {
                const Audio::MidiNote* n = noteAt(notes, c.unit, s);
                const float cx = stepCentreX(s, c.strip.x, c.stepW, c.groupGap, c.scrollX);
                const float bw = std::max(2.0f, c.stepW - 3.0f);
                const AestraUI::NUIRect cell{cx - bw * 0.5f, r.y, bw, r.height};
                renderer.fillRect(cell, theme.getColor("surfaceTertiary").withAlpha(0.35f));
                if (!n) continue;
                const float f = toFraction(lanes[i], readValue(*n, lanes[i], c.root));
                const bool centred = lanes[i] == Lane::Pitch;
                const float top = r.y + r.height * (1.0f - f);
                const float base = centred ? r.y + r.height * 0.5f : r.bottom();
                const AestraUI::NUIRect bar{cell.x, std::min(top, base), bw, std::max(2.0f, std::abs(base - top))};
                renderer.fillRect(bar, c.accent);
                // The value, when it is not the default and the column is wide enough to read it (the mock's +3, 50%, x3).
                const float v = readValue(*n, lanes[i], c.root);
                const bool shown = lanes[i] == Lane::Pitch ? v != 0.0f : lanes[i] == Lane::Length ? std::abs(v - 1.0f) > 0.01f : lanes[i] == Lane::Chance ? v < 0.995f : v > 1.0f;
                if (shown && bw >= 18.0f) {
                    const std::string txt = lanes[i] == Lane::Pitch ? (v > 0 ? "+" : "") + std::to_string(static_cast<int>(v)) : lanes[i] == Lane::Ratchet ? "x" + std::to_string(static_cast<int>(v)) : std::to_string(static_cast<int>(std::lround(v * 100.0f))) + "%";
                    renderer.drawTextCentered(txt, {cell.x, std::max(r.y, bar.y - 12.0f), bw, 11.0f}, tp.fontSizeMicro, theme.getColor("textSecondary"));
                }
            }
            renderer.clearClipRect();
        }
    }

    /// Start (or continue) painting a lane value; returns true if the pointer is in a lane.
    bool press(const Context& c, const AestraUI::NUIPoint& p) {
        const auto lanes = lanesFor(c.type);
        for (size_t i = 0; i < lanes.size(); ++i) {
            if (!laneRect(c, i).contains(p) || p.x < c.strip.x || p.x >= c.strip.x + c.strip.width) continue;
            m_lane = static_cast<int>(i);
            m_before = notesOf(c);
            m_changed = false;
            paint(c, p);
            return true;
        }
        return false;
    }
    bool dragging() const { return m_lane >= 0; }
    /// True once if a value was written since the last call, so the panel can tell the engine the pattern changed.
    bool takeEdited() { const bool e = m_edited; m_edited = false; return e; }
    void drag(const Context& c, const AestraUI::NUIPoint& p) { if (m_lane >= 0) paint(c, p); }
    /// Finish the gesture: the whole drag becomes one undo step.
    void release(const Context& c) {
        if (m_lane < 0) return;
        if (m_changed && c.tm && c.pattern.isValid()) {
            auto after = notesOf(c);
            c.tm->getCommandHistory().pushExecuted(std::make_shared<Audio::EditPatternNotesCommand>(
                c.tm->getPatternManager(), c.pattern, std::move(m_before), std::move(after), "Edit Step Lane"));
        }
        m_lane = -1;
        m_before.clear();
    }

    static AestraUI::NUIRect laneRect(const Context& c, size_t index) {
        return {c.area.x, c.area.y + kPad + static_cast<float>(index) * (kLaneH + kLaneGap), c.area.width, kLaneH};
    }
    static std::vector<Audio::MidiNote> notesOf(const Context& c) {
        if (!c.tm || !c.pattern.isValid()) return {};
        const auto* p = c.tm->getPatternManager().getPattern(c.pattern);
        if (!p || !p->isMidi()) return {};
        return std::get<Audio::MidiPayload>(p->payload).notes;
    }
    static const Audio::MidiNote* noteAt(const std::vector<Audio::MidiNote>& notes, Audio::UnitID unit, int step) {
        for (const auto& n : notes) {
            if (n.unitId == unit && static_cast<int>(std::floor(n.startBeat / kStepBeats + 0.0001)) == step) return &n;
        }
        return nullptr;
    }

private:
    void paint(const Context& c, const AestraUI::NUIPoint& p) {
        const auto lanes = lanesFor(c.type);
        if (m_lane < 0 || static_cast<size_t>(m_lane) >= lanes.size() || !c.tm || !c.pattern.isValid()) return;
        const int step = stepAtX(p.x, c.strip.x, c.strip.width, c.stepW, c.groupGap, c.scrollX, c.steps);
        if (step < 0) return;
        const auto r = laneRect(c, static_cast<size_t>(m_lane));
        const Lane lane = lanes[static_cast<size_t>(m_lane)];
        const float v = fromFraction(lane, 1.0f - (p.y - r.y) / r.height);
        c.tm->getPatternManager().applyPatch(c.pattern, [&](Audio::PatternSource& ps) {
            if (!ps.isMidi()) return;
            for (auto& n : std::get<Audio::MidiPayload>(ps.payload).notes) {
                if (n.unitId == c.unit && static_cast<int>(std::floor(n.startBeat / kStepBeats + 0.0001)) == step) {
                    writeValue(n, lane, v, c.root);
                    m_changed = true;
                    m_edited = true;
                    return;
                }
            }
        });
    }

    int m_lane{-1};
    bool m_changed{false};
    bool m_edited{false};
    std::vector<Audio::MidiNote> m_before;
};

} // namespace Aestra
