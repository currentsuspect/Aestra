// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <vector>
#include <array>
#include <string_view>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace Nomad {
namespace Audio {

/**
 * @brief Validates audio files by extension and magic bytes
 * 
 * Prevents non-audio files from being imported into tracks,
 * which would cause harsh/distorted audio output.
 */
class AudioFileValidator {
public:
    /**
     * @brief Check if a file is a valid audio file
     * @param path File path to validate
     * @return true if file is a valid audio format
     */
    static bool isValidAudioFile(const std::string& path) {
        // First check extension (fast rejection)
        if (!hasValidAudioExtension(path)) {
            return false;
        }
        
        // Then validate file header (magic bytes)
        return validateFileHeader(path);
    }
    
    /**
     * @brief Get the audio file type from extension
     * @param path File path to check
     * @return Audio format name (e.g., "WAV", "MP3") or "Unknown"
     */
    static std::string getAudioFileType(const std::string& path) {
        std::string ext = getExtension(path);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                      [](unsigned char c){ return std::tolower(c); });
        
        if (ext == ".wav" || ext == ".wave") return "WAV";
        if (ext == ".mp3") return "MP3";
        if (ext == ".flac") return "FLAC";
        if (ext == ".ogg" || ext == ".oga") return "OGG";
        if (ext == ".aiff" || ext == ".aif" || ext == ".aifc") return "AIFF";
        if (ext == ".m4a" || ext == ".mp4" || ext == ".aac") return "AAC";
        if (ext == ".wma") return "WMA";
        if (ext == ".opus") return "OPUS";
        return "Unknown";
    }
    
    /**
     * @brief Check if file extension is a known audio format
     * @param path File path to check
     * @return true if extension is a valid audio extension
     */
    static bool hasValidAudioExtension(const std::string& path) {
        std::string ext = getExtension(path);
        
        // Convert to lowercase for comparison
        std::transform(ext.begin(), ext.end(), ext.begin(),
                      [](unsigned char c){ return std::tolower(c); });
        
        // List of supported audio extensions - compile-time container to avoid heap allocation
        static constexpr std::array<std::string_view, 14> validExtensions = {
            ".wav", ".wave",
            ".mp3",
            ".flac",
            ".ogg", ".oga",
            ".aiff", ".aif", ".aifc",
            ".m4a", ".mp4", ".aac",
            ".wma", ".opus"
        };
        
        // Convert extension to string_view for comparison
        std::string_view extView(ext);
        
        for (const auto& valid : validExtensions) {
            if (extView == valid) {
                return true;
            }
        }
        
        return false;
    }
    
    /**
     * @brief Validate file header (magic bytes) to confirm audio format
     * @param path File path to validate
     * @return true if file header matches a known audio format
     */
    static bool validateFileHeader(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        
        // Read first 16 bytes for header detection (need up to 16 for ASF GUID)
        uint8_t header[16] = {0};
        file.read(reinterpret_cast<char*>(header), sizeof(header));
        
        // Basic check - need at least 4 bytes for any header detection
        if (file.gcount() < 4) {
            return false; // File too small
        }
        
        // WAV: RIFF....WAVE (requires 12 bytes for header+8 check)
        if (file.gcount() >= 12 && memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0) {
            return true;
        }
        
        // MP3: ID3 tag or frame sync (requires 3-4 bytes)
        if (file.gcount() >= 3 && memcmp(header, "ID3", 3) == 0) {
            return true; // ID3v2 tag
        }
        if (file.gcount() >= 2 && header[0] == 0xFF && (header[1] & 0xE0) == 0xE0) {
            return true; // MP3 frame sync
        }
        
        // FLAC: fLaC (requires 4 bytes)
        if (file.gcount() >= 4 && memcmp(header, "fLaC", 4) == 0) {
            return true;
        }
        
        // OGG: OggS (requires 4 bytes)
        if (file.gcount() >= 4 && memcmp(header, "OggS", 4) == 0) {
            return true;
        }
        
        // AIFF: FORM....AIFF or FORM....AIFC (requires 12 bytes for header+8 check)
        if (file.gcount() >= 12 && memcmp(header, "FORM", 4) == 0) {
            if (memcmp(header + 8, "AIFF", 4) == 0 || memcmp(header + 8, "AIFC", 4) == 0) {
                return true;
            }
        }
        
        // M4A/MP4/AAC: ftyp (requires 8 bytes for header+4 check)
        if (file.gcount() >= 8 && memcmp(header + 4, "ftyp", 4) == 0) {
            return true;
        }
        
        // WMA/ASF: ASF header GUID (requires full 16 bytes)
        static const uint8_t asfGuid[16] = {
            0x30, 0x26, 0xB2, 0x75, 0x8E, 0x66, 0xCF, 0x11,
            0xA6, 0xD9, 0x00, 0xAA, 0x00, 0x62, 0xCE, 0x6C
        };
        if (file.gcount() >= 16 && memcmp(header, asfGuid, 16) == 0) { // Check full 16-byte GUID
            return true;
        }
        
        return false;
    }
    
    /**
     * @brief Get human-readable description of why file was rejected
     * @param path File path that was rejected
     * @return Error message describing the rejection reason
     */
    static std::string getRejectionReason(const std::string& path) {
        if (!hasValidAudioExtension(path)) {
            std::string ext = getExtension(path);
            if (ext.empty()) {
                return "File has no extension";
            }
            return "\"" + ext + "\" is not a supported audio format";
        }
        
        if (!validateFileHeader(path)) {
            return "File header does not match a valid audio format";
        }
        
        return "Unknown error";
    }
    
    /**
     * @brief Get list of supported audio extensions for display
     * @return Comma-separated list of extensions
     */
    static std::string getSupportedExtensions() {
        return "WAV, MP3, FLAC, OGG, AIFF, M4A, WMA, OPUS";
    }

private:
    static std::string getExtension(const std::string& path) {
        size_t dotPos = path.rfind('.');
        if (dotPos == std::string::npos) {
            return "";
        }
        return path.substr(dotPos);
    }
};

} // namespace Audio
} // namespace Nomad
