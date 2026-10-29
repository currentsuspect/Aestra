// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// What happens at the two edges of playback, through the real engine with an Arsenal
// instrument unit (a plugin that records the MIDI it receives):
//   A. Play start is sample-accurate. Notes are queued by the main loop; with the live
//      start hold the first block waits until they are, so a note ON the play position
//      sounds at that exact frame even when the main loop runs only every few blocks.
//   B. Stop releases held notes in plugin instruments (sustain up + all notes off). The
//      stop drops the queued note-offs, and plugins used to get nothing: a held note
//      kept sounding until the next play.
//   C. A new play starts clean: leftovers get All Sound Off before the first note.
//   D. "Stop cuts sound" makes a stop cut (All Sound Off) instead of releasing.

#include "Core/AudioEngine.h"
#include "Core/AudioGraphBuilder.h"
#include "Headless/PDCTestHelpers.h"
#include "Models/TrackManager.h"

#include <cstdio>
#include <memory>
#include <vector>

using namespace Aestra;
using namespace Aestra::Audio;

namespace {

constexpr uint32_t kRate = 48000;
constexpr uint32_t kBlock = 256;
constexpr uint64_t kBeat = 24000; // 120 BPM

int g_failures = 0;
void check(bool cond, const char* what) {
    std::printf("  %s %s\n", cond ? "PASS" : "FAIL", what);
    g_failures += cond ? 0 : 1;
}

struct Received {
    uint64_t frame;
    uint8_t status, data1, data2;
};

// A pass-through "instrument" that logs every MIDI event with its absolute engine frame.
class RecordingInstrument : public PDCTest::MockLatencyPlugin {
public:
    RecordingInstrument() : MockLatencyPlugin(0, "RecordingInstrument") {}
    void process(const float* const* inputs, float** outputs, uint32_t numInputChannels, uint32_t numOutputChannels,
                 uint32_t numFrames, const MidiBuffer* midiInput, MidiBuffer* midiOutput) override {
        MockLatencyPlugin::process(inputs, outputs, numInputChannels, numOutputChannels, numFrames, midiInput,
                                   midiOutput);
        if (midiInput) {
            for (size_t i = 0; i < midiInput->getEventCount(); ++i) {
                const auto& e = midiInput->getEvent(i);
                log.push_back({blockStart + e.sampleOffset, e.data[0], e.data[1], e.data[2]});
            }
        }
    }
    uint64_t blockStart = 0;
    std::vector<Received> log;

    bool sawNoteOn(uint64_t frame) const {
        for (const auto& r : log)
            if ((r.status & 0xF0) == 0x90 && r.data2 > 0 && r.frame == frame) return true;
        return false;
    }
    bool sawController(uint8_t cc, size_t from = 0) const {
        for (size_t i = from; i < log.size(); ++i)
            if ((log[i].status & 0xF0) == 0xB0 && log[i].data1 == cc) return true;
        return false;
    }
    uint64_t firstNoteOn() const {
        for (const auto& r : log)
            if ((r.status & 0xF0) == 0x90 && r.data2 > 0) return r.frame;
        return UINT64_MAX;
    }
};

struct Rig {
    std::shared_ptr<TrackManager> tm = std::make_shared<TrackManager>();
    AudioEngine engine;
    std::shared_ptr<RecordingInstrument> synth = std::make_shared<RecordingInstrument>();
    std::vector<float> out = std::vector<float>(kBlock * 2);
    int blocks = 0;

    explicit Rig(bool holdStart) {
        tm->setOutputSampleRate(kRate);
        engine.setSampleRate(kRate);
        engine.setBufferConfig(kBlock, 2);
        engine.setBPM(120.0f);
        engine.setGraph(AudioGraphBuilder::buildFromTrackManager(*tm));
        if (auto slotMap = tm->getChannelSlotMapShared()) engine.setChannelSlotMap(slotMap);
        engine.setUnitManager(&tm->getUnitManager());
        engine.setPatternPlaybackEngine(&tm->getPatternPlaybackEngine());
        engine.setMetronomeEnabled(false);
        engine.setHoldStartForSchedule(holdStart);

        auto& units = tm->getUnitManager();
        const UnitID unit = units.createUnit("synth", UnitType::Instrument);
        units.attachPlugin(unit, "aestra.test.recording", synth);
        MidiPayload payload;
        MidiNote note; // a long note exactly on beat 1: the play position below
        note.pitch = 60;
        note.startBeat = 1.0;
        note.durationBeats = 4.0;
        note.velocity = 0.8f;
        note.unitId = unit;
        payload.notes.push_back(note);
        const PatternID pid = tm->getPatternManager().createMidiPattern("p", 8.0, payload);
        tm->getPatternPlaybackEngine().schedulePatternInstance(pid, 0.0, 1);
    }

    // One callback; the main loop (refill) runs only every `uiEvery` blocks, after the block.
    void block(int uiEvery = 4) {
        synth->blockStart = engine.getGlobalSamplePos();
        engine.processBlock(out.data(), nullptr, kBlock, 0.0);
        if (++blocks % uiEvery == 0) engine.performNonRealtimeMaintenance();
    }
};

void testPlayStartIsSampleAccurate() {
    {
        Rig rig(true);
        rig.engine.setGlobalSamplePos(kBeat); // cue exactly on the note
        rig.engine.setTransportPlaying(true);
        for (int i = 0; i < 40; ++i) rig.block();
        check(rig.synth->sawNoteOn(kBeat), "A: with the start hold, the note on the play position sounds at its frame");
    }
    {
        Rig rig(false); // what playback did before: no hold
        rig.engine.setGlobalSamplePos(kBeat);
        rig.engine.setTransportPlaying(true);
        for (int i = 0; i < 40; ++i) rig.block();
        std::printf("  (without the hold the first note-on lands at frame %llu, %lld frames late)\n",
                    static_cast<unsigned long long>(rig.synth->firstNoteOn()),
                    static_cast<long long>(rig.synth->firstNoteOn()) - static_cast<long long>(kBeat));
    }
}

void testStopReleasesAndPlayStartsClean() {
    Rig rig(true);
    rig.engine.setGlobalSamplePos(kBeat);
    rig.engine.setTransportPlaying(true);
    for (int i = 0; i < 40; ++i) rig.block();
    check(rig.synth->firstNoteOn() == kBeat, "B: the held note started");

    size_t mark = rig.synth->log.size();
    rig.engine.setTransportPlaying(false);
    for (int i = 0; i < 4; ++i) rig.block();
    check(rig.synth->sawController(123, mark), "B: stop sends All Notes Off to the plugin");
    check(rig.synth->sawController(64, mark), "B: and releases the sustain pedal");
    check(!rig.synth->sawController(120, mark), "B: but does not cut the sound (tails ring)");

    mark = rig.synth->log.size();
    rig.engine.setTransportPlaying(true);
    for (int i = 0; i < 40; ++i) rig.block();
    check(rig.synth->sawController(120, mark), "C: a new play cuts leftovers (All Sound Off) first");
}

void testStopCutsSoundPreference() {
    Rig rig(true);
    rig.engine.setStopCutsSound(true);
    rig.engine.setGlobalSamplePos(kBeat);
    rig.engine.setTransportPlaying(true);
    for (int i = 0; i < 40; ++i) rig.block();
    const size_t mark = rig.synth->log.size();
    rig.engine.setTransportPlaying(false);
    for (int i = 0; i < 4; ++i) rig.block();
    check(rig.synth->sawController(120, mark), "D: with 'stop cuts sound', stop sends All Sound Off");
}

} // namespace

int main() {
    std::printf("TransportEdgesTest\n");
    testPlayStartIsSampleAccurate();
    testStopReleasesAndPlayStartsClean();
    testStopCutsSoundPreference();
    return g_failures == 0 ? 0 : 1;
}
