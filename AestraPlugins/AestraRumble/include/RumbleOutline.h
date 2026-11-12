// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The outline of a Rumble sound, as the approved sketch draws it on every card and in the picture
// (SPEC-025, slice R3), and the worker that makes outlines away from the UI thread.
//
// An outline is one render of the sound at the sketch's picture note (F1, MIDI 29) folded into 260 peak bins
// on a square-root time scale, so the first tenth of a second gets room. Every sound is levelled to the same
// peak by the voice, so outlines compare by shape and never by volume. RumbleOutlineTest holds the bins to
// golden data made by the sketch's own peaks() (tools/rumble/sketch_outline_golden.js).
#pragma once

#include "RumbleVoice.h"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace Aestra {
namespace Plugins {

struct RumbleOutline {
    static constexpr int kPictureNote = 29;     // F1: the note the sketch drew every sound at
    static constexpr double kSeconds = 3.9;     // how much time the picture shows
    static constexpr size_t kBins = 260;

    std::array<float, kBins> bins{}; // the levelled sound's peak in each bin, 0 to RumbleVoice::kLevelledPeak
    double rawPeak = 0.0;            // the sound's loudest raw sample at the picture note, before levelling
    double lengthSeconds = 0.0;      // how long it rings

    // One render, a few milliseconds of work: never on the UI or audio thread (use RumbleOutlineWorker).
    static RumbleOutline of(const RumbleSound& sound, double sampleRate);

    // Where a moment sits across the picture, 0 to 1 (the sketch's square-root scale), and back.
    static double xOf(double seconds);
    static double secondsAt(double x);
    // Bin j is drawn at xOf the start of its time span, which is j / kBins: the bins are already spaced on the
    // square-root scale. (The sketch drew them at sqrt(j / kBins), off its own grid; corrected 2026-11-12.)
    static double xOfBin(size_t bin);

    // How far above its note the sound is at a moment, in semitones: the pitch line over the picture.
    static double pitchAbove(const RumbleSound& sound, double seconds);
};

// Makes outlines on its own thread, each sound once. The UI asks with request() and draws whatever find()
// has; a card whose outline has not arrived yet shows as filling. The newest request is made first, so the
// sound in front of the producer arrives before older ones. Thread-safe.
class RumbleOutlineWorker {
public:
    explicit RumbleOutlineWorker(double sampleRate = 48000.0, size_t capacity = 256);
    ~RumbleOutlineWorker();
    RumbleOutlineWorker(const RumbleOutlineWorker&) = delete;
    RumbleOutlineWorker& operator=(const RumbleOutlineWorker&) = delete;

    // Ask for a sound's outline. Cheap; a sound already made or already asked for is not made again.
    void request(const RumbleSound& sound);
    // The outline if it is made, else null. Cheap enough to call every frame.
    std::shared_ptr<const RumbleOutline> find(const RumbleSound& sound) const;
    // Drop every request not yet started, for when a new round replaces the one on screen.
    void forgetPending();

    // For tests and a busy indicator.
    bool waitUntilIdle(int timeoutMs) const;
    size_t rendersDone() const;

private:
    struct Key {
        std::array<double, RumbleSound::Count> v{};
        uint32_t seed = 0;
        bool operator<(const Key& other) const {
            return v != other.v ? v < other.v : seed < other.seed;
        }
    };
    struct Entry {
        std::shared_ptr<const RumbleOutline> outline;
        uint64_t lastUsed = 0;
    };
    static Key keyOf(const RumbleSound& sound);
    void run();
    void evictIfFull(); // with m_mutex held

    const double m_sampleRate;
    const size_t m_capacity;
    mutable std::mutex m_mutex;
    mutable std::condition_variable m_wake;
    mutable std::condition_variable m_idle;
    mutable std::map<Key, Entry> m_made;
    std::deque<RumbleSound> m_pending; // newest at the front
    bool m_busy = false;
    bool m_stop = false;
    mutable uint64_t m_clock = 0;
    size_t m_renders = 0;
    std::thread m_thread;
};

} // namespace Plugins
} // namespace Aestra
