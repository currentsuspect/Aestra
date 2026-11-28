// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The audio-thread side of the sandbox hand-over.
//
// The helper process runs the plugin one chunk behind the engine: a chunk goes over in
// one callback and its result plays in the next. Three things keep that delay constant:
//   - the helper always gets chunks of one size (the callback size, learned here), cut
//     from an input FIFO, so a callback the engine splits (a loop wrap) or the first
//     callback after an insert no longer drops or truncates audio;
//   - every chunk carries its position on the input timeline, and a callback plays the
//     wet frames for exactly "its own position minus one chunk": silence before the first
//     chunk, the input dry when the helper has nothing for it, never wet audio that is late;
//   - after a stall the input backlog is cut back to one chunk, so the helper works on the
//     newest audio. Counting frames instead (a FIFO primed with one chunk of silence) left a
//     missed deadline as a permanent extra chunk of delay (#1229).
// getLatencySamples() reports that chunk, which is what PDC must compensate.
#include "OutOfProcessPluginInstance.h"

#include "Plugin/MidiSlice.h"
#include "Plugin/OfflineRenderMode.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <thread>
#include <vector>

namespace Aestra {
namespace Audio {

namespace {
constexpr uint32_t kMaxOutputChannels = 32;
} // namespace

void OutOfProcessPluginInstance::process(const float* const* inputs, float** outputs, uint32_t numInputChannels,
                                         uint32_t numOutputChannels, uint32_t numFrames, const MidiBuffer* midiInput,
                                         MidiBuffer* midiOutput) {
    if (midiOutput) {
        midiOutput->clear();
    }

    // A render starting or ending changes what this plugin's hand-off means: its FIFOs, timeline and queued MIDI belong to the
    // other mode, and the plugin may still be ringing. Left alone, live audio leaks into the first samples of a bounce (a click
    // that differs each time) and the render's tail leaks into live playback afterwards (crackle).
    if (const uint32_t epoch = OfflineRenderMode::epoch(); epoch != m_seenRenderEpoch) {
        m_seenRenderEpoch = epoch;
        flushForRenderBoundary(OfflineRenderMode::active());
    }

    // An offline render (export, bounce, freeze) uses much larger blocks than the plugin was prepared for (the exporter renders 4096
    // frames, the plugin was initialised for the device's few hundred), and a block over the plugin's maximum is passed straight
    // through: silence for an instrument, dry for an effect, so a bounce lost every sandboxed plugin's audio. Offline, such a block
    // is cut into slices of the plugin's own chunk size and each slice is processed in turn, which is what playback does. The slice
    // is the chunk the plugin already uses live, so the delay it reports for delay compensation stays the one it applies here.
    if (OfflineRenderMode::active() && m_maxBlockSize > 0 && numFrames > m_maxBlockSize && !m_crashed.load(std::memory_order_acquire)) {
        const uint32_t learned = m_chunkFrames.load(std::memory_order_relaxed);
        const uint32_t slice = learned > 0 ? std::min(learned, m_maxBlockSize) : m_maxBlockSize;
        std::array<const float*, kMaxOutputChannels> inSlice{};
        std::array<float*, kMaxOutputChannels> outSlice{};
        const uint32_t inCount = std::min<uint32_t>(numInputChannels, kMaxOutputChannels);
        const uint32_t outCount = std::min<uint32_t>(numOutputChannels, kMaxOutputChannels);
        for (uint32_t start = 0; start < numFrames; start += slice) {
            const uint32_t frames = std::min(slice, numFrames - start);
            for (uint32_t c = 0; c < inCount; ++c) inSlice[c] = (inputs && inputs[c]) ? inputs[c] + start : nullptr;
            for (uint32_t c = 0; c < outCount; ++c) outSlice[c] = (outputs && outputs[c]) ? outputs[c] + start : nullptr;
            MidiBuffer sliceMidi;
            if (midiInput) splitMidiForSlice(*midiInput, start, frames, sliceMidi);
            process(inputs ? inSlice.data() : nullptr, outSlice.data(), inCount, outCount, frames, midiInput ? &sliceMidi : nullptr,
                    nullptr);
        }
        return;
    }

    if (m_crashed.load(std::memory_order_acquire) || m_maxBlockSize == 0 || numFrames > m_maxBlockSize) {
        passThrough(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
        return;
    }

    // The chunk grows to the largest callback seen (and PDC hears about the new size).
    const uint32_t previousChunk = m_chunkFrames.load(std::memory_order_relaxed);
    if (numFrames > previousChunk) {
        m_chunkFrames.store(numFrames, std::memory_order_release);
    }
    const uint32_t chunk = std::max(previousChunk, numFrames);

    // 1. A finished chunk joins the output, at its place on the timeline. A gap (a result the
    //    helper could not hand over) restarts the output there.
    const auto takeFinishedChunk = [&] {
        uint8_t readyFull = 2;
        if (m_readyState.compare_exchange_strong(readyFull, 3, std::memory_order_acq_rel)) {
            const uint32_t frames = std::min(m_readyFrames.load(std::memory_order_acquire), m_maxBlockSize);
            if (m_outFifo.frames() == 0 || m_readyStart != m_outStart + m_outFifo.frames()) {
                m_outFifo.clear();
                m_outStart = m_readyStart;
            }
            m_outFifo.pushInterleaved(m_readyOutput.data(), frames);
            m_readyState.store(0, std::memory_order_release);
            m_helperBlocks.fetch_add(1, std::memory_order_relaxed);
        }
    };
    takeFinishedChunk();
    const uint64_t processedBeforeSend = m_processedBlocks.load(std::memory_order_acquire);
    bool sentChunk = false;

    // 2. This callback's input and MIDI join the input FIFO.
    const uint64_t callbackStart = m_inFifoEnd;
    const size_t queued = m_inFifo.pushPlanar(inputs, numInputChannels, numFrames);
    m_inFifoEnd += queued;
    if (midiInput) {
        for (size_t i = 0; i < midiInput->getEventCount(); ++i) {
            const auto& event = midiInput->getEvent(i);
            if (event.size != 3) {
                continue;
            }
            const uint32_t offset = std::min<uint32_t>(event.sampleOffset, numFrames > 0 ? numFrames - 1 : 0);
            if (!m_midiQueue.push(callbackStart + offset, event.data)) {
                m_midiDrops.fetch_add(1, std::memory_order_relaxed);
            }
        }
    }

    // 3. One chunk goes to the helper when its slot is free. Two chunks or more waiting means
    //    the helper fell behind: their time has passed (they played dry), so only the newest
    //    goes. Their MIDI is kept and lands at the start of the chunk that does go.
    if (chunk > 0 && m_inFifo.frames() >= chunk) { // no chunk before the first frames (a 0-frame callback)
        uint8_t pendingEmpty = 0;
        if (m_pendingState.compare_exchange_strong(pendingEmpty, 1, std::memory_order_acq_rel)) {
            while (m_inFifo.frames() >= 2 * static_cast<size_t>(chunk)) {
                m_inFifo.discard(chunk);
            }
            const uint64_t chunkStart = m_inFifoEnd - m_inFifo.frames();
            m_inFifo.popInterleaved(m_pendingInput.data(), chunk);
            const size_t midiBytes = m_midiQueue.takeChunk(chunkStart, chunkStart + chunk, m_pendingMidiData.data(),
                                                           m_pendingMidiData.size());
            m_pendingTransport = HostTransportClock::current();
            m_pendingStart = chunkStart;
            m_pendingMidiBytes.store(midiBytes, std::memory_order_release);
            m_pendingFrames.store(chunk, std::memory_order_release);
            m_pendingState.store(2, std::memory_order_release);
            wakeWorker();
            sentChunk = true;
        } else {
            m_unsentBlocks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (queued < numFrames) {
        m_unsentBlocks.fetch_add(1, std::memory_order_relaxed); // the helper fell a whole FIFO behind
    }

    // 3b. An offline render (a bounce, a freeze, an export) runs flat out, far faster than the helper can follow, so with the
    //     hand-off above nearly every block would play dry or go unsent and be baked into the audio. Offline, the chunk just
    //     handed over is waited for and taken at once: the same one-chunk delay (so delay compensation lines up), but exact.
    //     Never on the live audio thread: only the exporter sets this, around a render on its own thread.
    if (sentChunk && OfflineRenderMode::active()) {
        const auto giveUpAt = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (m_processedBlocks.load(std::memory_order_acquire) == processedBeforeSend &&
               !m_crashed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < giveUpAt) {
            std::this_thread::yield();
        }
        takeFinishedChunk();
    }

    // 4. Play the wet frames for one chunk ago. Late ones are skipped. Before the first chunk
    //    there is nothing yet (silence); with no wet frame at all, the helper missed its
    //    deadline and the input plays dry for this callback.
    const int64_t want = static_cast<int64_t>(callbackStart) - static_cast<int64_t>(chunk);
    if (m_outFifo.frames() > 0 && static_cast<int64_t>(m_outStart) < want) {
        const uint64_t late = std::min<uint64_t>(m_outFifo.frames(), static_cast<uint64_t>(want) - m_outStart);
        m_outFifo.discard(late);
        m_outStart += late;
    }
    const int64_t wetFrom = m_outFifo.frames() > 0 ? static_cast<int64_t>(m_outStart) - want : numFrames;
    const uint32_t lead = static_cast<uint32_t>(std::clamp<int64_t>(wetFrom, 0, numFrames));
    const uint32_t wet = static_cast<uint32_t>(std::min<size_t>(m_outFifo.frames(), numFrames - lead));
    if (wet == 0 && want + static_cast<int64_t>(numFrames) > 0) {
        m_dryBlocks.fetch_add(1, std::memory_order_relaxed);
        passThrough(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
        return;
    }
    // Silence around the wet frames: before the first chunk, or where the chunk grew.
    std::array<float*, kMaxOutputChannels> at{};
    const uint32_t channels = std::min(numOutputChannels, kMaxOutputChannels);
    for (uint32_t c = 0; c < numOutputChannels; ++c) {
        if (!outputs || !outputs[c]) {
            continue;
        }
        if (c >= channels) {
            std::fill(outputs[c], outputs[c] + numFrames, 0.0f);
            continue;
        }
        std::fill(outputs[c], outputs[c] + lead, 0.0f);
        std::fill(outputs[c] + lead + wet, outputs[c] + numFrames, 0.0f);
        at[c] = outputs[c] + lead;
    }
    m_outFifo.popPlanar(at.data(), channels, wet); // channels past the transport's are silenced
    m_outStart += wet;
}

void OutOfProcessPluginInstance::flushForRenderBoundary(bool startingRender) {
    // A chunk may be with the helper. A render can wait for it; the live callback must not, and at the end of a render nothing is in
    // flight (each offline block waited for its answer).
    if (startingRender) {
        const auto giveUpAt = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while ((m_pendingState.load(std::memory_order_acquire) == 1 || m_pendingState.load(std::memory_order_acquire) == 2 ||
                m_readyState.load(std::memory_order_acquire) == 1) &&
               !m_crashed.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < giveUpAt) {
            std::this_thread::yield();
        }
    }
    uint8_t readyFull = 2;
    m_readyState.compare_exchange_strong(readyFull, 0, std::memory_order_acq_rel); // a finished chunk of the other mode is dropped
    m_inFifo.reset(static_cast<size_t>(std::max<uint32_t>(1, m_maxBlockSize)) * 3, m_transportChannels);
    m_outFifo.reset(static_cast<size_t>(std::max<uint32_t>(1, m_maxBlockSize)) * 3, m_transportChannels);
    m_midiQueue.clear();
    m_inFifoEnd = 0;
    m_outStart = 0;
    // All Sound Off on every channel: notes still ringing from the other mode must not run into this one.
    for (uint8_t channel = 0; channel < 16; ++channel) {
        const uint8_t soundOff[3] = {static_cast<uint8_t>(0xB0 | channel), 120, 0};
        m_midiQueue.push(0, soundOff);
    }
}

} // namespace Audio
} // namespace Aestra
