// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// MixerStateResyncTest — regression for #913 (mixer-state half).
//
// State pushes are best-effort drop-newest, so a rejected push must mark the
// channel dirty and the UI pump (TrackManager::resyncDirtyChannels, driven
// from AestraContent::onUpdate) must re-push latest values until accepted.
// These tests pin that contract: dirty on rejection, converge on resync,
// install-marks-dirty for the loader path, metronome included.

#include "Core/AudioCommandQueue.h"
#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using Aestra::Audio::AudioQueueCommand;
using Aestra::Audio::AudioQueueCommandType;
using Aestra::Audio::MixerChannel;
using Aestra::Audio::TrackManager;

namespace {

int g_failures = 0;

void check(bool condition, const std::string& label) {
    std::cout << (condition ? "PASS: " : "FAIL: ") << label << "\n";
    if (!condition) {
        ++g_failures;
    }
}

void dirtyOnRejectedPush() {
    std::atomic<bool> accept{true};
    MixerChannel ch("T", 7);
    ch.setCommandSink([&](const AudioQueueCommand&) { return accept.load(std::memory_order_relaxed); });
    check(ch.resyncEngineState(), "initial resync accepted");
    check(!ch.isStateDirty(), "channel starts clean");
    accept.store(false, std::memory_order_relaxed);
    ch.setVolume(0.5f);
    check(ch.isStateDirty(), "rejected volume push marks a clean channel dirty");
    check(!ch.resyncEngineState(), "resync fails while the sink rejects");
    check(ch.isStateDirty(), "failed resync keeps the channel dirty");
    accept.store(true, std::memory_order_relaxed);
    check(ch.resyncEngineState(), "resync accepted after the sink recovers");
    check(!ch.isStateDirty(), "channel clean after recovery");
}

void resyncConvergesToLatest() {
    std::atomic<bool> accept{true};
    MixerChannel ch("T", 7);
    std::vector<AudioQueueCommand> sent;
    ch.setCommandSink([&](const AudioQueueCommand& cmd) {
        if (accept.load(std::memory_order_relaxed)) {
            sent.push_back(cmd);
            return true;
        }
        return false;
    });
    check(ch.resyncEngineState(), "initial resync accepted");
    check(!ch.isStateDirty(), "channel starts clean");
    sent.clear();
    accept.store(false, std::memory_order_relaxed);
    ch.setVolume(0.5f);
    ch.setPan(-0.3f);
    ch.setMute(true);
    ch.setSolo(true);
    check(ch.isStateDirty(), "rejected updates mark a clean channel dirty");
    accept.store(true, std::memory_order_relaxed);
    check(ch.resyncEngineState(), "resync accepted");
    check(!ch.isStateDirty(), "channel clean after resync");
    check(sent.size() == 4, "resync re-pushes all four state commands");
    if (sent.size() == 4) {
        check(sent[0].type == AudioQueueCommandType::SetTrackVolume && sent[0].value1 == 0.5f,
              "resync carries latest volume");
        check(sent[1].type == AudioQueueCommandType::SetTrackPan && sent[1].value1 == -0.3f,
              "resync carries latest pan");
        check(sent[2].type == AudioQueueCommandType::SetTrackMute && sent[2].value1 == 1.0f,
              "resync carries latest mute");
        check(sent[3].type == AudioQueueCommandType::SetTrackSolo && sent[3].value1 == 1.0f,
              "resync carries latest solo");
        check(sent[0].channelId == 7, "resync carries the stable channel id");
    }
}

void trackManagerResyncFansOut() {
    TrackManager tm;
    tm.setOutputSampleRate(48000.0);
    auto* ch = tm.addChannelWithId("T", 5);
    check(ch != nullptr, "channel created");
    std::vector<AudioQueueCommand> sent;
    tm.setCommandSink([&](const AudioQueueCommand& cmd) {
        sent.push_back(cmd);
        return true;
    });
    tm.resyncDirtyChannels();
    check(sent.size() == 4, "fresh sink install heals via one resync");
    sent.clear();
    tm.resyncDirtyChannels();
    check(sent.empty(), "clean channels push nothing on resync");
}

void metronomeResync() {
    TrackManager tm;
    tm.setOutputSampleRate(48000.0);
    std::vector<AudioQueueCommand> sent;
    tm.setCommandSink([&](const AudioQueueCommand&) { return false; });
    tm.enableMetronome(true);
    tm.setCommandSink([&](const AudioQueueCommand& cmd) {
        sent.push_back(cmd);
        return true;
    });
    tm.resyncDirtyChannels();
    bool sawMetronome = false;
    for (const auto& cmd : sent) {
        if (cmd.type == AudioQueueCommandType::SetMetronomeEnabled && cmd.value1 == 1.0f) {
            sawMetronome = true;
        }
    }
    check(sawMetronome, "dropped metronome-enable heals on resync");
}

} // namespace

int main() {
    std::cout << "=== Mixer state resync (#913) ===\n";
    dirtyOnRejectedPush();
    resyncConvergesToLatest();
    trackManagerResyncFansOut();
    metronomeResync();

    std::cout << (g_failures == 0 ? "ALL PASSED\n" : "FAILURES: " + std::to_string(g_failures) + "\n");
    return g_failures == 0 ? 0 : 1;
}
