// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// The audio-thread side of the sandbox hand-over.
//
// The helper process runs the plugin one chunk behind the engine: a chunk goes over in
// one callback and its result plays in the next. Two things make that delay constant:
//   - the helper always gets chunks of one size (the callback size, learned here), cut
//     from an input FIFO, so a callback the engine splits (a loop wrap) or the first
//     callback after an insert no longer drops or truncates audio;
//   - the output plays from a FIFO primed with one chunk of silence (added as the chunk
//     size is learned), so it runs one chunk behind whatever sizes the callbacks come in.
// getLatencySamples() reports that chunk, which is what PDC must compensate.
#include "OutOfProcessPluginInstance.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace Aestra {
namespace Audio {

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

    // The chunk grows to the largest callback seen; the output FIFO takes the extra delay as
    // silence, so it stays exactly one chunk behind (and PDC hears about the new size).
    const uint32_t previousChunk = m_chunkFrames.load(std::memory_order_relaxed);
    if (numFrames > previousChunk) {
        m_outFifo.pushSilence(numFrames - previousChunk);
        m_chunkFrames.store(numFrames, std::memory_order_release);
    }
    const uint32_t chunk = std::max(previousChunk, numFrames);

    // 1. A finished chunk joins the output.
    uint8_t readyFull = 2;
    if (m_readyState.compare_exchange_strong(readyFull, 3, std::memory_order_acq_rel)) {
        const uint32_t frames = std::min(m_readyFrames.load(std::memory_order_acquire), m_maxBlockSize);
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

    // 3. One chunk goes to the helper when its slot is free.
    if (m_inFifo.frames() >= chunk) {
        uint8_t pendingEmpty = 0;
        if (m_pendingState.compare_exchange_strong(pendingEmpty, 1, std::memory_order_acq_rel)) {
            const uint64_t chunkStart = m_inFifoEnd - m_inFifo.frames();
            m_inFifo.popInterleaved(m_pendingInput.data(), chunk);
            const size_t midiBytes = m_midiQueue.takeChunk(chunkStart, chunkStart + chunk, m_pendingMidiData.data(),
                                                           m_pendingMidiData.size());
            m_pendingTransport = HostTransportClock::current();
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

    // 4. Play one chunk behind. The helper missing its deadline leaves the FIFO short: play
    //    the input dry for this callback, as before.
    if (m_outFifo.frames() >= numFrames) {
        m_outFifo.popPlanar(outputs, numOutputChannels, numFrames); // channels past the transport's are silenced
        return;
    }
    m_dryBlocks.fetch_add(1, std::memory_order_relaxed);
    passThrough(inputs, outputs, numInputChannels, numOutputChannels, numFrames);
}

} // namespace Audio
} // namespace Aestra
