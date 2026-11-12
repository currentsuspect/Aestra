// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumbleFindSession.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace Aestra {
namespace Plugins {

namespace {
using RumbleVariation::Distance;
using RumbleVariation::Quality;

// Plain words for the settings, as the six controls and the Everything page name them.
const char* const kSettingNames[RumbleSound::Count] = {
    "Punch", "Tail", "Weight", "Tone", "Dirt", "Slide", "Drop time", "Hold",
    "Curve", "2nd harmonic", "3rd harmonic", "Clip type", "Click", "Click length", "Click tone"};

// How a quality reads when a sound has more of it, then less.
const char* const kMore[RumbleVariation::kQualityCount] = {"harder at the front", "longer", "deeper", "brighter", "dirtier"};
const char* const kLess[RumbleVariation::kQualityCount] = {"softer at the front", "shorter", "lighter", "darker", "cleaner"};
const char* const kHoldWord[RumbleVariation::kQualityCount] = {"punch", "tail", "weight", "tone", "dirt"};

const char* distanceWords(Distance distance) {
    switch (distance) {
    case Distance::Close:
        return "close to it";
    case Distance::Further:
        return "a little further";
    case Distance::Far:
        return "far from it";
    }
    return "a little further";
}

std::string lowerFirst(std::string s) {
    if (!s.empty() && s[0] >= 'A' && s[0] <= 'Z') s[0] = static_cast<char>(s[0] - 'A' + 'a');
    return s;
}

std::string valueWords(uint32_t id, double value) {
    char buf[48];
    if (id == RumbleSound::Tail) {
        std::snprintf(buf, sizeof(buf), "rings %.2f s", RumbleVoice::tailSeconds(value));
    } else if (id == RumbleSound::Punch) {
        std::snprintf(buf, sizeof(buf), "the pitch falls %.1f semitones", RumbleVoice::dropSemitones(value));
    } else {
        std::snprintf(buf, sizeof(buf), "%d %%", static_cast<int>(std::lround(value * 100.0)));
    }
    return buf;
}

bool sameSound(const RumbleSound& a, const RumbleSound& b) {
    return a.v == b.v && a.seed == b.seed;
}
} // namespace

RumbleFindSession::RumbleFindSession(const RumbleSound& start, std::string startName, uint64_t seed) : m_seed(seed) {
    makeStep(start, std::move(startName));
    m_said = "Pick a sound, then ask for six more like it.";
}

uint64_t RumbleFindSession::seedFor(size_t stepIndex) const {
    // SplitMix64 of the session seed and the step: the same session makes the same rounds.
    uint64_t z = m_seed + 0x9E3779B97F4A7C15ull * (stepIndex + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

void RumbleFindSession::makeStep(const RumbleSound& parent, std::string parentName) {
    Step step;
    step.parent = parent;
    step.parentArrived = parent;
    step.parentName = std::move(parentName);
    RumbleVariation::Request request;
    request.parent = parent;
    request.distance = m_distance;
    request.held = m_held;
    request.seed = seedFor(m_steps.size());
    step.six = RumbleVariation::makeRound(request);
    step.arrived = step.six;
    m_steps.push_back(std::move(step));
    m_at = m_steps.size() - 1;
}

const RumbleSound& RumbleFindSession::current() const {
    const Step& step = m_steps[m_at];
    return step.selected == kParent ? step.parent : step.six[static_cast<size_t>(step.selected)];
}

RumbleSound& RumbleFindSession::currentMutable() {
    Step& step = m_steps[m_at];
    return step.selected == kParent ? step.parent : step.six[static_cast<size_t>(step.selected)];
}

const RumbleSound& RumbleFindSession::arrivedOf(int index) const {
    const Step& step = m_steps[m_at];
    return index == kParent ? step.parentArrived : step.arrived[static_cast<size_t>(index)];
}

std::string RumbleFindSession::nameOf(int index) const {
    const Step& step = m_steps[m_at];
    if (index == kParent) return step.parentName;
    if (index < 0 || index >= static_cast<int>(kSix)) return {};
    return RumbleVariation::nameFor(step.six[static_cast<size_t>(index)]);
}

bool RumbleFindSession::shaped(int index) const {
    const Step& step = m_steps[m_at];
    if (index == kParent) return step.parentShaped;
    if (index < 0 || index >= static_cast<int>(kSix)) return false;
    return step.shaped[static_cast<size_t>(index)];
}

void RumbleFindSession::refreshShaped(int index) {
    Step& step = m_steps[m_at];
    const bool changed = !sameSound(index == kParent ? step.parent : step.six[static_cast<size_t>(index)], arrivedOf(index));
    if (index == kParent) {
        step.parentShaped = changed;
    } else {
        step.shaped[static_cast<size_t>(index)] = changed;
    }
}

void RumbleFindSession::select(int index) {
    index = std::clamp(index, kParent, static_cast<int>(kSix) - 1);
    m_steps[m_at].selected = index;
    const std::string name = nameOf(index);
    if (index == kParent) {
        m_said = name + ": the sound these six came from.";
        return;
    }
    // Say how the picked sound differs from its parent: its two biggest moves.
    const RumbleSound& parent = m_steps[m_at].parent;
    const RumbleSound& sound = current();
    std::array<std::pair<double, size_t>, RumbleVariation::kQualityCount> moves{};
    for (size_t q = 0; q < RumbleVariation::kQualityCount; ++q) {
        const uint32_t id = RumbleVariation::settingOf(static_cast<Quality>(q));
        moves[q] = {sound[id] - parent[id], q};
    }
    std::sort(moves.begin(), moves.end(), [](const auto& a, const auto& b) { return std::fabs(a.first) > std::fabs(b.first); });
    std::string how;
    for (size_t k = 0; k < 2; ++k) {
        if (std::fabs(moves[k].first) < 0.03) break;
        if (!how.empty()) how += " and ";
        how += moves[k].first > 0.0 ? kMore[moves[k].second] : kLess[moves[k].second];
    }
    m_said = how.empty() ? name + ": close to " + m_steps[m_at].parentName + "."
                         : name + ": " + how + " than " + m_steps[m_at].parentName + ".";
}

void RumbleFindSession::moveSelection(int delta) {
    // The parent sits before the first of the six; arrows wrap through it.
    const int slots = static_cast<int>(kSix) + 1;
    int at = m_steps[m_at].selected + 1; // 0 = parent
    at = ((at + delta) % slots + slots) % slots;
    select(at - 1);
}

void RumbleFindSession::more() {
    const RumbleSound parent = current();
    const std::string parentName = currentName();
    m_steps.resize(m_at + 1); // a new branch replaces whatever came after this step
    makeStep(parent, parentName);
    std::string held;
    for (size_t q = 0; q < RumbleVariation::kQualityCount; ++q) {
        if ((m_held & (1u << q)) == 0) continue;
        if (!held.empty()) held += " and ";
        held += kHoldWord[q];
    }
    m_said = "Six more like " + parentName + ", " + distanceWords(m_distance) +
             (held.empty() ? std::string(".") : ", keeping its " + held + " exactly.");
}

bool RumbleFindSession::back() {
    if (m_at == 0) {
        m_said = "This is where you started.";
        return false;
    }
    --m_at;
    m_said = "Back a step: the same six as before.";
    return true;
}

bool RumbleFindSession::goToStep(size_t index) {
    if (index >= m_steps.size()) return false;
    m_at = index;
    m_said = "Step " + std::to_string(index + 1) + ", as you left it.";
    return true;
}

void RumbleFindSession::setDistance(Distance distance) {
    m_distance = distance;
    m_said = std::string("The next six will be ") + distanceWords(distance) + ".";
}

void RumbleFindSession::toggleHold(Quality quality) {
    const uint8_t bit = RumbleVariation::holdBit(quality);
    m_held = static_cast<uint8_t>(m_held ^ bit);
    const char* word = kHoldWord[static_cast<size_t>(quality)];
    if ((m_held & bit) != 0) {
        m_said = std::string("The next six keep its ") + word + " exactly.";
    } else {
        std::string w = word;
        w[0] = static_cast<char>(w[0] - 'a' + 'A');
        m_said = w + " can move again.";
    }
}

bool RumbleFindSession::held(Quality quality) const {
    return (m_held & RumbleVariation::holdBit(quality)) != 0;
}

void RumbleFindSession::shape(uint32_t settingId, double value) {
    if (settingId >= RumbleSound::Count) return;
    RumbleSound& sound = currentMutable();
    const double before = sound[settingId];
    sound[settingId] = std::clamp(value, 0.0, 1.0);
    refreshShaped(selected());
    const char* direction = sound[settingId] > before ? " up: " : sound[settingId] < before ? " down: " : ": ";
    m_said = std::string(kSettingNames[settingId]) + direction + valueWords(settingId, sound[settingId]) + ".";
}

void RumbleFindSession::putBack(uint32_t settingId) {
    if (settingId >= RumbleSound::Count) return;
    currentMutable()[settingId] = arrivedOf(selected())[settingId];
    refreshShaped(selected());
    m_said = std::string(kSettingNames[settingId]) + " back to how " + lowerFirst(currentName()) + " arrived.";
}

} // namespace Plugins
} // namespace Aestra
