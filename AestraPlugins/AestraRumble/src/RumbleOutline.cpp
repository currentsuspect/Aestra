// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "RumbleOutline.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace Aestra {
namespace Plugins {

// The sketch's peaks(): bin j covers the time from (j/N)^2 to ((j+1)/N)^2 of the picture, at least one sample,
// and takes the loudest of every third sample in it.
RumbleOutline RumbleOutline::of(const RumbleSound& sound, double sampleRate) {
    RumbleOutline outline;
    const std::vector<float> rendered = RumbleVoice::render(sound, kPictureNote, sampleRate);
    const double span = kSeconds * sampleRate;
    const double n = static_cast<double>(kBins);
    for (size_t j = 0; j < kBins; ++j) {
        const double a = static_cast<double>(j) / n;
        const double b = static_cast<double>(j + 1) / n;
        const size_t i0 = static_cast<size_t>(std::floor(a * a * span));
        const size_t i1 = std::max(i0 + 1, static_cast<size_t>(std::floor(b * b * span)));
        float loudest = 0.0f;
        for (size_t i = i0; i < i1 && i < rendered.size(); i += 3) {
            loudest = std::max(loudest, std::fabs(rendered[i]));
        }
        outline.bins[j] = loudest;
    }
    outline.rawPeak = RumbleVoice::rawPeak(sound, kPictureNote, sampleRate);
    outline.lengthSeconds = RumbleVoice::lengthSeconds(sound);
    return outline;
}

double RumbleOutline::xOf(double seconds) {
    return std::sqrt(std::clamp(seconds, 0.0, kSeconds) / kSeconds);
}

double RumbleOutline::secondsAt(double x) {
    const double clamped = std::clamp(x, 0.0, 1.0);
    return clamped * clamped * kSeconds;
}

// Bin j starts at (j / kBins)^2 of the picture's time, so on the square-root scale it sits at j / kBins.
double RumbleOutline::xOfBin(size_t bin) {
    return static_cast<double>(bin) / static_cast<double>(kBins);
}

double RumbleOutline::pitchAbove(const RumbleSound& sound, double seconds) {
    return RumbleVoice::dropSemitones(sound[RumbleSound::Punch]) *
           std::exp(-std::max(0.0, seconds) / RumbleVoice::dropSeconds(sound));
}

RumbleOutlineWorker::RumbleOutlineWorker(double sampleRate, size_t capacity)
    : m_sampleRate(sampleRate), m_capacity(std::max<size_t>(1, capacity)), m_thread([this] { run(); }) {}

RumbleOutlineWorker::~RumbleOutlineWorker() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_pending.clear();
    }
    m_wake.notify_all();
    if (m_thread.joinable()) {
        m_thread.join();
    }
}

RumbleOutlineWorker::Key RumbleOutlineWorker::keyOf(const RumbleSound& sound) {
    Key key;
    key.v = sound.v;
    key.seed = sound.seed;
    return key;
}

void RumbleOutlineWorker::request(const RumbleSound& sound) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_stop || m_made.count(keyOf(sound)) != 0) {
            return;
        }
        // Asked again while waiting: move it to the front instead of queueing it twice.
        const Key key = keyOf(sound);
        m_pending.erase(std::remove_if(m_pending.begin(), m_pending.end(),
                                       [&](const RumbleSound& queued) {
                                           const Key other = keyOf(queued);
                                           return !(key < other) && !(other < key);
                                       }),
                        m_pending.end());
        m_pending.push_front(sound);
    }
    m_wake.notify_one();
}

std::shared_ptr<const RumbleOutline> RumbleOutlineWorker::find(const RumbleSound& sound) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto found = m_made.find(keyOf(sound));
    if (found == m_made.end()) {
        return nullptr;
    }
    found->second.lastUsed = ++m_clock;
    return found->second.outline;
}

void RumbleOutlineWorker::forgetPending() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_pending.clear();
    if (!m_busy) {
        m_idle.notify_all();
    }
}

bool RumbleOutlineWorker::waitUntilIdle(int timeoutMs) const {
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_idle.wait_for(lock, std::chrono::milliseconds(timeoutMs),
                           [this] { return m_stop || (!m_busy && m_pending.empty()); });
}

size_t RumbleOutlineWorker::rendersDone() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_renders;
}

void RumbleOutlineWorker::evictIfFull() {
    while (m_made.size() >= m_capacity) {
        auto oldest = m_made.begin();
        for (auto it = m_made.begin(); it != m_made.end(); ++it) {
            if (it->second.lastUsed < oldest->second.lastUsed) {
                oldest = it;
            }
        }
        m_made.erase(oldest);
    }
}

void RumbleOutlineWorker::run() {
    std::unique_lock<std::mutex> lock(m_mutex);
    for (;;) {
        m_wake.wait(lock, [this] { return m_stop || !m_pending.empty(); });
        if (m_stop) {
            break;
        }
        const RumbleSound sound = m_pending.front();
        m_pending.pop_front();
        if (m_made.count(keyOf(sound)) != 0) {
            continue;
        }
        m_busy = true;
        lock.unlock();
        auto outline = std::make_shared<const RumbleOutline>(RumbleOutline::of(sound, m_sampleRate));
        lock.lock();
        m_busy = false;
        ++m_renders;
        evictIfFull();
        m_made[keyOf(sound)] = Entry{std::move(outline), ++m_clock};
        if (m_pending.empty()) {
            m_idle.notify_all();
        }
    }
    m_busy = false;
    m_idle.notify_all();
}

} // namespace Plugins
} // namespace Aestra
