// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Metadata extracted from an audio file
 */
struct AudioMetadata {
    std::string title;
    std::string artist;
    std::string album;
    double durationSeconds = 0.0;

    // Cover Art
    std::vector<uint8_t> coverArtData;
    std::string coverArtMimeType; // "image/jpeg", "image/png"

    bool hasCoverArt() const { return !coverArtData.empty(); }
};

/**
 * @brief Native metadata parser for MP3 (ID3v2) and FLAC files
 *
 * Zero external dependencies. Handles:
 * - ID3v2.3, ID3v2.4 tags
 * - Synchsafe integers
 * - UTF-8 and UTF-16 encoding
 * - APIC (Attached Picture) frames
 * - FLAC Vorbis Comments
 * - FLAC Picture blocks
 */
class MetadataParser {
public:
    /**
     * @brief Extract metadata from an audio file
     * @param filePath Path to the audio file
     * @return Populated AudioMetadata struct (best effort, fields may be empty)
     */
    static AudioMetadata parse(const std::string& filePath);

private:
    // ========== ID3v2 (MP3) ==========
    static bool parseID3v2(const std::string& filePath, AudioMetadata& meta);
    static uint32_t readSynchsafeInt(const uint8_t* data);
    static uint32_t readBigEndian32(const uint8_t* data);
    static uint32_t readBigEndian24(const uint8_t* data);
    static std::string decodeID3String(const uint8_t* data, size_t size, uint8_t encoding);

    // ========== FLAC ==========
    static bool parseFLAC(const std::string& filePath, AudioMetadata& meta);
    static std::string readVorbisComment(const uint8_t* data, size_t size, const std::string& key);

    // ========== WAV (Optional) ==========
    static bool parseWAV(const std::string& filePath, AudioMetadata& meta);

    // ========== Utilities ==========
    static std::string getFileExtension(const std::string& path);
    static std::string toLowerCase(const std::string& s);
};

} // namespace Audio
} // namespace Aestra
