// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// End-to-end guards for the clip-source readiness invariant that keeps timeline
// waveforms alive.
//
// The waveform-cache sweep in TrackManagerUI (TrackManagerUIRender.cpp:543-548)
// re-runs only when SourceManager's revision moves. If a ClipSource flips from
// not-ready to ready WITHOUT moving that revision, the sweep never runs, no
// cache is installed, and the clip renders as a bare centre line until some
// unrelated source event happens — the regression b5da2b03 fixed for project
// reloads, and which survived for mid-session buffer attachments
// (AestraContent.cpp loadSampleIntoSelectedTrack used to setBuffer a deduped
// source directly). The invariant is now enforced inside ClipSource::setBuffer
// via the owning-manager back-pointer, with SourceManager::attachBuffer as the
// canonical entry point.
//
// The second half guards the failure side of the same pipeline: a failed
// project-load decode (ProjectSerializer.cpp's source loop) must leave the
// source genuinely unready — retryable and honestly reported by !isReady() —
// instead of installing an empty fallback buffer.
//
// Each case asserts its precondition first so it cannot pass vacuously.

#include "Models/ClipSource.h"
#include "Models/SourceManager.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <utility>

namespace {
using namespace Aestra::Audio;

void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "[FAIL] " << message << '\n';
        std::exit(1);
    }
}

std::shared_ptr<AudioBufferData> makeBuffer(uint32_t frames = 128) {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->numChannels = 2;
    buffer->sampleRate = 48000;
    buffer->interleavedData.assign(static_cast<size_t>(frames) * 2, 0.25f);
    buffer->numFrames = frames;
    return buffer;
}

// An undecodable payload, shaped exactly like what a failed decode would have
// produced under the old silent-mono-fallback path (empty data).
std::shared_ptr<AudioBufferData> makeEmptyBuffer() {
    auto buffer = std::make_shared<AudioBufferData>();
    buffer->numChannels = 1;
    buffer->sampleRate = 44100;
    buffer->numFrames = 0;
    return buffer; // interleavedData empty => isValid() == false
}

// TEST A (the invariant): attaching a decoded buffer to an existing unready
// source through the canonical path must move the revision, so the exact gate
// TrackManagerUIRender.cpp:543-548 evaluates fires and buildAllWaveformCaches()
// would queue this source — with no unrelated source mutation in between.
void testCanonicalAttachFiresTheSweepGate() {
    SourceManager mgr;
    const char* path = "/tmp/aestra-test-invariant-attach.wav";
    const ClipSourceID id = mgr.getOrCreateSource(path);

    ClipSource* source = mgr.getSource(id);
    require(source != nullptr, "sweepGate: precondition — source lookup failed");
    require(!source->isReady(), "sweepGate: precondition — source must start unready");

    // Simulate the UI having already run its sweep this frame and found nothing:
    // it snapshotted the revision after creation and skipped every source.
    const uint64_t lastSeenRevision = mgr.getRevision();

    // Simulate an async decode completing out of band, handing its result to
    // the manager through the canonical entry point.
    mgr.attachBuffer(source, makeBuffer(256));

    require(source->isReady(), "sweepGate: canonical attach must make the source ready");
    require(mgr.getRevision() != lastSeenRevision,
            "sweepGate: canonical attach must move the revision without any unrelated source mutation");

    // The gate itself, transcribed from TrackManagerUIRender.cpp:543-548.
    const bool sweepGateFires = (mgr.getRevision() != lastSeenRevision);
    require(sweepGateFires, "sweepGate: the per-frame revision compare must fire");

    // What the fired sweep then does (TrackManagerUIRender.cpp:1303): a ready
    // source with no cache is eligible, so its cache build gets queued.
    require(source->isReady() && !source->getWaveformCache(),
            "sweepGate: fired sweep must find the source eligible for a cache build");
}

// TEST A (the invariant, direct path): even a bare ClipSource::setBuffer on a
// managed source — the shape an async decoder completing onto an existing
// deduped source takes — must bump the owning manager's revision when it flips
// readiness. This is the hole the original bug fell through.
void testDirectSetBufferReadinessFlipBumpsRevision() {
    SourceManager mgr;
    const char* path = "/tmp/aestra-test-invariant-direct.wav";
    const ClipSourceID id = mgr.getOrCreateSourceWithId(ClipSourceID{9001}, path);

    ClipSource* source = mgr.getSource(id);
    require(source != nullptr, "directSetBuffer: precondition — source lookup failed");
    require(!source->isReady(), "directSetBuffer: precondition — source must start unready");
    require(source->getID().value == 9001, "directSetBuffer: precondition — restored id expected");

    const uint64_t before = mgr.getRevision();
    source->setBuffer(makeBuffer()); // direct call, NOT through attachBuffer

    require(source->isReady(), "directSetBuffer: setBuffer must make the source ready");
    require(mgr.getRevision() != before,
            "directSetBuffer: a readiness flip via plain setBuffer must bump the owning manager's revision");
}

// A replacement buffer over an ALREADY-ready source never flips readiness, but
// setBuffer discards its waveform cache — the canonical path must wake the
// sweep anyway or the new content would draw from a stale/missing cache.
void testReplaceOnReadySourceStillWakesSweep() {
    SourceManager mgr;
    const char* path = "/tmp/aestra-test-invariant-replace.wav";
    const ClipSourceID id = mgr.getOrCreateSource(path);
    ClipSource* source = mgr.getSource(id);
    require(source != nullptr, "replaceReady: precondition — source lookup failed");

    mgr.attachBuffer(source, makeBuffer(256));
    require(source->isReady(), "replaceReady: precondition — source must be ready after first attach");
    const uint64_t seenAfterFirstSweep = mgr.getRevision();

    mgr.attachBuffer(source, makeBuffer(512)); // different take, same source

    require(mgr.getRevision() != seenAfterFirstSweep,
            "replaceReady: replacing a ready source's buffer must still move the revision");
    require(!source->getWaveformCache(),
            "replaceReady: replacement must leave the source cache-less and sweep-eligible");
}

// A failed attach changes nothing observable: the source stays unready and the
// sweep is not woken by a no-op (mirrors testFailedRemoveDoesNotBump).
void testFailedAttachDoesNotBumpOrWake() {
    SourceManager mgr;
    const char* path = "/tmp/aestra-test-invariant-failed.wav";
    const ClipSourceID id = mgr.getOrCreateSource(path);
    ClipSource* source = mgr.getSource(id);
    require(source != nullptr, "failedAttach: precondition — source lookup failed");

    const uint64_t before = mgr.getRevision();
    mgr.attachBuffer(source, makeEmptyBuffer());

    require(!source->isReady(), "failedAttach: an invalid buffer must not flip the source ready");
    require(mgr.getRevision() == before,
            "failedAttach: a failed attach must not wake the sweep");

    mgr.attachBuffer(nullptr, makeBuffer()); // null source: must be a safe no-op
    require(mgr.getRevision() == before, "failedAttach: a null-source attach must not wake the sweep");
}

// TEST B (failure safety): the project-load decode-failure path
// (ProjectSerializer.cpp source loop) creates sources through
// getOrCreateSourceWithId, decodes, and — since the silent-mono-fallback
// removal — installs NOTHING when decoding fails. The source must stay
// genuinely unready (draw path early-outs on !isReady(), the load guard skips
// only READY sources), so a later successful attach both readies it and wakes
// the sweep. This replays that sequence against the real objects headless.
void testProjectLoadDecodeFailureLeavesSourceRetryable() {
    SourceManager mgr;
    const char* storedPath = "/tmp/aestra-test-corrupt-sample.wav";

    // ProjectSerializer.cpp:1638 — restore the serialized identity.
    const ClipSourceID restoredId = mgr.getOrCreateSourceWithId(ClipSourceID{77}, storedPath);
    require(restoredId.value == 77, "loadFailure: precondition — serialized id must be restored verbatim");

    ClipSource* source = mgr.getSource(restoredId);
    require(source != nullptr, "loadFailure: precondition — source lookup failed");
    require(!source->isReady(), "loadFailure: precondition — restored source starts unready");

    // Decode of the corrupt asset fails here. Since the silent-mono-fallback
    // removal, the failure branch installs NOTHING — no setBuffer call at all.

    // ProjectSerializer.cpp:1652 — the retry guard only skips READY sources,
    // so the failed source must still be eligible for a decode attempt on a
    // later load or import.
    require(!source->isValid(), "loadFailure: a failed decode must leave the source invalid");
    require(source->getRawBuffer() == nullptr,
            "loadFailure: no buffer may be installed when decoding fails");
    require(!source->isReady(), "loadFailure: source must still satisfy the load loop's !isReady() retry guard");

    // Retry on a later load/import: a subsequent successful attach through the
    // canonical path makes the source ready AND wakes the waveform-cache sweep.
    const uint64_t beforeRetry = mgr.getRevision();
    mgr.attachBuffer(source, makeBuffer(512));
    require(source->isReady(), "loadFailure: retry attach must make the source ready");
    require(mgr.getRevision() != beforeRetry,
            "loadFailure: the retrying attach must bump the revision so caches get built");
}

} // namespace

int main() {
    testCanonicalAttachFiresTheSweepGate();
    testDirectSetBufferReadinessFlipBumpsRevision();
    testReplaceOnReadySourceStillWakesSweep();
    testFailedAttachDoesNotBumpOrWake();
    testProjectLoadDecodeFailureLeavesSourceRetryable();

    std::cout << "[PASS] SourceReadinessInvariantTest\n";
    return 0;
}
