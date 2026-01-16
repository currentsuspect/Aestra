// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "MetadataParser.h"
#include "AestraLog.h"

#include <fstream>
#include <algorithm>
#include <cstring>

namespace Aestra {
namespace Audio {

// ============================================================================
// PUBLIC API
// ============================================================================

AudioMetadata MetadataParser::parse(const std::string& filePath) {
    AudioMetadata meta;
    
    std::string ext = toLowerCase(getFileExtension(filePath));
    
    if (ext == "mp3") {
        parseID3v2(filePath, meta);
    } else if (ext == "flac") {
        parseFLAC(filePath, meta);
    } else if (ext == "wav") {
        parseWAV(filePath, meta);
    }
    
    return meta;
}

// ============================================================================
// UTILITIES
// ============================================================================

std::string MetadataParser::getFileExtension(const std::string& path) {
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return "";
    return path.substr(dot + 1);
}

std::string MetadataParser::toLowerCase(const std::string& s) {
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), ::tolower);
    return result;
}

uint32_t MetadataParser::readSynchsafeInt(const uint8_t* data) {
    // ID3v2 synchsafe: 7 bits per byte, MSB is always 0
    return ((data[0] & 0x7F) << 21) |
           ((data[1] & 0x7F) << 14) |
           ((data[2] & 0x7F) << 7) |
           (data[3] & 0x7F);
}

uint32_t MetadataParser::readBigEndian32(const uint8_t* data) {
    return (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
}

uint32_t MetadataParser::readBigEndian24(const uint8_t* data) {
    return (data[0] << 16) | (data[1] << 8) | data[2];
}

std::string MetadataParser::decodeID3String(const uint8_t* data, size_t size, uint8_t encoding) {
    if (size == 0) return "";
    
    // Encoding: 0=ISO-8859-1, 1=UTF-16 BOM, 2=UTF-16BE, 3=UTF-8
    if (encoding == 0 || encoding == 3) {
        // ASCII-compatible: Just copy, skip null terminators
        size_t len = size;
        while (len > 0 && data[len - 1] == 0) len--;
        return std::string(reinterpret_cast<const char*>(data), len);
    } else if (encoding == 1) {
        // UTF-16 with BOM
        if (size < 2) return "";
        bool littleEndian = (data[0] == 0xFF && data[1] == 0xFE);
        // Simple: Just extract ASCII-range characters (good enough for most music)
        std::string result;
        for (size_t i = 2; i + 1 < size; i += 2) {
            uint16_t ch = littleEndian ? (data[i] | (data[i + 1] << 8)) : ((data[i] << 8) | data[i + 1]);
            if (ch == 0) break;
            if (ch < 128) result += static_cast<char>(ch);
            else result += '?'; // Non-ASCII placeholder
        }
        return result;
    } else if (encoding == 2) {
        // UTF-16BE no BOM
        std::string result;
        for (size_t i = 0; i + 1 < size; i += 2) {
            uint16_t ch = (data[i] << 8) | data[i + 1];
            if (ch == 0) break;
            if (ch < 128) result += static_cast<char>(ch);
            else result += '?';
        }
        return result;
    }
    
    return "";
}

// ============================================================================
// ID3v2 PARSER (MP3)
// ============================================================================

bool MetadataParser::parseID3v2(const std::string& filePath, AudioMetadata& meta) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return false;
    
    // Read header
    uint8_t header[10];
    file.read(reinterpret_cast<char*>(header), 10);
    if (!file || file.gcount() < 10) return false;
    
    // Check "ID3" signature
    if (header[0] != 'I' || header[1] != 'D' || header[2] != '3') {
        return false; // No ID3v2 tag
    }
    
    uint8_t majorVersion = header[3]; // 3 = ID3v2.3, 4 = ID3v2.4
    // uint8_t minorVersion = header[4];
    // uint8_t flags = header[5];
    uint32_t tagSize = readSynchsafeInt(&header[6]);
    
    // Read entire tag
    std::vector<uint8_t> tagData(tagSize);
    file.read(reinterpret_cast<char*>(tagData.data()), tagSize);
    if (!file || static_cast<uint32_t>(file.gcount()) < tagSize) return false;
    
    // Parse frames
    size_t offset = 0;
    while (offset + 10 <= tagSize) {
        // Frame header: 4 bytes ID, 4 bytes size, 2 bytes flags
        std::string frameId(reinterpret_cast<char*>(&tagData[offset]), 4);
        if (frameId[0] == '\0') break; // Padding
        
        uint32_t frameSize;
        if (majorVersion == 4) {
            frameSize = readSynchsafeInt(&tagData[offset + 4]);
        } else {
            frameSize = readBigEndian32(&tagData[offset + 4]);
        }
        
        // uint16_t frameFlags = (tagData[offset + 8] << 8) | tagData[offset + 9];
        offset += 10;
        
        if (offset + frameSize > tagSize) break;
        
        const uint8_t* frameData = &tagData[offset];
        
        // Text frames (TIT2, TPE1, TALB)
        if (frameId == "TIT2" && frameSize > 1) {
            meta.title = decodeID3String(frameData + 1, frameSize - 1, frameData[0]);
        } else if (frameId == "TPE1" && frameSize > 1) {
            meta.artist = decodeID3String(frameData + 1, frameSize - 1, frameData[0]);
        } else if (frameId == "TALB" && frameSize > 1) {
            meta.album = decodeID3String(frameData + 1, frameSize - 1, frameData[0]);
        } else if (frameId == "APIC" && frameSize > 12) {
            // Attached Picture
            uint8_t encoding = frameData[0];
            size_t pos = 1;
            
            // MIME type (null-terminated)
            std::string mimeType;
            while (pos < frameSize && frameData[pos] != 0) {
                mimeType += static_cast<char>(frameData[pos++]);
            }
            pos++; // Skip null
            
            if (pos >= frameSize) { offset += frameSize; continue; }
            
            uint8_t pictureType = frameData[pos++];
            (void)pictureType; // Could filter for front cover (type 3)
            
            // Description (null-terminated, encoding-aware)
            if (encoding == 0 || encoding == 3) {
                while (pos < frameSize && frameData[pos] != 0) pos++;
                pos++; // Skip null
            } else {
                // UTF-16: double null terminator
                while (pos + 1 < frameSize && !(frameData[pos] == 0 && frameData[pos + 1] == 0)) pos += 2;
                pos += 2;
            }
            
            // Picture data
            if (pos < frameSize && meta.coverArtData.empty()) {
                meta.coverArtData.assign(frameData + pos, frameData + frameSize);
                meta.coverArtMimeType = mimeType.empty() ? "image/jpeg" : mimeType;
            }
        }
        
        offset += frameSize;
    }
    
    return true;
}

// ============================================================================
// FLAC PARSER
// ============================================================================

bool MetadataParser::parseFLAC(const std::string& filePath, AudioMetadata& meta) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return false;
    
    // Check "fLaC" signature
    uint8_t sig[4];
    file.read(reinterpret_cast<char*>(sig), 4);
    if (!file || sig[0] != 'f' || sig[1] != 'L' || sig[2] != 'a' || sig[3] != 'C') {
        return false;
    }
    
    // Parse metadata blocks
    bool lastBlock = false;
    while (!lastBlock && file) {
        uint8_t blockHeader[4];
        file.read(reinterpret_cast<char*>(blockHeader), 4);
        if (!file || file.gcount() < 4) break;
        
        lastBlock = (blockHeader[0] & 0x80) != 0;
        uint8_t blockType = blockHeader[0] & 0x7F;
        uint32_t blockSize = readBigEndian24(&blockHeader[1]);
        
        if (blockSize == 0) continue;
        
        std::vector<uint8_t> blockData(blockSize);
        file.read(reinterpret_cast<char*>(blockData.data()), blockSize);
        if (!file || static_cast<uint32_t>(file.gcount()) < blockSize) break;
        
        if (blockType == 0) {
            // STREAMINFO - extract duration
            if (blockSize >= 18) {
                // Bytes 10-13 (upper 4 bits) + bytes 14-17: total samples
                // Sample rate: bytes 10-12 (20 bits)
                uint32_t sampleRate = (blockData[10] << 12) | (blockData[11] << 4) | (blockData[12] >> 4);
                uint64_t totalSamples = (static_cast<uint64_t>(blockData[13] & 0x0F) << 32) |
                                       (blockData[14] << 24) | (blockData[15] << 16) |
                                       (blockData[16] << 8) | blockData[17];
                if (sampleRate > 0) {
                    meta.durationSeconds = static_cast<double>(totalSamples) / sampleRate;
                }
            }
        } else if (blockType == 4) {
            // VORBIS_COMMENT
            if (blockSize < 8) continue;
            
            size_t pos = 0;
            // Vendor string (little-endian length)
            uint32_t vendorLen = blockData[0] | (blockData[1] << 8) | (blockData[2] << 16) | (blockData[3] << 24);
            pos = 4 + vendorLen;
            if (pos + 4 > blockSize) continue;
            
            // Number of comments
            uint32_t numComments = blockData[pos] | (blockData[pos + 1] << 8) | 
                                   (blockData[pos + 2] << 16) | (blockData[pos + 3] << 24);
            pos += 4;
            
            for (uint32_t i = 0; i < numComments && pos + 4 <= blockSize; ++i) {
                uint32_t commentLen = blockData[pos] | (blockData[pos + 1] << 8) |
                                      (blockData[pos + 2] << 16) | (blockData[pos + 3] << 24);
                pos += 4;
                if (pos + commentLen > blockSize) break;
                
                std::string comment(reinterpret_cast<char*>(&blockData[pos]), commentLen);
                pos += commentLen;
                
                // Parse "KEY=VALUE"
                size_t eq = comment.find('=');
                if (eq != std::string::npos) {
                    std::string key = toLowerCase(comment.substr(0, eq));
                    std::string value = comment.substr(eq + 1);
                    
                    if (key == "title") meta.title = value;
                    else if (key == "artist") meta.artist = value;
                    else if (key == "album") meta.album = value;
                }
            }
        } else if (blockType == 6) {
            // PICTURE
            if (blockSize < 32 || !meta.coverArtData.empty()) continue;
            
            size_t pos = 0;
            // uint32_t pictureType = readBigEndian32(&blockData[pos]); 
            pos += 4;
            
            uint32_t mimeLen = readBigEndian32(&blockData[pos]); pos += 4;
            if (pos + mimeLen > blockSize) continue;
            std::string mimeType(reinterpret_cast<char*>(&blockData[pos]), mimeLen);
            pos += mimeLen;
            
            uint32_t descLen = readBigEndian32(&blockData[pos]); pos += 4;
            pos += descLen; // Skip description
            
            pos += 16; // Skip width, height, depth, colors
            
            if (pos + 4 > blockSize) continue;
            uint32_t dataLen = readBigEndian32(&blockData[pos]); pos += 4;
            
            if (pos + dataLen <= blockSize) {
                meta.coverArtData.assign(&blockData[pos], &blockData[pos + dataLen]);
                meta.coverArtMimeType = mimeType;
            }
        }
    }
    
    return true;
}

// ============================================================================
// WAV PARSER (Basic - Minimal Metadata)
// ============================================================================

bool MetadataParser::parseWAV(const std::string& filePath, AudioMetadata& meta) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) return false;
    
    // Read RIFF header
    uint8_t header[44];
    file.read(reinterpret_cast<char*>(header), 44);
    if (!file || file.gcount() < 44) return false;
    
    // Validate RIFF/WAVE
    if (std::memcmp(header, "RIFF", 4) != 0 || std::memcmp(header + 8, "WAVE", 4) != 0) {
        return false;
    }
    
    // Extract duration from fmt chunk
    uint16_t numChannels = header[22] | (header[23] << 8);
    uint32_t sampleRate = header[24] | (header[25] << 8) | (header[26] << 16) | (header[27] << 24);
    uint16_t bitsPerSample = header[34] | (header[35] << 8);
    
    // Find data chunk size (simplified: assume at offset 40)
    uint32_t dataSize = header[40] | (header[41] << 8) | (header[42] << 16) | (header[43] << 24);
    
    if (sampleRate > 0 && numChannels > 0 && bitsPerSample > 0) {
        uint32_t bytesPerSample = (bitsPerSample / 8) * numChannels;
        if (bytesPerSample > 0) {
            meta.durationSeconds = static_cast<double>(dataSize) / (sampleRate * bytesPerSample);
        }
    }
    
    // WAV has minimal metadata - title from filename would be handled by caller
    return true;
}

std::string MetadataParser::readVorbisComment(const uint8_t* data, size_t size, const std::string& key) {
    // Utility - not used directly but available for extension
    (void)data; (void)size; (void)key;
    return "";
}

} // namespace Audio
} // namespace Aestra
