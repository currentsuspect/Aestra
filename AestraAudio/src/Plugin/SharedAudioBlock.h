// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// The audio of one block, shared between the app and the plugin helper (V82-H7 follow-up).
//
// Hex over the pipe cost two characters per byte each way (8 KB out and 8 KB back for 512 stereo frames), read one byte
// per syscall, and the block came back late often enough to play dry. Now the samples and the MIDI sit in one shared
// region; the pipe only carries a short PROCESSSHM line and an "OK" back. The pipe stays the doorbell on purpose: it
// keeps the timeouts, the crash detection and the order of commands exactly as they were, and its write/read pair is
// the memory barrier, so the region needs no atomics.
//
// The app makes the region and owns the sizes; the helper only maps it. Nothing the helper writes into it is trusted:
// the app reads back only its own sample count. Layout: header | input floats | output floats | MIDI bytes.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Aestra {
namespace Audio {

class SharedAudioBlock {
public:
    static constexpr uint32_t kMagic = 0x41455348; // "AESH"
    static constexpr uint32_t kVersion = 1;

    SharedAudioBlock() = default;
    ~SharedAudioBlock() { close(); }
    SharedAudioBlock(const SharedAudioBlock&) = delete;
    SharedAudioBlock& operator=(const SharedAudioBlock&) = delete;

    static size_t bytesFor(size_t maxSamples, size_t maxMidiBytes) {
        return kHeaderBytes + (2 * maxSamples * sizeof(float)) + maxMidiBytes;
    }

    /// App side: make a fresh region nobody else has. False leaves the app on the hex path.
    bool create(const std::string& name, size_t maxSamples, size_t maxMidiBytes) {
        close();
        const size_t bytes = bytesFor(maxSamples, maxMidiBytes);
#ifdef _WIN32
        m_mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, static_cast<DWORD>(bytes),
                                       name.c_str());
        if (!m_mapping || GetLastError() == ERROR_ALREADY_EXISTS) {
            close();
            return false;
        }
#else
        const int fd = shm_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0)
            return false;
        m_name = name;
        if (ftruncate(fd, static_cast<off_t>(bytes)) != 0) {
            ::close(fd);
            close();
            return false;
        }
        m_fd = fd;
#endif
        if (!map(bytes)) {
            close();
            return false;
        }
        auto* h = header();
        h->magic = kMagic;
        h->version = kVersion;
        h->maxSamples = static_cast<uint32_t>(maxSamples);
        h->maxMidiBytes = static_cast<uint32_t>(maxMidiBytes);
        setSizes(maxSamples, maxMidiBytes);
        return true;
    }

    /// Helper side: map the app's region. The sizes come from the app's command and must match the header.
    bool open(const std::string& name, size_t maxSamples, size_t maxMidiBytes) {
        close();
        const size_t bytes = bytesFor(maxSamples, maxMidiBytes);
#ifdef _WIN32
        m_mapping = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, name.c_str());
        if (!m_mapping)
            return false;
#else
        const int fd = shm_open(name.c_str(), O_RDWR, 0600);
        if (fd < 0)
            return false;
        struct stat st {};
        if (fstat(fd, &st) != 0 || static_cast<size_t>(st.st_size) < bytes) {
            ::close(fd);
            return false;
        }
        m_fd = fd;
#endif
        if (!map(bytes)) {
            close();
            return false;
        }
        const auto* h = header();
        if (h->magic != kMagic || h->version != kVersion || h->maxSamples != maxSamples ||
            h->maxMidiBytes != maxMidiBytes) {
            close();
            return false;
        }
        setSizes(maxSamples, maxMidiBytes);
        return true;
    }

    /// App side, once the helper has it mapped: take the name away so nothing else can open it, and so it cannot
    /// outlive both processes. The mappings stay valid.
    void unlinkName() {
#ifndef _WIN32
        if (!m_name.empty()) {
            shm_unlink(m_name.c_str());
            m_name.clear();
        }
#endif
    }

    void close() {
        unlinkName();
#ifdef _WIN32
        if (m_base)
            UnmapViewOfFile(m_base);
        if (m_mapping)
            CloseHandle(m_mapping);
        m_mapping = nullptr;
#else
        if (m_base)
            munmap(m_base, m_bytes);
        if (m_fd >= 0)
            ::close(m_fd);
        m_fd = -1;
#endif
        m_base = nullptr;
        m_bytes = 0;
        m_maxSamples = 0;
        m_maxMidiBytes = 0;
    }

    bool isOpen() const { return m_base != nullptr; }
    size_t maxSamples() const { return m_maxSamples; }
    size_t maxMidiBytes() const { return m_maxMidiBytes; }
    float* input() { return reinterpret_cast<float*>(bytes() + kHeaderBytes); }
    float* output() { return input() + m_maxSamples; }
    uint8_t* midi() { return reinterpret_cast<uint8_t*>(output() + m_maxSamples); }

    /// A name no other region has: the process id, a counter and the time, so two instances or a stale region from
    /// a crashed run never collide (create() refuses an existing one either way).
    static std::string uniqueName(unsigned long pid, unsigned long counter, unsigned long long stamp) {
        char buf[96];
#ifdef _WIN32
        std::snprintf(buf, sizeof buf, "Local\\aestra-ph-%lu-%lu-%llx", pid, counter, stamp);
#else
        std::snprintf(buf, sizeof buf, "/aestra-ph-%lu-%lu-%llx", pid, counter, stamp);
#endif
        return buf;
    }

private:
    struct Header {
        uint32_t magic;
        uint32_t version;
        uint32_t maxSamples;
        uint32_t maxMidiBytes;
    };
    static constexpr size_t kHeaderBytes = 64; // keeps the samples cache-line aligned

    bool map(size_t bytes) {
#ifdef _WIN32
        m_base = MapViewOfFile(m_mapping, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
#else
        void* p = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, m_fd, 0);
        m_base = p == MAP_FAILED ? nullptr : p;
        // The mapping outlives the descriptor, and a descriptor left open in the helper is one more handle untrusted
        // plugin code could reach (the sandbox test checks the helper holds only 0, 1 and 2).
        ::close(m_fd);
        m_fd = -1;
#endif
        m_bytes = m_base ? bytes : 0;
        return m_base != nullptr;
    }
    void setSizes(size_t maxSamples, size_t maxMidiBytes) {
        m_maxSamples = maxSamples;
        m_maxMidiBytes = maxMidiBytes;
    }
    Header* header() { return reinterpret_cast<Header*>(m_base); }
    uint8_t* bytes() { return static_cast<uint8_t*>(m_base); }

    void* m_base = nullptr;
    size_t m_bytes = 0;
    size_t m_maxSamples = 0;
    size_t m_maxMidiBytes = 0;
#ifdef _WIN32
    HANDLE m_mapping = nullptr;
#else
    int m_fd = -1;
    std::string m_name;
#endif
};

} // namespace Audio
} // namespace Aestra
