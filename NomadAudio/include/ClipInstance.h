// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TimeTypes.h"
#include "PatternSource.h"
#include "MixerChannel.h"
#include <cstdint>
#include <memory>
#include <random>
#include <string>
#include <variant>

namespace Nomad {
namespace Audio {

struct PlaylistTrackID;



// =============================================================================
// ClipInstanceID - Unique clip identity
// =============================================================================

struct ClipInstanceID {
    uint64_t high = 0;
    uint64_t low = 0;
    
    bool isValid() const { return high != 0 || low != 0; }
    bool operator==(const ClipInstanceID& other) const { 
        return high == other.high && low == other.low; 
    }
    bool operator!=(const ClipInstanceID& other) const { return !(*this == other); }
    bool operator<(const ClipInstanceID& other) const {
        if (high != other.high) return high < other.high;
        return low < other.low;
    }
    
    std::string toString() const {
        char buf[37];
        snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                 static_cast<uint32_t>(high >> 32),
                 static_cast<uint16_t>(high >> 16),
                 static_cast<uint16_t>(high),
                 static_cast<uint16_t>(low >> 48),
                 static_cast<unsigned long long>(low & 0xFFFFFFFFFFFFULL));
        return std::string(buf);
    }
    
    static ClipInstanceID fromString(const std::string& str) {
        ClipInstanceID uuid;
        if (str.length() >= 36) {
            unsigned int a, b, c, d;
            unsigned long long e;
            if (sscanf(str.c_str(), "%8x-%4x-%4x-%4x-%12llx", &a, &b, &c, &d, &e) == 5) {
                uuid.high = (static_cast<uint64_t>(a) << 32) | 
                           (static_cast<uint64_t>(b) << 16) | 
                           static_cast<uint64_t>(c);
                uuid.low = (static_cast<uint64_t>(d) << 48) | e;
            }
        }
        return uuid;
    }

    static ClipInstanceID generate() {
        static std::random_device rd;
        static std::mt19937_64 gen(rd());
        static std::uniform_int_distribution<uint64_t> dis;
        
        ClipInstanceID id;
        id.high = dis(gen);
        id.low = dis(gen);
        id.high = (id.high & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
        id.low = (id.low & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
        return id;
    }
};

// =============================================================================
// LinkMode - Pattern linking semantics
// =============================================================================

enum class LinkMode {
    LinkedReadOnly,    // Follows pattern, timeline content edits forbidden
    LinkedPropagating, // Follows pattern, timeline content edits push back (v2+)
    Detached           // Unique "Pattern of One"
};

// =============================================================================
// LocalEdits - Per-instance overrides for caching
// =============================================================================

struct LocalEdits {
    float gainLinear = 1.0f;
    float pan = 0.0f;
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
    double playbackRate = 1.0;
    SampleIndex sourceStart = 0;
    bool muted = false;
    bool syncToProject = true;
    
    // Generate a signature for cache invalidation
    uint64_t getSignature() const {
        uint64_t sig = 0;
        // Simple hash of parameters
        auto f2u = [](float f) { uint32_t u; memcpy(&u, &f, 4); return u; };
        auto d2u = [](double d) { uint64_t u; memcpy(&u, &d, 8); return u; };
        sig ^= d2u(gainLinear);
        sig ^= d2u(pan) << 1;
        sig ^= d2u(fadeInBeats) << 2;
        sig ^= d2u(fadeOutBeats) << 3;
        sig ^= d2u(playbackRate) << 4;
        sig ^= d2u(static_cast<double>(sourceStart)) << 5;
        sig ^= (muted ? 1ull : 0ull) << 31;
        sig ^= (syncToProject ? 1ull : 0ull) << 32;
        return sig;
    }

};

// =============================================================================
// ClipInstance - Atomic playlist object (v3.0)
// =============================================================================

struct ClipInstance {
    ClipInstanceID id;
    
    // Naming & Routing
    uint64_t playlistTrackId = 0; // Lane ID
    MixerChannelID mixerChannelId;  // Target Mixer Destination
    
    // Timeline Position (Beats)
    double startBeat = 0.0;
    double durationBeats = 0.0;
    
    // Content Reference
    PatternID patternId;
    LinkMode linkMode = LinkMode::LinkedReadOnly;
    
    // Local Overrides
    LocalEdits edits;
    
    // UI / Metadata
    uint32_t colorRGBA = 0xFF4A90D9;
    std::string name;
    bool muted = false;


    ClipInstance() : id(ClipInstanceID::generate()) {}
};

} // namespace Audio
} // namespace Nomad
