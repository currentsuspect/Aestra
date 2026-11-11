// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "Models/RecordingTakeStream.h"

#include "AestraFile.h"
#include "AestraLog.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

namespace Aestra::Audio {

namespace {

// Mono 32-bit float WAV with the plain 44-byte header: the data chunk starts at
// a fixed offset, so the two size fields can be rewritten in place each drain.
constexpr size_t kHeaderBytes = 44;
constexpr std::streamoff kRiffSizeOffset = 4;
constexpr std::streamoff kDataSizeOffset = 40;
constexpr uint16_t kFormatIeeeFloat = 3;
constexpr uint16_t kChannels = 1;
constexpr uint16_t kBitsPerSample = 32;
constexpr uint16_t kBlockAlign = kChannels * (kBitsPerSample / 8);
// A WAV's size fields are 32-bit; past this the take stays in memory only.
constexpr uint64_t kMaxDataBytes = 0xFFFFFFFFull - kHeaderBytes;
constexpr auto kSyncInterval = std::chrono::seconds(1);

void putU16(std::array<char, kHeaderBytes>& h, size_t at, uint16_t v) {
    std::memcpy(h.data() + at, &v, 2);
}
void putU32(std::array<char, kHeaderBytes>& h, size_t at, uint32_t v) {
    std::memcpy(h.data() + at, &v, 4);
}

std::array<char, kHeaderBytes> makeHeader(uint32_t sampleRate, uint32_t dataBytes) {
    std::array<char, kHeaderBytes> h{};
    std::memcpy(h.data(), "RIFF", 4);
    putU32(h, 4, static_cast<uint32_t>(36u + dataBytes));
    std::memcpy(h.data() + 8, "WAVEfmt ", 8);
    putU32(h, 16, 16);
    putU16(h, 20, kFormatIeeeFloat);
    putU16(h, 22, kChannels);
    putU32(h, 24, sampleRate);
    putU32(h, 28, sampleRate * kBlockAlign);
    putU16(h, 32, kBlockAlign);
    putU16(h, 34, kBitsPerSample);
    std::memcpy(h.data() + 36, "data", 4);
    putU32(h, 40, dataBytes);
    return h;
}

} // namespace

RecordingTakeStream::RecordingTakeStream(std::string partialPath, uint32_t sampleRate)
    : m_partialPath(std::move(partialPath)), m_sampleRate(std::max<uint32_t>(1, sampleRate)) {
    openFile();
}

RecordingTakeStream::~RecordingTakeStream() {
    if (m_file.is_open()) {
        // Left on disk on purpose: a take that was never committed is exactly
        // what recovery is for. discardPartialFile() is the only delete.
        m_file.flush();
        m_file.close();
    }
}

void RecordingTakeStream::openFile() {
    if (m_partialPath.empty()) {
        return;
    }
    std::error_code ec;
    const std::filesystem::path path(m_partialPath);
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    m_file.open(m_partialPath, std::ios::binary | std::ios::trunc);
    if (!m_file.is_open()) {
        Log::warning("[Recording] Cannot open in-progress take file; this take is held in memory only: " +
                     m_partialPath);
        return;
    }
    const auto header = makeHeader(m_sampleRate, 0);
    m_file.write(header.data(), static_cast<std::streamsize>(header.size()));
    m_fileHealthy = static_cast<bool>(m_file);
    m_lastSync = std::chrono::steady_clock::now();
}

void RecordingTakeStream::drain(const std::atomic<float>* ring, size_t capacity,
                                const std::atomic<uint64_t>& published) {
    if (!ring || capacity == 0) {
        return;
    }
    const uint64_t end = published.load(std::memory_order_acquire);
    if (end <= m_drainedFrames) {
        return;
    }

    const size_t before = m_samples.size();
    uint64_t from = m_drainedFrames;
    // Frames older than one ring length have already been overwritten.
    if (end - from > capacity) {
        const uint64_t lost = (end - capacity) - from;
        m_samples.resize(m_samples.size() + lost, 0.0f);
        m_lostFrames += lost;
        from += lost;
    }
    for (uint64_t f = from; f < end; ++f) {
        m_samples.push_back(ring[f % capacity].load(std::memory_order_relaxed));
    }

    // The audio thread kept writing while we copied. Anything it has wrapped
    // past since may have been torn mid-copy: silence it rather than keep a
    // mix of two moments.
    const uint64_t after = published.load(std::memory_order_acquire);
    if (after > capacity && after - capacity > from) {
        const uint64_t tornEnd = std::min(end, after - capacity);
        const size_t first = before + static_cast<size_t>(from - m_drainedFrames);
        const size_t count = static_cast<size_t>(tornEnd - from);
        std::fill_n(m_samples.begin() + static_cast<std::ptrdiff_t>(first), count, 0.0f);
        m_lostFrames += count;
    }
    m_drainedFrames = end;
    appendToFile(m_samples.data() + before, m_samples.size() - before);
}

void RecordingTakeStream::appendToFile(const float* data, size_t count) {
    if (!partialFileHealthy() || count == 0) {
        return;
    }
    if (static_cast<uint64_t>(m_samples.size()) * sizeof(float) > kMaxDataBytes) {
        Log::warning("[Recording] Take passed the WAV size limit; the rest stays in memory only: " + m_partialPath);
        m_fileHealthy = false;
        return;
    }
    m_file.seekp(0, std::ios::end);
    m_file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(count * sizeof(float)));
    rewriteHeader();
    m_file.flush();
    if (!m_file) {
        Log::warning("[Recording] Writing the in-progress take failed; it is held in memory only: " + m_partialPath);
        m_fileHealthy = false;
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastSync >= kSyncInterval) {
        if (!Aestra::syncOfstream(m_file, m_partialPath)) {
            Log::warning("[Recording] Syncing the in-progress take failed: " + m_partialPath);
        }
        m_lastSync = now;
    }
}

void RecordingTakeStream::rewriteHeader() {
    const auto dataBytes = static_cast<uint32_t>(m_samples.size() * sizeof(float));
    const uint32_t riffBytes = 36u + dataBytes;
    m_file.seekp(kRiffSizeOffset);
    m_file.write(reinterpret_cast<const char*>(&riffBytes), sizeof(riffBytes));
    m_file.seekp(kDataSizeOffset);
    m_file.write(reinterpret_cast<const char*>(&dataBytes), sizeof(dataBytes));
    m_file.seekp(0, std::ios::end);
}

void RecordingTakeStream::discardPartialFile() {
    if (m_file.is_open()) {
        m_file.close();
    }
    m_fileHealthy = false;
    if (!m_partialPath.empty()) {
        std::error_code ec;
        std::filesystem::remove(m_partialPath, ec);
    }
}

int64_t RecordingTakeStream::readPartialFrameCount(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::array<char, kHeaderBytes> h{};
    if (!in.read(h.data(), static_cast<std::streamsize>(h.size()))) {
        return -1;
    }
    if (std::memcmp(h.data(), "RIFF", 4) != 0 || std::memcmp(h.data() + 8, "WAVEfmt ", 8) != 0 ||
        std::memcmp(h.data() + 36, "data", 4) != 0) {
        return -1;
    }
    uint32_t dataBytes = 0;
    std::memcpy(&dataBytes, h.data() + 40, 4);
    return static_cast<int64_t>(dataBytes / kBlockAlign);
}

} // namespace Aestra::Audio
