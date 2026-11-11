// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "AudioEngine.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <functional>
#include <thread>

namespace Aestra {
namespace Audio {

namespace {
// A nonzero tag per thread. std::this_thread::get_id() wraps pthread_self()/GetCurrentThreadId():
// no allocation, no lock, safe on the audio thread.
size_t currentThreadTag() noexcept { return std::hash<std::thread::id>{}(std::this_thread::get_id()) | 1u; }
} // namespace

void AudioEngine::beginManualRender() {
    const size_t self = currentThreadTag();
    if (m_manualRenderOwner.load(std::memory_order_acquire) == self) {
        ++m_manualRenderNesting;
        return;
    }
    // Another thread's manual render runs to completion first.
    size_t expected = 0;
    while (!m_manualRenderOwner.compare_exchange_weak(expected, self, std::memory_order_seq_cst)) {
        expected = 0;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    m_manualRenderNesting = 1;
    // A live block may be mid-flight. It counts itself in before it reads the owner (seq_cst on
    // both sides), so once the count drains every later live block sees the lease and plays silence.
    for (int waited = 0; m_processBlocksInFlight.load(std::memory_order_seq_cst) != 0; ++waited) {
        if (waited == 2000) {
            Aestra::Log::error("[AudioEngine] manual render: a live block did not finish within 2 s");
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void AudioEngine::endManualRender() {
    if (m_manualRenderOwner.load(std::memory_order_relaxed) != currentThreadTag() || m_manualRenderNesting == 0) {
        return;
    }
    if (--m_manualRenderNesting == 0) {
        m_manualRenderOwner.store(0, std::memory_order_release);
    }
}

// Driver callback entry. Every per-block buffer the engine renders into is sized
// to m_maxBufferFrames, but a backend is free to hand over more frames than the
// size it reported (WASAPI shared mode delivers up to its device buffer, which
// is larger than the requested period). Rendering such a block in one piece
// read past the master buffer. Split it into capacity-sized chunks instead, so
// an oversized callback costs extra passes, never memory safety or audio.
int AudioEngine::processBlock(float* outputBuffer, const float* inputBuffer, uint32_t numFrames, double streamTime) {
    m_processBlocksInFlight.fetch_add(1, std::memory_order_seq_cst);
    struct InFlight {
        std::atomic<uint32_t>& count;
        ~InFlight() { count.fetch_sub(1, std::memory_order_release); }
    } inFlight{m_processBlocksInFlight};
    const size_t leaseOwner = m_manualRenderOwner.load(std::memory_order_seq_cst);
    if (leaseOwner != 0 && leaseOwner != currentThreadTag()) {
        // Another thread is rendering this engine by hand (export, bounce, Muse): stand down.
        if (outputBuffer && numFrames > 0) {
            const uint32_t channels = std::max<uint32_t>(1, m_outputChannels.load(std::memory_order_relaxed));
            std::memset(outputBuffer, 0, static_cast<size_t>(numFrames) * channels * sizeof(float));
        }
        return 0;
    }

    const uint32_t capacity = m_maxBufferFrames.load(std::memory_order_relaxed);
    if (capacity == 0 || numFrames <= capacity || !outputBuffer) {
        return processBlockWithinCapacity(outputBuffer, inputBuffer, numFrames, streamTime);
    }

    // Recording input is consumed whole, once: the callback does not depend on
    // the render block size, and the input channel count is not known here to
    // slice it.
    if (inputBuffer) {
        if (auto cb = m_inputCallback.load(std::memory_order_relaxed)) {
            cb(inputBuffer, numFrames, m_inputCallbackData.load(std::memory_order_relaxed));
        }
    }

    const uint32_t channels = std::max<uint32_t>(1, m_outputChannels.load(std::memory_order_relaxed));
    const uint32_t sampleRate = m_sampleRate.load(std::memory_order_relaxed);
    uint32_t done = 0;
    int result = 0;
    while (done < numFrames) {
        const uint32_t chunk = std::min(capacity, numFrames - done);
        const double chunkTime =
            sampleRate > 0 ? streamTime + static_cast<double>(done) / static_cast<double>(sampleRate) : streamTime;
        result = processBlockWithinCapacity(outputBuffer + static_cast<size_t>(done) * channels, nullptr, chunk,
                                            chunkTime);
        done += chunk;
    }
    m_telemetry.incrementOversizedBlocks();
    return result;
}

} // namespace Audio
} // namespace Aestra
