// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// Guards the SourceManager revision counter that the timeline uses to notice
// "some source is ready but has no waveform cache yet".
//
// Why this exists: clip waveforms are drawn from a WaveformCache, and above the
// finest mip level a source without one renders as a bare centre line. Building
// those caches used to be the responsibility of whichever import path happened
// to remember, so a clip imported mid-session showed no waveform until the
// project was saved and reloaded. TrackManagerUI now sweeps for missing caches,
// change-gated on getRevision() so the steady state costs one integer compare.
//
// That makes the counter load-bearing: if a mutation stops bumping it, the sweep
// silently stops running and the waveforms silently disappear again. Each case
// below asserts the precondition first so it cannot pass vacuously.

#include "Models/SourceManager.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

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

// A newly created source bumps the revision.
void testCreateBumps() {
    SourceManager mgr;
    const uint64_t before = mgr.getRevision();

    const ClipSourceID id = mgr.getOrCreateSource("/tmp/aestra-test-a.wav");
    require(id.isValid(), "createBumps: expected a valid source id");
    require(mgr.getRevision() != before, "createBumps: creating a source must bump the revision");
}

// Re-requesting the same path dedupes to the existing source, so there is no new
// readiness transition and nothing for the sweep to do.
void testDedupeDoesNotBump() {
    SourceManager mgr;
    const ClipSourceID first = mgr.getOrCreateSource("/tmp/aestra-test-a.wav");
    const uint64_t after = mgr.getRevision();

    const ClipSourceID second = mgr.getOrCreateSource("/tmp/aestra-test-a.wav");
    require(second.value == first.value, "dedupe: same path must return the same source id");
    require(mgr.getRevision() == after, "dedupe: returning an existing source must not bump the revision");
}

// The case the bug turned on: the source already exists (so no id is minted),
// but attaching a buffer flips it to ready. That IS a transition the sweep must
// see, so the revision has to move even though the source set did not change.
void testReadinessFlipBumpsOnExistingSource() {
    SourceManager mgr;
    const char* path = "/tmp/aestra-test-b.wav";
    const ClipSourceID created = mgr.getOrCreateSource(path);
    const uint64_t beforeBuffer = mgr.getRevision();

    ClipSource* source = mgr.getSource(created);
    require(source != nullptr, "readinessFlip: source lookup failed");
    require(!source->isReady(), "readinessFlip: precondition — source must not be ready before a buffer is attached");

    const ClipSourceID recorded = mgr.createRecordedSource(path, "Take", makeBuffer());
    require(recorded.value == created.value, "readinessFlip: precondition — path dedupe must reuse the same id");
    require(mgr.getSource(recorded)->isReady(), "readinessFlip: source must be ready once a buffer is attached");
    require(mgr.getRevision() != beforeBuffer,
            "readinessFlip: attaching a buffer to an existing source must bump the revision");
}

// Removal and clear change which sources exist, so both must bump.
void testRemoveAndClearBump() {
    SourceManager mgr;
    const ClipSourceID id = mgr.getOrCreateSource("/tmp/aestra-test-c.wav");

    const uint64_t beforeRemove = mgr.getRevision();
    require(mgr.removeSource(id), "removeAndClear: precondition — removing an existing source must succeed");
    require(mgr.getRevision() != beforeRemove, "removeAndClear: removeSource must bump the revision");

    mgr.getOrCreateSource("/tmp/aestra-test-d.wav");
    const uint64_t beforeClear = mgr.getRevision();
    mgr.clear();
    require(mgr.getRevision() != beforeClear, "removeAndClear: clear must bump the revision");
}

// Removing a source that is not there changes nothing, so it must not bump —
// otherwise the sweep would be woken by a no-op.
void testFailedRemoveDoesNotBump() {
    SourceManager mgr;
    mgr.getOrCreateSource("/tmp/aestra-test-e.wav");
    const uint64_t before = mgr.getRevision();

    require(!mgr.removeSource(ClipSourceID{999999}), "failedRemove: precondition — removing an absent id must fail");
    require(mgr.getRevision() == before, "failedRemove: a failed removeSource must not bump the revision");
}

// A source restored from a serialized project (#446) comes in through
// getOrCreateSourceWithId, which also mints a source and so must bump — a loaded
// project's clips need waveforms on the same terms as freshly imported ones.
void testRestoredIdBumpsThenDedupes() {
    SourceManager mgr;
    const char* path = "/tmp/aestra-test-restored.wav";
    const uint64_t before = mgr.getRevision();

    const ClipSourceID restored = mgr.getOrCreateSourceWithId(ClipSourceID{4242}, path);
    require(restored.isValid(), "restoredId: expected a valid source id");
    require(restored.value == 4242, "restoredId: precondition — a free requested id must be restored verbatim");
    require(mgr.getRevision() != before, "restoredId: restoring a source must bump the revision");

    const uint64_t afterRestore = mgr.getRevision();
    const ClipSourceID again = mgr.getOrCreateSourceWithId(ClipSourceID{7777}, path);
    require(again.value == restored.value, "restoredId: precondition — path dedupe must win over the requested id");
    require(mgr.getRevision() == afterRestore, "restoredId: a path-deduped restore must not bump the revision");
}

// The counter only has to change on mutation; a caller comparing snapshots must
// never see it run backwards, or a sweep would be skipped.
void testRevisionIsMonotonic() {
    SourceManager mgr;
    uint64_t previous = mgr.getRevision();
    for (int i = 0; i < 8; ++i) {
        mgr.getOrCreateSource("/tmp/aestra-test-mono-" + std::to_string(i) + ".wav");
        const uint64_t current = mgr.getRevision();
        require(current >= previous, "monotonic: revision must never decrease");
        require(current != previous, "monotonic: each new source must move the revision");
        previous = current;
    }
}

} // namespace

int main() {
    testCreateBumps();
    testDedupeDoesNotBump();
    testReadinessFlipBumpsOnExistingSource();
    testRemoveAndClearBump();
    testFailedRemoveDoesNotBump();
    testRestoredIdBumpsThenDedupes();
    testRevisionIsMonotonic();

    std::cout << "[PASS] SourceManagerRevisionTest\n";
    return 0;
}
