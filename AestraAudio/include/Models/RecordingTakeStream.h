// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace Aestra::Audio {

/**
 * @brief Carries one recorded take from the audio thread's capture ring to disk
 *        while it is still being recorded ("never lose a session").
 *
 * The audio thread writes frame f of a take into ring[f % capacity] and then
 * publishes the running frame count. drain() runs off the audio thread: it
 * copies every frame published since the last drain into memory and appends it
 * to a `.partial` WAV beside where the finished take will go. The WAV header is
 * rewritten on every drain and the file is synced about once a second, so after
 * a crash the partial file is a playable take up to the last drain.
 *
 * The ring only has to cover the time between drains, so the length of a take
 * is limited by memory and disk, not by the ring.
 *
 * If a drain comes too late and the audio thread has already overwritten
 * frames, those frames are written as silence and counted in lostFrames(), so
 * everything after them stays at its true position on the timeline.
 *
 * Not thread-safe: one thread drains at a time (TrackManager holds its
 * recording mutex around every call).
 */
class RecordingTakeStream {
public:
    /** @param partialPath where the in-progress take is written (may be empty: memory only). */
    RecordingTakeStream(std::string partialPath, uint32_t sampleRate);
    ~RecordingTakeStream();

    RecordingTakeStream(const RecordingTakeStream&) = delete;
    RecordingTakeStream& operator=(const RecordingTakeStream&) = delete;

    /**
     * @brief Copy every frame published since the last drain.
     * @param ring the capture ring (frame f lives at f % capacity).
     * @param capacity ring length in frames.
     * @param published running count of frames the audio thread has written,
     *        read with acquire after the samples were stored.
     */
    void drain(const std::atomic<float>* ring, size_t capacity, const std::atomic<uint64_t>& published);

    /** @brief Every frame of the take so far, in order, silence where frames were lost. */
    const std::vector<float>& samples() const { return m_samples; }
    /** @brief Frames the audio thread overwrote before they could be drained. */
    uint64_t lostFrames() const { return m_lostFrames; }
    /** @brief The in-progress file, or empty when the take is memory-only. */
    const std::string& partialPath() const { return m_partialPath; }
    /** @brief True while the partial file is open and every write so far has succeeded. */
    bool partialFileHealthy() const { return m_file.is_open() && m_fileHealthy; }

    /** @brief Close and delete the partial file. Call once the finished take is safely written. */
    void discardPartialFile();

    /** @brief The extension every in-progress take carries. Cleanup never deletes these. */
    static constexpr const char* kPartialExtension = ".partial";

    /**
     * @brief Read the frame count a partial take's header claims (header of a
     *        mono float WAV as written here). For recovery and tests.
     * @return -1 when the file is not a partial take this class wrote.
     */
    static int64_t readPartialFrameCount(const std::string& path);

private:
    void openFile();
    void appendToFile(const float* data, size_t count);
    void rewriteHeader();

    std::string m_partialPath;
    uint32_t m_sampleRate{0};
    std::ofstream m_file;
    bool m_fileHealthy{false};
    std::vector<float> m_samples;
    uint64_t m_drainedFrames{0};
    uint64_t m_lostFrames{0};
    std::chrono::steady_clock::time_point m_lastSync;
};

} // namespace Aestra::Audio
