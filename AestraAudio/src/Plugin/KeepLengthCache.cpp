// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Plugin/KeepLengthCache.h"

#include "GarbageCollector.h"

#include "signalsmith-stretch/signalsmith-stretch.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <limits>
#include <thread>

namespace Aestra {
namespace Audio {
namespace Plugins {

namespace {

// One background thread renders for every sampler. Caches register as weak_ptr, so a
// sampler deleted mid-render stays alive until that render finishes (on this thread).
class KeepLengthRenderService {
public:
    static KeepLengthRenderService& instance() {
        static KeepLengthRenderService service;
        return service;
    }

    void add(std::weak_ptr<KeepLengthCache> cache) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_caches.push_back(std::move(cache));
            if (!m_thread.joinable()) {
                m_thread = std::thread([this] { run(); });
            }
        }
        wake();
    }

    void wake() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_woken = true;
        }
        m_cv.notify_one();
    }

    ~KeepLengthRenderService() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_one();
        if (m_thread.joinable()) m_thread.join();
    }

private:
    void run() {
        std::vector<std::shared_ptr<KeepLengthCache>> live;
        while (true) {
            live.clear();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stop) return;
                m_caches.erase(std::remove_if(m_caches.begin(), m_caches.end(),
                                              [](const std::weak_ptr<KeepLengthCache>& w) { return w.expired(); }),
                               m_caches.end());
                for (const auto& w : m_caches) {
                    if (auto c = w.lock()) live.push_back(std::move(c));
                }
            }
            // One render per cache per pass, so one sampler's backlog can't starve another.
            bool didWork = false;
            for (const auto& c : live) {
                didWork = c->renderOnePending() || didWork;
            }
            live.clear(); // drop references before sleeping
            if (didWork) continue;
            // Audio-thread requests can't signal (no locks there), so idle waits are short polls.
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(10), [this] { return m_stop || m_woken; });
            m_woken = false;
        }
    }

    std::mutex m_mutex;
    std::condition_variable m_cv;
    std::vector<std::weak_ptr<KeepLengthCache>> m_caches;
    std::thread m_thread;
    bool m_stop = false;
    bool m_woken = false;
};

// Stretch blocks of ~50 ms keep attacks tight; pitch accuracy is not at stake here because
// the stretcher never transposes (the sampler's resampler applies the pitch).
constexpr double STRETCH_BLOCK_SECONDS = 0.05;
constexpr long STRETCH_SEED = 0x5eed; // fixed: the same project must render the same audio

} // namespace

KeepLengthCache::~KeepLengthCache() {
    for (auto& slot : m_slots) {
        retire(slot.buffer.exchange(nullptr, std::memory_order_acq_rel));
    }
}

void KeepLengthCache::ensureRegistered() {
    if (!m_registered.exchange(true, std::memory_order_acq_rel)) {
        KeepLengthRenderService::instance().add(weak_from_this());
    }
}

void KeepLengthCache::retire(std::shared_ptr<const Buffer> buffer) {
    if (buffer) {
        GarbageCollector::instance().release(std::move(buffer), "KeepLengthCache::Buffer");
    }
}

void KeepLengthCache::setSource(std::shared_ptr<const std::vector<float>> interleavedStereo, uint32_t sourceRate) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_source = std::move(interleavedStereo);
    m_sourceRate = sourceRate;
    ++m_generation; // renders in flight for the old source are discarded on completion
    for (auto& slot : m_slots) {
        slot.state.store(Empty, std::memory_order_release);
        retire(slot.buffer.exchange(nullptr, std::memory_order_acq_rel));
    }
    m_bytes.store(0, std::memory_order_relaxed);
    m_settled.notify_all();
}

void KeepLengthCache::prewarm(const std::vector<int>& semitones) {
    ensureRegistered();
    for (int s : semitones) {
        request(s);
    }
    KeepLengthRenderService::instance().wake();
}

bool KeepLengthCache::waitUntilSettled(const std::vector<int>& semitones, std::chrono::milliseconds timeout) {
    prewarm(semitones);
    std::unique_lock<std::mutex> lock(m_mutex);
    return m_settled.wait_for(lock, timeout, [&] {
        for (int s : semitones) {
            if (!inRange(s)) continue;
            const uint8_t st = m_slots[slotIndex(s)].state.load(std::memory_order_acquire);
            if (st != Ready && st != Unsupported) return false;
        }
        return true;
    });
}

std::shared_ptr<const KeepLengthCache::Buffer> KeepLengthCache::tryGet(int semitones) noexcept {
    if (!inRange(semitones)) return nullptr;
    Slot& slot = m_slots[slotIndex(semitones)];
    auto buffer = slot.buffer.load(std::memory_order_acquire);
    if (buffer) {
        slot.lastUsed.store(m_useClock.fetch_add(1, std::memory_order_relaxed), std::memory_order_relaxed);
    }
    return buffer;
}

void KeepLengthCache::request(int semitones) noexcept {
    if (!inRange(semitones)) return;
    uint8_t expected = Empty;
    m_slots[slotIndex(semitones)].state.compare_exchange_strong(expected, Pending, std::memory_order_acq_rel);
}

bool KeepLengthCache::renderOnePending() {
    for (size_t idx = 0; idx < m_slots.size(); ++idx) {
        uint8_t expected = Pending;
        if (!m_slots[idx].state.compare_exchange_strong(expected, Rendering, std::memory_order_acq_rel)) {
            continue;
        }

        std::shared_ptr<const std::vector<float>> source;
        uint32_t rate = 0;
        uint64_t generation = 0;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            source = m_source;
            rate = m_sourceRate;
            generation = m_generation;
        }
        const int semitones = static_cast<int>(idx) + MIN_SEMITONES;
        auto finish = [&](std::shared_ptr<const Buffer> rendered, SlotState state) {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (generation != m_generation) return; // the source changed while rendering
            if (rendered) {
                m_bytes.fetch_add(rendered->interleaved.size() * sizeof(float), std::memory_order_relaxed);
                m_slots[idx].lastUsed.store(m_useClock.fetch_add(1, std::memory_order_relaxed),
                                            std::memory_order_relaxed);
                retire(m_slots[idx].buffer.exchange(std::move(rendered), std::memory_order_acq_rel));
            }
            m_slots[idx].state.store(state, std::memory_order_release);
            evictToBudget(idx);
            m_settled.notify_all();
        };

        if (!source || rate == 0 || source->size() < 4) {
            finish(nullptr, Unsupported);
            return true;
        }

        const size_t frames = source->size() / 2;
        const double ratio = std::pow(2.0, semitones / 12.0);
        const size_t outFrames = static_cast<size_t>(std::llround(static_cast<double>(frames) * ratio));
        std::vector<float> inL(frames), inR(frames), outL(outFrames), outR(outFrames);
        for (size_t i = 0; i < frames; ++i) {
            inL[i] = (*source)[i * 2];
            inR[i] = (*source)[i * 2 + 1];
        }

        signalsmith::stretch::SignalsmithStretch<float> stretch(STRETCH_SEED);
        const int block = std::max(256, static_cast<int>(rate * STRETCH_BLOCK_SECONDS));
        stretch.configure(2, block, block / 4);
        const float* inputs[2] = {inL.data(), inR.data()};
        float* outputs[2] = {outL.data(), outR.data()};
        // Time only: transpose stays 1, frequencies pass through untouched.
        if (!stretch.exact(inputs, static_cast<int>(frames), outputs, static_cast<int>(outFrames))) {
            finish(nullptr, Unsupported); // too short to stretch; the note plays resampled
            return true;
        }

        auto buffer = std::make_shared<Buffer>();
        buffer->interleaved.resize(outFrames * 2);
        for (size_t i = 0; i < outFrames; ++i) {
            const float l = outL[i], r = outR[i];
            buffer->interleaved[i * 2] = std::isfinite(l) ? l : 0.0f;
            buffer->interleaved[i * 2 + 1] = std::isfinite(r) ? r : 0.0f;
        }
        finish(std::move(buffer), Ready);
        return true;
    }
    return false;
}

void KeepLengthCache::evictToBudget(size_t keepIndex) {
    while (m_bytes.load(std::memory_order_relaxed) > MEMORY_BUDGET_BYTES) {
        size_t victim = m_slots.size();
        uint64_t oldest = std::numeric_limits<uint64_t>::max();
        for (size_t i = 0; i < m_slots.size(); ++i) {
            if (i == keepIndex || m_slots[i].state.load(std::memory_order_acquire) != Ready) continue;
            const uint64_t used = m_slots[i].lastUsed.load(std::memory_order_relaxed);
            if (used < oldest) {
                oldest = used;
                victim = i;
            }
        }
        if (victim == m_slots.size()) return; // only the newest remains; keep it even over budget
        auto old = m_slots[victim].buffer.exchange(nullptr, std::memory_order_acq_rel);
        m_slots[victim].state.store(Empty, std::memory_order_release);
        if (old) {
            m_bytes.fetch_sub(old->interleaved.size() * sizeof(float), std::memory_order_relaxed);
            retire(std::move(old));
        }
    }
}

} // namespace Plugins
} // namespace Audio
} // namespace Aestra
