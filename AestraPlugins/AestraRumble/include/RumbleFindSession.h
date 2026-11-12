// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Where the producer is in finding a sound (SPEC-025, slice R5): the trail of steps, each step's six, which
// sound is picked, how far the next six stray, which qualities are held, and what the producer has shaped.
//
// No drawing and no audio here, so every move is tested headless. The editor draws this and hands
// current() to the instrument after every move. Not for the audio thread: a round renders nothing, but it
// allocates.
#pragma once

#include "RumbleVariation.h"
#include "RumbleVoice.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Plugins {

class RumbleFindSession {
public:
    static constexpr int kParent = -1; // the sound the six were made from
    static constexpr size_t kSix = RumbleVariation::kRoundSize;

    struct Step {
        RumbleSound parent{};
        std::string parentName;
        std::array<RumbleSound, kSix> six{};
        std::array<RumbleSound, kSix> arrived{}; // the six as they were made, for "put it back"
        RumbleSound parentArrived{};
        std::array<bool, kSix> shaped{};
        bool parentShaped = false;
        int selected = 0;
    };

    // A session starts from a sound (a starting sound or a kept one) with its six already made.
    RumbleFindSession(const RumbleSound& start, std::string startName, uint64_t seed = 1);

    size_t stepCount() const { return m_steps.size(); }
    size_t currentStep() const { return m_at; }
    const Step& step(size_t index) const { return m_steps[index]; }
    int selected() const { return m_steps[m_at].selected; }
    // The sound the instrument plays now: the picked one of the six, or the parent.
    const RumbleSound& current() const;
    std::string nameOf(int index) const; // in the current step; kParent for the parent
    std::string currentName() const { return nameOf(selected()); }
    bool shaped(int index) const;
    // How many sounds have been made in this session (the trail's count).
    size_t soundsMade() const { return m_steps.size() * kSix; }

    // Moves. Each one leaves a sentence in said().
    void select(int index);          // kParent or 0..5
    void moveSelection(int delta);   // arrow keys: wraps through the parent
    void more();                     // six more like current(); later steps are dropped
    bool back();                     // a step up; the same six as before
    bool goToStep(size_t index);     // a pill on the trail

    void setDistance(RumbleVariation::Distance distance);
    RumbleVariation::Distance distance() const { return m_distance; }
    void toggleHold(RumbleVariation::Quality quality);
    bool held(RumbleVariation::Quality quality) const;

    // Shaping: the producer changed one of the current sound's settings. Carried into the next six.
    void shape(uint32_t settingId, double value);
    // Double-click on a control: the setting goes back to how the sound arrived.
    void putBack(uint32_t settingId);

    const std::string& said() const { return m_said; }

private:
    RumbleSound& currentMutable();
    const RumbleSound& arrivedOf(int index) const;
    void makeStep(const RumbleSound& parent, std::string parentName);
    uint64_t seedFor(size_t stepIndex) const;
    void refreshShaped(int index);

    std::vector<Step> m_steps;
    size_t m_at = 0;
    uint64_t m_seed;
    RumbleVariation::Distance m_distance = RumbleVariation::Distance::Further;
    uint8_t m_held = 0;
    std::string m_said;
};

} // namespace Plugins
} // namespace Aestra
