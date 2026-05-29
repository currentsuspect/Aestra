// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace Aestra {

// Sync a file path to disk. Use after writing critical data (project save,
// autosave, export) to ensure data survives a crash.
inline bool fsyncPath(const std::string& path) {
#ifdef _WIN32
    const auto nativePath = std::filesystem::path(path).wstring();
    HANDLE h = CreateFileW(nativePath.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    bool ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    return ok;
#else
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0)
        return false;
    bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
#endif
}

inline bool fsyncParentDirectory(const std::string& path) {
    namespace fs = std::filesystem;
    fs::path parent = fs::path(path).parent_path();
    if (parent.empty())
        parent = ".";

#ifdef _WIN32
    const auto nativePath = parent.wstring();
    HANDLE h = CreateFileW(nativePath.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE)
        return false;
    bool ok = FlushFileBuffers(h) != 0;
    CloseHandle(h);
    return ok;
#else
    int fd = open(parent.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        return false;
    bool ok = fsync(fd) == 0;
    close(fd);
    return ok;
#endif
}

// Sync an ofstream flush+fsync. Call after writing and before rename.
inline bool syncOfstream(std::ofstream& stream, const std::string& path) {
    stream.flush();
    if (!stream)
        return false;
    return fsyncPath(path);
}

// =============================================================================
// File Abstraction Layer
// =============================================================================
class File {
public:
    enum class Mode { Read, Write, Append, ReadWrite };

    File() : isOpen_(false) {}
    ~File() { close(); }

    // Open file
    bool open(const std::string& path, Mode mode) {
        close();
        path_ = path;

        std::ios::openmode iosMode = std::ios::binary;
        switch (mode) {
        case Mode::Read:
            iosMode |= std::ios::in;
            break;
        case Mode::Write:
            iosMode |= std::ios::out | std::ios::trunc;
            break;
        case Mode::Append:
            iosMode |= std::ios::out | std::ios::app;
            break;
        case Mode::ReadWrite:
            iosMode |= std::ios::in | std::ios::out;
            break;
        }

        stream_.open(path, iosMode);
        isOpen_ = stream_.is_open();
        return isOpen_;
    }

    // Close file
    void close() {
        if (isOpen_) {
            stream_.close();
            isOpen_ = false;
        }
    }

    // Check if file is open
    bool isOpen() const { return isOpen_; }

    // Read data
    bool read(void* buffer, size_t size) {
        if (!isOpen_)
            return false;
        stream_.read(static_cast<char*>(buffer), size);
        return stream_.good();
    }

    // Write data
    bool write(const void* buffer, size_t size) {
        if (!isOpen_)
            return false;
        stream_.write(static_cast<const char*>(buffer), size);
        return stream_.good();
    }

    // Sync file data to disk (fsync on POSIX, FlushFileBuffers on Windows)
    bool sync() {
        if (!isOpen_)
            return false;
        stream_.flush();
        if (!stream_)
            return false;
        return Aestra::fsyncPath(path_);
    }

    // Get file size
    size_t size() {
        if (!isOpen_)
            return 0;
        auto pos = stream_.tellg();
        if (pos == std::streampos(-1))
            return 0;
        stream_.seekg(0, std::ios::end);
        auto endPos = stream_.tellg();
        stream_.seekg(pos);
        if (endPos == std::streampos(-1))
            return 0;
        return static_cast<size_t>(endPos);
    }

    // Seek to position
    bool seek(size_t position) {
        if (!isOpen_)
            return false;
        stream_.seekg(position);
        return stream_.good();
    }

    // Get current position
    size_t tell() {
        if (!isOpen_)
            return 0;
        return stream_.tellg();
    }

    // Read entire file as string
    static std::string readAllText(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open())
            return "";

        file.seekg(0, std::ios::end);
        size_t size = file.tellg();
        file.seekg(0);

        std::string content(size, '\0');
        file.read(&content[0], size);
        return content;
    }

    // Write entire string to file
    static bool writeAllText(const std::string& path, const std::string& content) {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
            return false;
        file.write(content.c_str(), content.size());
        return file.good();
    }

    // Check if file exists
    static bool exists(const std::string& path) {
        std::ifstream file(path);
        return file.good();
    }

private:
    std::fstream stream_;
    std::string path_;
    bool isOpen_;
};

// =============================================================================
// Binary Serialization
// =============================================================================
class BinaryWriter {
public:
    BinaryWriter() {}

    // Write primitive types
    void write(int8_t value) { data_.push_back(static_cast<uint8_t>(value)); }
    void write(uint8_t value) { data_.push_back(value); }

    void write(int16_t value) {
        data_.push_back(static_cast<uint8_t>(value & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void write(uint16_t value) {
        data_.push_back(static_cast<uint8_t>(value & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
    }

    void write(int32_t value) {
        data_.push_back(static_cast<uint8_t>(value & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void write(uint32_t value) {
        data_.push_back(static_cast<uint8_t>(value & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 8) & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 16) & 0xFF));
        data_.push_back(static_cast<uint8_t>((value >> 24) & 0xFF));
    }

    void write(float value) {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(float));
        write(bits);
    }

    void write(double value) {
        uint64_t bits;
        std::memcpy(&bits, &value, sizeof(double));
        for (int i = 0; i < 8; ++i) {
            data_.push_back(static_cast<uint8_t>((bits >> (i * 8)) & 0xFF));
        }
    }

    void write(const std::string& value) {
        write(static_cast<uint32_t>(value.size()));
        for (char c : value) {
            data_.push_back(static_cast<uint8_t>(c));
        }
    }

    // Get serialized data
    const std::vector<uint8_t>& data() const { return data_; }

    // Write to file
    bool writeToFile(const std::string& path) {
        File file;
        if (!file.open(path, File::Mode::Write))
            return false;
        return file.write(data_.data(), data_.size());
    }

private:
    std::vector<uint8_t> data_;
};

class BinaryReader {
public:
    BinaryReader(const std::vector<uint8_t>& data) : dataRef_(&data), position_(0) {}
    BinaryReader(std::vector<uint8_t>&& data) : ownedData_(std::move(data)), dataRef_(&ownedData_), position_(0) {}

    // Read primitive types
    bool read(int8_t& value) {
        const auto& data = *dataRef_;
        if (position_ >= data.size())
            return false;
        value = static_cast<int8_t>(data[position_++]);
        return true;
    }

    bool read(uint8_t& value) {
        const auto& data = *dataRef_;
        if (position_ >= data.size())
            return false;
        value = data[position_++];
        return true;
    }

    bool read(int16_t& value) {
        const auto& data = *dataRef_;
        if (position_ + 2 > data.size())
            return false;
        value = static_cast<int16_t>(data[position_] | (data[position_ + 1] << 8));
        position_ += 2;
        return true;
    }

    bool read(uint16_t& value) {
        const auto& data = *dataRef_;
        if (position_ + 2 > data.size())
            return false;
        value = static_cast<uint16_t>(data[position_] | (data[position_ + 1] << 8));
        position_ += 2;
        return true;
    }

    bool read(int32_t& value) {
        const auto& data = *dataRef_;
        if (position_ + 4 > data.size())
            return false;
        value = static_cast<int32_t>(data[position_] | (data[position_ + 1] << 8) | (data[position_ + 2] << 16) |
                                     (data[position_ + 3] << 24));
        position_ += 4;
        return true;
    }

    bool read(uint32_t& value) {
        const auto& data = *dataRef_;
        if (position_ + 4 > data.size())
            return false;
        value = static_cast<uint32_t>(data[position_] | (data[position_ + 1] << 8) | (data[position_ + 2] << 16) |
                                      (data[position_ + 3] << 24));
        position_ += 4;
        return true;
    }

    bool read(float& value) {
        uint32_t bits;
        if (!read(bits))
            return false;
        std::memcpy(&value, &bits, sizeof(float));
        return true;
    }

    bool read(double& value) {
        const auto& data = *dataRef_;
        if (position_ + 8 > data.size())
            return false;
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i) {
            bits |= static_cast<uint64_t>(data[position_ + i]) << (i * 8);
        }
        position_ += 8;
        std::memcpy(&value, &bits, sizeof(double));
        return true;
    }

    bool read(std::string& value) {
        const auto& data = *dataRef_;
        uint32_t size;
        if (!read(size))
            return false;
        if (size > 64 * 1024 * 1024)
            return false;
        if (position_ + size > data.size())
            return false;
        value.resize(size);
        for (uint32_t i = 0; i < size; ++i) {
            value[i] = static_cast<char>(data[position_++]);
        }
        return true;
    }

    // Read from file
    static std::unique_ptr<BinaryReader> readFromFile(const std::string& path) {
        File file;
        if (!file.open(path, File::Mode::Read))
            return nullptr;

        size_t fileSize = file.size();
        std::vector<uint8_t> data(fileSize);
        if (!file.read(data.data(), fileSize))
            return nullptr;

        return std::make_unique<BinaryReader>(std::move(data));
    }

private:
    std::vector<uint8_t> ownedData_;
    const std::vector<uint8_t>* dataRef_;
    size_t position_;
};

} // namespace Aestra
