// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "MixerBus.h"
#include <memory>
#include <vector>
#include <string>
#include <atomic>
#include <mutex>
#include <functional>
#include "AudioCommandQueue.h"

#include "NomadUUID.h"

namespace Nomad {
namespace Audio {

/**
 * @brief Unique identifier for a Mixer Channel
 */
struct MixerChannelID : public NomadUUID {
    uint64_t value = 0;
    
    MixerChannelID() = default;
    MixerChannelID(uint64_t v) : value(v) { low = v; }
    MixerChannelID(const NomadUUID& other) : NomadUUID(other), value(other.low) {}
};


/**
 * @brief Legacy alias for serialization
 */
using TrackUUID = NomadUUID;

/**
 * @brief Legacy Track states for UI compatibility
 */
enum class TrackState {
    Empty,
    Loading,
    Ready,
    Processing,
    Recording,
    Error
};

/**
 * @brief Legacy Audio Quality definitions for UI compatibility
 */
enum class QualityPreset {
    Economy,
    Balanced,
    HighFidelity,
    Mastering,
    Custom
};

enum class ResamplingMode {
    Fast,
    Medium,
    High,
    Ultra,
    Extreme,
    Perfect
};

enum class DitheringMode {
    None,
    Triangular,
    HighPass,
    NoiseShaped
};

enum class NomadMode {
    Off,
    Transparent,
    Euphoric
};

enum class InternalPrecision {
    Float32,
    Float64
};

enum class OversamplingMode {
    None,
    x2,
    x4,
    x8
};

struct AudioQualitySettings {
    QualityPreset preset{QualityPreset::Balanced};
    ResamplingMode resampling{ResamplingMode::Medium};
    DitheringMode dithering{DitheringMode::None};
    InternalPrecision precision{InternalPrecision::Float32};
    OversamplingMode oversampling{OversamplingMode::None};
    bool removeDCOffset{true};
    bool enableSoftClipping{false};
    NomadMode nomadMode{NomadMode::Off};

    void applyPreset(QualityPreset p) {
        preset = p;
        if (p == QualityPreset::Economy) {
            resampling = ResamplingMode::Fast;
            dithering = DitheringMode::None;
        } else if (p == QualityPreset::Balanced) {
            resampling = ResamplingMode::Medium;
            dithering = DitheringMode::Triangular;
        } else if (p == QualityPreset::HighFidelity) {
            resampling = ResamplingMode::High;
            dithering = DitheringMode::HighPass;
        } else if (p == QualityPreset::Mastering) {
            resampling = ResamplingMode::Perfect;
            dithering = DitheringMode::NoiseShaped;
        }
    }
};


/**
 * @brief Mixer Channel - Dedicated routing and DSP entity (v3.0)



 *
 * Refactored from legacy Track.h. Stripped of timeline/clip storage.
 * Focuses on:
 * - Routing (MixerBus)
 * - Volume, Pan, Mute, Solo
 * - DSP Effects & Quality Settings
 */
class MixerChannel {
public:
    MixerChannel(const std::string& name = "Channel", uint32_t channelId = 0);
    ~MixerChannel();

    // === Identity ===

    MixerChannelID getID() const { return m_uuid; }
    uint32_t getChannelId() const { return m_channelId; }
    
    void setUUID(const NomadUUID& uuid) { m_uuid = uuid; }
    const NomadUUID& getUUID() const { return m_uuid; }


    
    // Channel Properties
    void setName(const std::string& name);
    const std::string& getName() const { return m_name; }
    void setColor(uint32_t color);
    uint32_t getColor() const { return m_color; }

    // Audio Parameters (thread-safe)
    void setVolume(float volume);
    float getVolume() const { return m_volume.load(); }
    void setPan(float pan);
    float getPan() const { return m_pan.load(); }
    void setMute(bool mute);
    bool isMuted() const { return m_muted.load(); }
    void setSolo(bool solo);
    bool isSoloed() const { return m_soloed.load(); }

    // Audio Processing
    // MixerChannel processes its input bus/buffer. Timeline clips are managed by PlaylistModel.
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate);

    // Mixer Integration
    MixerBus* getMixerBus() { return m_mixerBus.get(); }
    const MixerBus* getMixerBus() const { return m_mixerBus.get(); }
    
    // Command sink for RT parameter updates
    void setCommandSink(std::function<void(const AudioQueueCommand&)> cb) { m_commandSink = std::move(cb); }
    
    void setQualitySettings(const AudioQualitySettings&) {}

private:
    std::string m_name;
    NomadUUID m_uuid;
    uint32_t m_channelId;
    uint32_t m_color;

    // Audio parameters (atomic for thread safety)
    std::atomic<float> m_volume{1.0f};
    std::atomic<float> m_pan{0.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_soloed{false};

    // Mixer integration
    std::unique_ptr<MixerBus> m_mixerBus;

    std::function<void(const AudioQueueCommand&)> m_commandSink;
};

using Track = MixerChannel;

} // namespace Audio
} // namespace Nomad

