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
        const bool instrument = m_info.type == PluginType::Instrument; // renders with no hand-off delay: any slice size will do
        const uint32_t slice = (!instrument && learned > 0) ? std::min(learned, m_maxBlockSize) : m_maxBlockSize;
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

    // In a render an instrument has no hand-off delay: each block waits for its own answer, and delay compensation covers insert
    // effects only, so an instrument's chunk of delay was in every bounce and nothing took it out (a blank start, a pop when
    // looped). Its chunk is this call's block and its output is read at the same position. An effect keeps the one-chunk delay in
    // a render too, because delay compensation lines the other tracks up against the latency it reports.
    const bool renderOnGrid = OfflineRenderMode::active() && m_info.type == PluginType::Instrument;

    // The chunk grows to the largest callback seen (and PDC hears about the new size). Not from a render on the grid: that
    // chunk is the render's, and live latency must stay what live playback set.
    const uint32_t previousChunk = m_chunkFrames.load(std::memory_order_relaxed);
    if (!renderOnGrid && numFrames > previousChunk) {
        m_chunkFrames.store(numFrames, std::memory_order_release);
    }
    const uint32_t chunk = renderOnGrid ? numFrames : std::max(previousChunk, numFrames);

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

    // 3. What has come in goes to the helper when its slot is free, at most one chunk of it: the newest. More than a chunk
    //    waiting means the helper fell behind and the older frames' time has passed (they played dry). Their MIDI is kept and
    //    lands at the start of what does go. Everything is sent, not whole chunks only: callbacks need not all be the chunk's
    //    size (PipeWire resampling 48 kHz to 44.1 kHz delivers 411 and 412 frames in turn), and fixed chunks then drift against
    //    the callbacks, so every callback wanted frames that had not gone yet: a short silence in every block, a pulsing crackle.
    //    Sent this way, each callback's frames come back by the next one, which the one-chunk delay (the largest callback) covers.
    if (chunk > 0 && m_inFifo.frames() > 0) { // nothing before the first frames (a 0-frame callback)
        // The worker holds the slot (3) only while it copies the chunk out: a bounded copy of at most one block. The second part
        // of a split callback arrives right then often enough to be heard (the first part's post is what woke the worker), so
        // wait out that copy, for a hard-capped few microseconds; past the cap it is treated as busy, as before.
        for (int spin = 0; spin < 512 && m_pendingState.load(std::memory_order_acquire) == 3; ++spin) {
#if defined(__x86_64__) || defined(__i386__)
            __builtin_ia32_pause();
#endif
        }
        uint8_t pendingEmpty = 0;
        if (m_pendingState.compare_exchange_strong(pendingEmpty, 1, std::memory_order_acq_rel)) {
            if (m_inFifo.frames() > chunk) {
                m_inFifo.discard(m_inFifo.frames() - chunk);
            }
            const auto sendFrames = static_cast<uint32_t>(m_inFifo.frames());
            const uint64_t chunkStart = m_inFifoEnd - sendFrames;
            m_inFifo.popInterleaved(m_pendingInput.data(), sendFrames);
            const size_t midiBytes = m_midiQueue.takeChunk(chunkStart, chunkStart + sendFrames, m_pendingMidiData.data(),
                                                           m_pendingMidiData.size());
            m_pendingTransport = HostTransportClock::current();
            m_pendingStart = chunkStart;
            m_pendingMidiBytes.store(midiBytes, std::memory_order_release);
            m_pendingFrames.store(sendFrames, std::memory_order_release);
            m_pendingState.store(2, std::memory_order_release);
            wakeWorker();
            sentChunk = true;
        } else if (appendToPendingChunk()) {
            // A callback split in two (a loop wrap renders up to the loop end, then from the loop start): the second part
            // arrives before the helper has picked up the first, and joins it, so both come back by the next callback.
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
    const int64_t want = static_cast<int64_t>(callbackStart) - static_cast<int64_t>(renderOnGrid ? 0 : chunk);
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

bool OutOfProcessPluginInstance::appendToPendingChunk() {
    // Only while the helper has not picked the chunk up (2 -> 1 claims it back; the worker's 2 -> 3 take loses or wins the race),
    // only when the new frames follow it exactly, and only while it fits the block size the plugin was prepared for.
    uint8_t posted = 2;
    if (!m_pendingState.compare_exchange_strong(posted, 1, std::memory_order_acq_rel)) {
        return false;
    }
    const uint32_t have = m_pendingFrames.load(std::memory_order_acquire);
    const auto add = static_cast<uint32_t>(m_inFifo.frames());
    const uint64_t newStart = m_inFifoEnd - add;
    if (add == 0 || newStart != m_pendingStart + have || have + add > m_maxBlockSize) {
        m_pendingState.store(2, std::memory_order_release); // left as it was
        return false;
    }
    m_inFifo.popInterleaved(m_pendingInput.data() + static_cast<size_t>(have) * m_transportChannels, add);
    const size_t haveMidi = m_pendingMidiBytes.load(std::memory_order_acquire);
    // Offsets stay relative to the chunk's start; the events taken are only the new frames' (earlier ones already went).
    const size_t moreMidi = m_midiQueue.takeChunk(m_pendingStart, newStart + add, m_pendingMidiData.data() + haveMidi,
                                                  m_pendingMidiData.size() - haveMidi);
    m_pendingMidiBytes.store(haveMidi + moreMidi, std::memory_order_release);
    m_pendingFrames.store(have + add, std::memory_order_release);
    m_pendingState.store(2, std::memory_order_release);
    wakeWorker();
    return true;
}

} // namespace Audio
} // namespace Aestra
