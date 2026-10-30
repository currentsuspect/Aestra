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

#include <algorithm>
#include <array>
#include <cstring>
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
    if (m_inFifo.frames() >= chunk) {
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
        } else {
            m_unsentBlocks.fetch_add(1, std::memory_order_relaxed);
        }
    }
    if (queued < numFrames) {
        m_unsentBlocks.fetch_add(1, std::memory_order_relaxed); // the helper fell a whole FIFO behind
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

} // namespace Audio
} // namespace Aestra
