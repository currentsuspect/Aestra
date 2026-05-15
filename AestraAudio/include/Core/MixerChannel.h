// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../AestraUUID.h"
#include "../DSP/AudioProcessor.h"
#include "../Drivers/AudioDriverTypes.h"
#include "../Plugin/EffectChain.h"
#include "AudioCommandQueue.h"
#include "AudioGraph.h"
#include "MixerBus.h"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/**
 * @brief Unique identifier for a Mixer Channel
 */
struct MixerChannelID : public AestraUUID {
    /** @brief Cached numeric channel identifier. */
    uint64_t value = 0;

    MixerChannelID() = default;
    /** @brief Construct from a numeric identifier. */
    MixerChannelID(uint64_t v) : value(v) { low = v; }
    /** @brief Construct from a generic UUID by copying the low bits. */
    MixerChannelID(const AestraUUID& other) : AestraUUID(other), value(other.low) {}
};

/**
 * @brief Legacy alias for serialization
 */
using TrackUUID = AestraUUID;

/**
 * @brief Legacy Track states for UI compatibility
 */
enum class TrackState { Empty, Loading, Ready, Processing, Recording, Error };

/**
 * @brief Legacy Audio Quality definitions for UI compatibility
 */
enum class QualityPreset { Economy, Balanced, HighFidelity, Mastering, Custom };

enum class ResamplingMode { Fast, Medium, High, Ultra, Extreme, Perfect };

enum class AestraMode { Off, Transparent, Euphoric };

enum class InternalPrecision { Float32, Float64 };

enum class OversamplingMode { None, x2, x4, x8 };

struct AudioQualitySettings {
    /** @brief Preset controlling the remaining quality fields. */
    QualityPreset preset{QualityPreset::Balanced};
    /** @brief Resampling strategy. */
    ResamplingMode resampling{ResamplingMode::Medium};
    /** @brief Dithering mode applied on output. */
    DitheringMode dithering{DitheringMode::None};
    /** @brief Internal processing precision. */
    InternalPrecision precision{InternalPrecision::Float32};
    /** @brief Oversampling mode used by the channel. */
    OversamplingMode oversampling{OversamplingMode::None};
    /** @brief Whether DC removal is enabled. */
    bool removeDCOffset{true};
    /** @brief Whether the channel should apply soft clipping. */
    bool enableSoftClipping{false};
    /** @brief Aestra coloration mode. */
    AestraMode aestraMode{AestraMode::Off};

    /** @brief Apply a named quality preset. */
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
    /** @brief Construct a mixer channel with a display name and stable channel ID. */
    MixerChannel(const std::string& name = "Channel", uint32_t channelId = 0);
    /** @brief Destroy the mixer channel and its owned processing objects. */
    ~MixerChannel();

    /** @brief Get the UUID-style channel identifier. */
    MixerChannelID getID() const { return m_uuid; }
    /** @brief Get the dense numeric channel identifier. */
    uint32_t getChannelId() const { return m_channelId; }

    /** @brief Override the persisted UUID for the channel. */
    void setUUID(const AestraUUID& uuid) { m_uuid = uuid; }
    /** @brief Get the persisted UUID for the channel. */
    const AestraUUID& getUUID() const { return m_uuid; }

    /** @brief Set the display name shown in the UI. */
    void setName(const std::string& name);
    /** @brief Get the display name shown in the UI. */
    const std::string& getName() const { return m_name; }
    /** @brief Set the UI accent color. */
    void setColor(uint32_t color);
    /** @brief Get the UI accent color. */
    uint32_t getColor() const { return m_color; }

    /** @brief Set channel output volume. */
    void setVolume(float volume);
    /** @brief Get channel output volume. */
    float getVolume() const { return m_volume.load(); }
    /** @brief Set stereo pan. */
    void setPan(float pan);
    /** @brief Get stereo pan. */
    float getPan() const { return m_pan.load(); }
    /** @brief Set stereo width. */
    void setWidth(float width);
    /** @brief Get stereo width. */
    float getWidth() const { return m_width.load(); }
    /** @brief Set mute state. */
    void setMute(bool mute);
    /** @brief Check mute state. */
    bool isMuted() const { return m_muted.load(); }
    /** @brief Set solo state. */
    void setSolo(bool solo);
    /** @brief Check solo state. */
    bool isSoloed() const { return m_soloed.load(); }
    /** @brief Set solo-safe state. */
    void setSoloSafe(bool safe);
    /** @brief Check solo-safe state. */
    bool isSoloSafe() const { return m_soloSafe.load(); }

    /** @brief Arm or disarm the channel. */
    void setArmed(bool armed) {
        m_isArmed.store(armed);
        notifyInputMonitoringStateChanged();
    }
    /** @brief Check whether the channel is armed. */
    bool isArmed() const { return m_isArmed.load(); }
    /** @brief Enable or disable input monitoring. */
    void setMonitoringEnabled(bool enabled) {
        m_monitorInput.store(enabled);
        notifyInputMonitoringStateChanged();
    }
    /** @brief Check whether input monitoring is enabled. */
    bool isMonitoringEnabled() const { return m_monitorInput.load(); }
    /** @brief Select the monitored input channel index (-2 = Auto, -1 = None, >=0 = explicit input). */
    void setInputChannelIndex(int index) {
        m_inputChannelIndex.store(index);
        notifyInputMonitoringStateChanged();
    }
    /** @brief Get the monitored input channel index (-2 = Auto, -1 = None, >=0 = explicit input). */
    int getInputChannelIndex() const { return m_inputChannelIndex.load(); }

    /** @brief Process this channel's audio for one callback block. */
    void processAudio(float* outputBuffer, uint32_t numFrames, double streamTime, double outputSampleRate);

    /** @brief Pre-allocate scratch buffers required by processAudio(). Must be called off the audio thread. */
    void prepareProcessingBuffers(uint32_t maxFrames);

    /** @brief Get the last stereo correlation value reported by the bus. */
    float getLastCorrelation() const {
        // Return 0 if no bus (e.g. inactive)
        return m_mixerBus ? m_mixerBus->getLastCorrelation() : 0.0f;
    }

    /** @brief Access the owned mixer bus. */
    MixerBus* getMixerBus() { return m_mixerBus.get(); }
    /** @brief Access the owned mixer bus. */
    const MixerBus* getMixerBus() const { return m_mixerBus.get(); }

    /** @brief Set the audio-thread command sink used for RT-safe updates. */
    void setCommandSink(std::function<void(const AudioQueueCommand&)> cb) { m_commandSink = std::move(cb); }
    /** @brief Set callback used to refresh input monitoring snapshots after route-affecting changes.
     * NOTE: Must be set before audio engine processing starts. Caller is responsible for thread safety. */
    void setInputMonitoringStateChangedCallback(std::function<void()> cb) {
        m_inputMonitoringStateChanged = std::move(cb);
    }

    /** @brief Apply quality settings to the channel. */
    void setQualitySettings(const AudioQualitySettings&) {}

    /** @brief Access the insert-effect chain. */
    EffectChain& getEffectChain() { return m_effectChain; }
    /** @brief Access the insert-effect chain. */
    const EffectChain& getEffectChain() const { return m_effectChain; }

    void setEffectChainLatencyCallback(std::function<void()> callback) {
        m_effectChain.setLatencyChangedCallback(std::move(callback));
    }

    /** @brief Reset all plugins in the effect chain (panic/hard reset). */
    void resetEffectChain() { m_effectChain.reset(); }

    /** @brief Get the primary output destination identifier. */
    uint32_t getMainOutputId() const { return m_mainOutputId; }

    /** @brief Get a copy of the current send list. Thread-safe: caller must not be RT. */
    std::vector<AudioRoute> getSends() const; // Returns c std::vector<AudioRoute> getSends() const; // Returns copy
    /** @brief Add a send route. */
    void addSend(const AudioRoute& route);
    /** @brief Remove a send route by index. */
    void removeSend(int index);
    /** @brief Update send level by index. */
    void setSendLevel(int index, float level);
    /** @brief Update send pan by index. */
    void setSendPan(int index, float pan);
    /** @brief Update send destination by index. */
    void setSendDestination(int index, uint32_t destId);
    /** @brief Update send pre/post fader mode by index. */
    void setSendPostFader(int index, bool postFader);
    /** @brief Update send audible/sidechain-only mode by index. */
    void setSendSidechainOnly(int index, bool sidechainOnly);

    /** @brief Set the effect chain snapshot for RT-safe processing (deprecated - snapshots are now owned by EffectChain). */
    void setEffectChainSnapshot(std::shared_ptr<const EffectChainSnapshot> snapshot) {
        std::atomic_store(&m_effectChainSnapshot, std::move(snapshot));
    }
    /** @brief Get the current effect chain snapshot (Pass 3: returns canonical snapshot from EffectChain). */
    std::shared_ptr<const EffectChainSnapshot> getEffectChainSnapshot() const {
        return m_effectChain.getSnapshot();
    }

    /** @brief Set the primary output destination identifier. */
    void setMainOutputId(uint32_t id) { m_mainOutputId = id; }

private:
    std::string m_name;
    AestraUUID m_uuid;
    uint32_t m_channelId;
    uint32_t m_color;

    // Audio parameters (atomic for thread safety)
    std::atomic<float> m_volume{1.0f};
    std::atomic<float> m_pan{0.0f};
    std::atomic<float> m_width{1.0f};
    std::atomic<bool> m_muted{false};
    std::atomic<bool> m_soloed{false};
    std::atomic<bool> m_soloSafe{false};

    // Recording
    std::atomic<bool> m_isArmed{false};
    std::atomic<bool> m_monitorInput{false};
    std::atomic<int> m_inputChannelIndex{-2}; // -2 = Auto mono, -1 = None, 0 = Input 1, 1 = Input 2, ...

    // Mixer integration
    std::unique_ptr<MixerBus> m_mixerBus;

    // Effect chain for insert effects
    EffectChain m_effectChain;

    // Pass 3: Snapshot for RT-safe processing (updated by AudioGraph publication)
    std::shared_ptr<const EffectChainSnapshot> m_effectChainSnapshot{nullptr};

    // Dry buffer for RT-safe dry/wet mixing in snapshot processing
    std::vector<float> m_dryChannelBuf;

    std::function<void(const AudioQueueCommand&)> m_commandSink;
    std::function<void()> m_inputMonitoringStateChanged;

    // Pre-allocated deinterleave buffers for RT-safe effect processing
    std::vector<float> m_leftChannelBuf;
    std::vector<float> m_rightChannelBuf;

    // Routing (v3.1)
    // Primary output (defaults to Master). 0xFFFFFFFF = Master.
    uint32_t m_mainOutputId{0xFFFFFFFF};

    // Aux Sends / Direct Outs
    // Aux Sends / Direct Outs
    mutable std::mutex m_sendMutex;
    std::vector<AudioRoute> m_sends;

    void notifyInputMonitoringStateChanged() {
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lock(m_sendMutex);
            cb = m_inputMonitoringStateChanged;
        }
        if (cb) {
            cb();
        }
    }
};

using Track = MixerChannel;

} // namespace Audio
} // namespace Aestra
