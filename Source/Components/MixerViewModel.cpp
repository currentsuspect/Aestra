// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "MixerViewModel.h"
#include "../AestraCore/include/AestraLog.h"
#include "Commands/RoutingCommands.h"
#include "AudioDeviceManager.h"
#include "../App/ServiceLocator.h"
#include "../Core/AestraAudioController.h"
#include "../../AestraUI/Widgets/TrackColorPalette.h"
#include <cstdio>
#include <unordered_map>

namespace Aestra {

MixerViewModel::MixerViewModel() {
    // Create master channel
    m_master = std::make_unique<ChannelViewModel>();
    m_master->id = 0;
    m_master->slotIndex = Audio::ChannelSlotMap::MASTER_SLOT_INDEX;
    m_master->name = "MASTER";
    m_master->routeName = "Output";
    m_master->trackColorIndex = 1; // Soft Purple (matches Aestra brand)
}

void MixerViewModel::refreshInputs(const Aestra::Audio::AudioDeviceManager& deviceManager) {
    inputNames.clear();
    inputDeviceIds.clear();
    
    // Add "Auto" and "None" options first.
    inputNames.push_back("Auto");
    inputDeviceIds.push_back(-2);
    inputNames.push_back("None");
    inputDeviceIds.push_back(-1);

    // Get input channel count from current config
    const auto& config = deviceManager.getCurrentConfig();
    int numInputs = config.numInputChannels;

    // Generate channel-based names (Input 1, Input 2, etc.)
    for (int i = 0; i < numInputs; ++i) {
        inputNames.push_back("Input " + std::to_string(i + 1));
        inputDeviceIds.push_back(i); // Channel index
    }
}

void MixerViewModel::updateMeters(const Audio::MeterSnapshotBuffer& snapshots, double deltaTime) {
    // Update channel meters
    for (auto& channel : m_channels) {
        if (!channel) continue;

        if (auto mc = channel->channel) {
            channel->muted = mc->isMuted();
            channel->soloed = mc->isSoloed();
            // channel->armed = mc->isRecording(); // MixerChannel has no recording state
        }

        const auto snapshot = snapshots.readSnapshot(channel->slotIndex);
        smoothMeterChannel(*channel, snapshot, deltaTime);
    }

    // Update master meter
    if (m_master) {
        const auto snapshot = snapshots.readSnapshot(m_master->slotIndex);
        smoothMeterChannel(*m_master, snapshot, deltaTime);
    }
}

void MixerViewModel::updateInputDiagnostics(const Audio::TrackManager& trackManager, double deltaTime) {
    const float attack = 1.0f - std::exp(static_cast<float>(-deltaTime * 22.0));
    const float release = 1.0f - std::exp(static_cast<float>(-deltaTime * 5.0));
    const int availableInputs = trackManager.getInputChannelCount();

    for (auto& channel : m_channels) {
        if (!channel) continue;

        float targetPeak = 0.0f;
        if (channel->inputChannelIndex == -2) {
            channel->inputSourceName = "Auto";
            for (int i = 0; i < availableInputs; ++i) {
                targetPeak = std::max(targetPeak, trackManager.getInputPeak(i));
            }
        } else if (channel->inputChannelIndex == -1) {
            channel->inputSourceName = "None";
        } else {
            channel->inputSourceName = "Input " + std::to_string(channel->inputChannelIndex + 1);
            targetPeak = trackManager.getInputPeak(channel->inputChannelIndex);
        }

        const float coeff = targetPeak > channel->inputPeak ? attack : release;
        channel->inputPeak += (targetPeak - channel->inputPeak) * coeff;
        channel->inputPeak = std::clamp(channel->inputPeak, 0.0f, 1.0f);
    }
}

void MixerViewModel::setPreviewDuckGain(float gain) {
    setPreviewDuckState(gain, {});
}

void MixerViewModel::setPreviewDuckState(float gain, const std::string& sourceLabel) {
    if (!std::isfinite(gain)) {
        gain = 1.0f;
    }
    m_previewDuckGain = std::clamp(gain, 0.0f, 1.0f);
    m_previewDuckSourceLabel = m_previewDuckGain < 0.995f ? sourceLabel : std::string{};
}

void MixerViewModel::syncFromEngine(const Audio::TrackManager& trackManager,
                                     const Audio::ChannelSlotMap& slotMap) {
    auto continuousParams = trackManager.getContinuousParams();

    if (m_master && continuousParams) {
        continuousParams->read(Audio::ChannelSlotMap::MASTER_SLOT_INDEX,
                               m_master->faderGainDb,
                               m_master->pan,
                               m_master->trimDb);
    }

    if (m_master) {
        // Master strip is a plugin host like any other channel (triage
        // 2026-08-14): wire the engine-side Master MixerChannel so the
        // existing insert paths (add/remove/bypass/mix/ordering) work
        // unchanged, and sync its insert slots into the view model. Hoisted
        // out of the continuous-params guard: insert-chain sync does not read
        // continuous params, and gating on them left master unwired (and
        // master inserts silently no-oping) whenever the buffer was absent.
        if (auto* masterChannel = trackManager.getMasterChannel()) {
            m_master->channel = masterChannel;
            if (m_master->inserts.size() != Audio::EffectChain::MAX_SLOTS) {
                m_master->inserts.resize(Audio::EffectChain::MAX_SLOTS);
            }
            auto& chain = masterChannel->getEffectChain();
            int fxCount = 0;
            for (size_t i = 0; i < Audio::EffectChain::MAX_SLOTS; ++i) {
                const auto* slot = chain.getSlot(i);
                auto& vm = m_master->inserts[i];
                const bool hasPlugin = (slot && !slot->isEmpty() && slot->plugin);
                // The chain is authoritative either way: the engine either
                // confirmed the removal (no plugin) or a plugin occupies the
                // slot again (re-add before confirmation, or a failed
                // removal). Clear the optimistic flag and show the live state.
                vm.pendingRemoval = false;
                if (hasPlugin) {
                    ++fxCount;
                    vm.isEmpty = false;
                    vm.name = slot->plugin->getInfo().name;
                    if (vm.name.empty()) vm.name = "Plugin";
                    vm.bypassed = slot->bypassed.load();
                    vm.mix = slot->dryWetMix.load();
                } else {
                    vm.isEmpty = true;
                    vm.name.clear();
                }
            }
            m_master->fxCount = fxCount;
        }
    }

    // Build set of current track IDs for quick lookup
    std::unordered_map<uint32_t, size_t> existingIds;
    
    // NOTE: inputNames is now managed by refreshInputs() from AudioDeviceManager
    // DO NOT overwrite here with trackManager.getInputChannelNames()

    for (size_t i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i]) {
            existingIds[m_channels[i]->id] = i;
        }
    }

    // Collect channel info from engine
    struct ChannelInfo {
        uint32_t id{0};
        std::string name;
        uint32_t color{0};
        uint32_t slot{0};
        Audio::MixerChannel* channel{nullptr};
        bool muted{false};
        bool soloed{false};
        bool armed{false};
        bool monitored{false};
        int fxCount{0};
    };
    std::vector<ChannelInfo> channelInfo;
    auto channels = trackManager.getChannelsSnapshot();
    for (const auto& channel : channels) {
        if (!channel) continue;

        uint32_t id = channel->getChannelId();
        uint32_t slot = slotMap.getSlotIndex(id);
        if (slot != Audio::ChannelSlotMap::INVALID_SLOT) {
            ChannelInfo info;
            info.id = id;
            info.name = channel->getName();
            info.color = channel->getColor();
            info.slot = slot;
            info.channel = channel;
            info.muted = channel->isMuted();
            info.soloed = channel->isSoloed();
            info.armed = channel->isArmed();
            info.monitored = channel->isMonitoringEnabled();
            info.fxCount = static_cast<int>(channel->getEffectChain().getActiveSlotCount());
            channelInfo.push_back(std::move(info));
        }
    }

    // Rebuild channel list to match tracks
    std::vector<std::unique_ptr<ChannelViewModel>> newChannels;
    newChannels.reserve(channelInfo.size());
    std::unordered_map<uint32_t, std::string> channelInfoById;
    channelInfoById.reserve(channelInfo.size());
    for (const auto& info : channelInfo) {
        channelInfoById[info.id] = info.name;
    }

    auto resolveMainOutputName = [&](uint32_t mainOutputId) -> std::string {
        if (mainOutputId == 0xFFFFFFFFu || mainOutputId == 0u) {
            return "Master";
        }
        auto targetIt = channelInfoById.find(mainOutputId);
        if (targetIt != channelInfoById.end()) {
            return targetIt->second;
        }
        return "Unknown (" + std::to_string(mainOutputId) + ")";
    };

    for (const auto& info : channelInfo) {
        const auto it = existingIds.find(info.id);
        if (it != existingIds.end() && m_channels[it->second]) {
            // Reuse existing channel (preserves meter state)
            auto& existing = m_channels[it->second];
            existing->id = info.id;
            existing->name = info.name;
            existing->slotIndex = info.slot;
            existing->channel = info.channel;
            existing->muted = info.muted;
            existing->soloed = info.soloed;
            existing->armed = info.armed;
            existing->monitored = info.monitored;
            existing->fxCount = info.fxCount;
            if (auto* mc = info.channel) {
                existing->inputChannelIndex = mc->getInputChannelIndex();
                existing->width = mc->getWidth();
                const uint32_t engineMainOutputId = mc->getMainOutputId();
                existing->mainOutputId = (engineMainOutputId == 0xFFFFFFFFu) ? 0u : engineMainOutputId;
                existing->masterSendEnabled = (engineMainOutputId == 0xFFFFFFFFu);
                existing->routeName = resolveMainOutputName(engineMainOutputId);
            }
            if (continuousParams && info.slot != Audio::ChannelSlotMap::INVALID_SLOT) {
                continuousParams->read(info.slot, existing->faderGainDb, existing->pan, existing->trimDb);
            }
            newChannels.push_back(std::move(existing));
        } else {
            // Create new channel
            auto channel = std::make_unique<ChannelViewModel>();
            channel->id = info.id;
            channel->slotIndex = info.slot;
            channel->channel = info.channel;
            channel->name = info.name;
            // The engine channel's palette index is the persisted source of truth
            // (restored by ProjectSerializer on load). Derive from the RGBA color
            // only when unset, and only then write the derived value back —
            // otherwise a rebuild after load would clobber the saved index.
            const int persistedIndex = info.channel ? info.channel->getTrackColorIndex() : -1;
            if (persistedIndex >= 0 && persistedIndex < AestraUI::PALETTE_SIZE) {
                channel->trackColorIndex = persistedIndex;
            } else {
                channel->trackColorIndex = AestraUI::nearestPaletteIndex(info.color);
                if (channel->trackColorIndex < 0) {
                    channel->trackColorIndex = static_cast<int>(newChannels.size()) % AestraUI::PALETTE_SIZE;
                }
                if (channel->channel) {
                    channel->channel->setTrackColorIndex(channel->trackColorIndex);
                }
            }
            channel->muted = info.muted;
            channel->soloed = info.soloed;
            channel->armed = info.armed;
            channel->monitored = info.monitored;
            channel->fxCount = info.fxCount;
            if (auto* mc = info.channel) {
                channel->inputChannelIndex = mc->getInputChannelIndex();
                channel->width = mc->getWidth();
                const uint32_t engineMainOutputId = mc->getMainOutputId();
                channel->mainOutputId = (engineMainOutputId == 0xFFFFFFFFu) ? 0u : engineMainOutputId;
                channel->masterSendEnabled = (engineMainOutputId == 0xFFFFFFFFu);
                channel->routeName = resolveMainOutputName(engineMainOutputId);
            }
            if (continuousParams && info.slot != Audio::ChannelSlotMap::INVALID_SLOT) {
                continuousParams->read(info.slot, channel->faderGainDb, channel->pan, channel->trimDb);
            }
            newChannels.push_back(std::move(channel));
        }
        
        // Sync Sends from Engine (Persistence Fix)
        if (auto* ch = newChannels.back().get()) {
            if (auto* mc = ch->channel) {
               auto engineSends = mc->getSends();
               ch->sends.clear();
               for (const auto& route : engineSends) {
                   ChannelViewModel::SendViewModel uiSend{};
                   // Handle Legacy Master ID
                   if (route.targetChannelId == 0xFFFFFFFF) {
                       uiSend.targetId = 0;
                       uiSend.targetName = "Master";
                   } else {
                       uiSend.targetId = route.targetChannelId;
                       // Try to resolve name from current snapshot info
                       auto targetIt = channelInfoById.find(route.targetChannelId);
                       if (targetIt != channelInfoById.end()) {
                           uiSend.targetName = targetIt->second;
                       } else if (route.targetChannelId == 0) {
                           uiSend.targetName = "Master";
                       } else {
                           uiSend.targetName = "Unknown (" + std::to_string(route.targetChannelId) + ")";
                       }
                   }
                   uiSend.sendId = route.sendId;
                   uiSend.gain = route.gain;
                   uiSend.pan = route.pan;
                   uiSend.postFader = route.postFader;
                   uiSend.muted = route.mute;
                   uiSend.sidechainOnly = route.sidechainOnly;
                   ch->sends.push_back(uiSend);
               }
            }
        }
        
        // Sync Inserts (New)
        if (auto* ch = newChannels.back().get()) {
            if (auto mc = ch->channel) {
                auto& chain = mc->getEffectChain();
                
                // Ensure size matches
                if (ch->inserts.size() != Audio::EffectChain::MAX_SLOTS) {
                    ch->inserts.resize(Audio::EffectChain::MAX_SLOTS);
                }

                // PRESERVE UI STATE (Fix for Delete Persistence)
                const ChannelViewModel* oldCh = nullptr;
                if (ch->id == 0) {
                     oldCh = m_master.get();
                } else {
                     oldCh = getChannelById(ch->id);
                }

                for (size_t i = 0; i < Audio::EffectChain::MAX_SLOTS; ++i) {
                    const auto* slot = chain.getSlot(i);
                    auto& vm = ch->inserts[i];

                    // Restore flags from old state
                    if (oldCh && i < oldCh->inserts.size()) {
                        vm.pendingRemoval = oldCh->inserts[i].pendingRemoval;
                        vm.bypassDirty = oldCh->inserts[i].bypassDirty;
                    }

                    const auto* slotptr = slot; // alias for clarity if needed

                    bool hasPlugin = (slot && !slot->isEmpty() && slot->plugin);
                    
                     if (vm.pendingRemoval) {
                          if (!hasPlugin) {
                              // Engine finally removed it
                              vm.pendingRemoval = false;
                              vm.isEmpty = true;
                              vm.name.clear();
                          } else {
                              // A plugin occupied this slot again — either the
                              // user re-added before the engine confirmed the
                              // removal, or the removal failed. The chain is
                              // authoritative: show the live plugin instead of
                              // pinning "Removing..." forever (delete -> re-add
                              // used to leave the insert area stuck red).
                              vm.pendingRemoval = false;
                              vm.isEmpty = false;
                              vm.name = slot->plugin->getInfo().name;
                              if (vm.name.empty()) vm.name = "Plugin";
                              vm.bypassed = slot->bypassed.load();
                              vm.mix = slot->dryWetMix.load();
                          }
                    } else {
                        // Normal Sync
                        bool engineBypassed = hasPlugin ? slot->bypassed.load() : false;
                        float engineMix = hasPlugin ? slot->dryWetMix.load() : 1.0f;

                        if (hasPlugin) {
                             vm.isEmpty = false;
                             vm.name = slot->plugin->getInfo().name;
                             if (vm.name.empty()) vm.name = "Plugin"; 
                        } else {
                             vm.isEmpty = true;
                             vm.name.clear();
                        }

                        // Sync Bypass (Optimistic)
                        if (vm.bypassDirty) {
                            if (vm.bypassed == engineBypassed) {
                                vm.bypassDirty = false; 
                            }
                        } else {
                            vm.bypassed = engineBypassed;
                        }

                        vm.mix = engineMix;
                    }
                }
            }
        }
    }

    m_channels = std::move(newChannels);
    rebuildIdMap();

    // Validate or establish startup selection.
    if (m_selectedChannelId >= 0 && !getChannelById(static_cast<uint32_t>(m_selectedChannelId))) {
        m_selectedChannelId = -1;
    }

    if (m_selectedChannelId < 0 && !m_channels.empty()) {
        uint32_t fallbackId = m_channels.front()->id;
        for (const auto& channel : m_channels) {
            if (channel && channel->fxCount > 0) {
                fallbackId = channel->id;
                break;
            }
        }
        m_selectedChannelId = static_cast<int32_t>(fallbackId);
    }
}

ChannelViewModel* MixerViewModel::getChannelById(uint32_t id) {
    if (id == 0) return m_master.get();
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end() || it->second >= m_channels.size()) {
        return nullptr;
    }
    return m_channels[it->second].get();
}

const ChannelViewModel* MixerViewModel::getChannelById(uint32_t id) const {
    if (id == 0) return m_master.get();
    auto it = m_idToIndex.find(id);
    if (it == m_idToIndex.end() || it->second >= m_channels.size()) {
        return nullptr;
    }
    return m_channels[it->second].get();
}

ChannelViewModel* MixerViewModel::getSelectedChannel() {
    if (m_selectedChannelId < 0) return nullptr;
    return getChannelById(static_cast<uint32_t>(m_selectedChannelId));
}

const ChannelViewModel* MixerViewModel::getSelectedChannel() const {
    if (m_selectedChannelId < 0) return nullptr;
    return getChannelById(static_cast<uint32_t>(m_selectedChannelId));
}

ChannelViewModel* MixerViewModel::getChannelByIndex(size_t index) {
    if (index >= m_channels.size()) return nullptr;
    return m_channels[index].get();
}

const ChannelViewModel* MixerViewModel::getChannelByIndex(size_t index) const {
    if (index >= m_channels.size()) return nullptr;
    return m_channels[index].get();
}

void MixerViewModel::clearClipLatch(uint32_t id) {
    auto* channel = getChannelById(id);
    if (channel) {
        channel->clipLatchL = false;
        channel->clipLatchR = false;
        channel->suppressClipRelatchL = true;
        channel->suppressClipRelatchR = true;
    }
}

void MixerViewModel::clearMasterClipLatch() {
    if (m_master) {
        m_master->clipLatchL = false;
        m_master->clipLatchR = false;
        m_master->suppressClipRelatchL = true;
        m_master->suppressClipRelatchR = true;
    }
}

void MixerViewModel::rebuildIdMap() {
    m_idToIndex.clear();
    for (size_t i = 0; i < m_channels.size(); ++i) {
        if (m_channels[i]) {
            m_idToIndex[m_channels[i]->id] = i;
        }
    }
}


void MixerViewModel::smoothMeterChannel(ChannelViewModel& channel,
                                        const Audio::MeterSnapshotBuffer::MeterReadout& snapshot,
                                        double deltaTime) {
    auto sanitizeLinear = [](float value) -> float {
        if (!std::isfinite(value)) return 0.0f;
        return std::clamp(value, 0.0f, 4.0f);
    };
    auto sanitizeDb = [](float value) -> float {
        if (!std::isfinite(value)) return MixerMath::DB_MIN;
        return std::clamp(value, MixerMath::DB_MIN, 12.0f);
    };
    constexpr float CLIP_RELEASE_THRESHOLD = 0.999f;

    // Convert LINEAR to dB (UI mapping is log-space).
    const float peakLinearL = sanitizeLinear(snapshot.peakL);
    const float peakLinearR = sanitizeLinear(snapshot.peakR);
    const float peakDbL = sanitizeDb(MixerMath::linearToDb(peakLinearL));
    const float peakDbR = sanitizeDb(MixerMath::linearToDb(peakLinearR));
    const float energyDbL = sanitizeDb(MixerMath::linearToDb(sanitizeLinear(snapshot.rmsL)));
    const float energyDbR = sanitizeDb(MixerMath::linearToDb(sanitizeLinear(snapshot.rmsR)));
    const float lowDbL = sanitizeDb(MixerMath::linearToDb(sanitizeLinear(snapshot.lowL)));
    const float lowDbR = sanitizeDb(MixerMath::linearToDb(sanitizeLinear(snapshot.lowR)));

    auto smoothDb = [&](float current, float target, float attackMs, float releaseMs) -> float {
        current = sanitizeDb(current);
        target = sanitizeDb(target);
        const float ms = static_cast<float>(deltaTime * 1000.0);
        const float tau = (target > current) ? attackMs : releaseMs;
        const float coeff = 1.0f - std::exp(-ms / std::max(1e-3f, tau));
        return sanitizeDb(current + (target - current) * coeff);
    };

    // Analysis envelopes (never drawn directly).
    channel.envPeakL = smoothDb(channel.envPeakL, peakDbL, PEAK_ATTACK_MS, PEAK_RELEASE_MS);
    channel.envPeakR = smoothDb(channel.envPeakR, peakDbR, PEAK_ATTACK_MS, PEAK_RELEASE_MS);
    channel.envEnergyL = smoothDb(channel.envEnergyL, energyDbL, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);
    channel.envEnergyR = smoothDb(channel.envEnergyR, energyDbR, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);
    channel.envLowEnergyL = smoothDb(channel.envLowEnergyL, lowDbL, LOW_ATTACK_MS, LOW_RELEASE_MS);
    channel.envLowEnergyR = smoothDb(channel.envLowEnergyR, lowDbR, LOW_ATTACK_MS, LOW_RELEASE_MS);

    // Perceptual mapping (Musical meters are interpretive, not purely peak).
    auto computeMusicalDb = [&](float peakEnvDb, float energyEnvDb, float lowEnvDb) -> float {
        // Transient strength: how much peak stands above energy (in dB).
        const float transientDb = std::max(0.0f, peakEnvDb - energyEnvDb);
        float peakWeight = std::clamp(transientDb / 12.0f, 0.0f, 1.0f);

        // Bass-heavy material shouldn't visually "spike" like transients.
        const float bassProximity = std::clamp((lowEnvDb - (energyEnvDb - 6.0f)) / 12.0f, 0.0f, 1.0f);
        peakWeight *= (1.0f - 0.65f * bassProximity);

        return energyEnvDb + peakWeight * (peakEnvDb - energyEnvDb);
    };

    // Dual-Bar Metering (Ableton Style):
    // 1. Peak Bar (Fast/Technical): Targets pure peak envelope.
    channel.smoothedPeakL = smoothDb(channel.smoothedPeakL, channel.envPeakL, DISPLAY_ATTACK_MS, DISPLAY_RELEASE_MS);
    
    // Correlation and LUFS (already computed/smoothed in audio engine)
    channel.correlation = std::isfinite(snapshot.correlation) ? std::clamp(snapshot.correlation, -1.0f, 1.0f) : 0.0f;
    channel.sidechainPeak = sanitizeLinear(snapshot.sidechainPeak);
    channel.integratedLufs = std::isfinite(snapshot.integratedLufs)
                                 ? std::clamp(snapshot.integratedLufs, -144.0f, 12.0f)
                                 : -144.0f;
    
    channel.smoothedPeakR = smoothDb(channel.smoothedPeakR, channel.envPeakR, DISPLAY_ATTACK_MS, DISPLAY_RELEASE_MS);

    // 2. RMS Bar (Average/Body): Targets energy envelope.
    channel.smoothedRmsL = smoothDb(channel.smoothedRmsL, channel.envEnergyL, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);
    channel.smoothedRmsR = smoothDb(channel.smoothedRmsR, channel.envEnergyR, ENERGY_ATTACK_MS, ENERGY_RELEASE_MS);

    channel.smoothedPeakL = std::max(channel.smoothedPeakL, MixerMath::DB_MIN);
    channel.smoothedPeakR = std::max(channel.smoothedPeakR, MixerMath::DB_MIN);
    channel.smoothedRmsL = std::max(channel.smoothedRmsL, MixerMath::DB_MIN);
    channel.smoothedRmsR = std::max(channel.smoothedRmsR, MixerMath::DB_MIN);

    channel.smoothedPeakL = std::max(channel.smoothedPeakL, MixerMath::DB_MIN);
    channel.smoothedPeakR = std::max(channel.smoothedPeakR, MixerMath::DB_MIN);

    // Peak hold uses true peak (for gain-staging confidence).
    if (peakDbL > channel.peakHoldL) {
        channel.peakHoldL = peakDbL;
        channel.peakHoldTimerL = 0.0;
    } else {
        channel.peakHoldTimerL += deltaTime;
        if (channel.peakHoldTimerL > PEAK_HOLD_MS / 1000.0) {
            float decayCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / PEAK_DECAY_MS));
            channel.peakHoldL += (MixerMath::DB_MIN - channel.peakHoldL) * decayCoeff;
        }
    }

    if (peakDbR > channel.peakHoldR) {
        channel.peakHoldR = peakDbR;
        channel.peakHoldTimerR = 0.0;
    } else {
        channel.peakHoldTimerR += deltaTime;
        if (channel.peakHoldTimerR > PEAK_HOLD_MS / 1000.0) {
            float decayCoeff = 1.0f - std::exp(static_cast<float>(-deltaTime * 1000.0 / PEAK_DECAY_MS));
            channel.peakHoldR += (MixerMath::DB_MIN - channel.peakHoldR) * decayCoeff;
        }
    }

    // Clip latch:
    // - user clear suppresses immediate re-latch while the same continuous clip is still active
    // - once clip drops out, the next distinct clip event can latch again
    if (peakLinearL < CLIP_RELEASE_THRESHOLD) {
        channel.suppressClipRelatchL = false;
    } else if (!channel.suppressClipRelatchL) {
        channel.clipLatchL = true;
    }

    if (peakLinearR < CLIP_RELEASE_THRESHOLD) {
        channel.suppressClipRelatchR = false;
    } else if (!channel.suppressClipRelatchR) {
        channel.clipLatchR = true;
    }
}

std::vector<MixerViewModel::Destination> MixerViewModel::getAvailableDestinations(uint32_t excludeId) const
{
    std::vector<Destination> dests;
    
    // Always add Master if we aren't Master
    if (excludeId != 0) {
        dests.push_back({0, "Master"});
    }

    // Add other channels (e.g. Buses/Returns/Tracks)
    // Note: In a real matrix, might filter to only Buses, but Aestra allows Track-to-Track sends.
    for (const auto& ch : m_channels) {
        if (!ch) continue;
        if (ch->id == excludeId) continue;
        if (ch->id == 0) continue; // Handled above
        if (!canRouteTo(excludeId, ch->id)) continue;

        dests.push_back({ch->id, ch->name});
    }

    return dests;
}

void MixerViewModel::addSend(uint32_t channelId) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    
    // Auto-select a meaningful destination (Avoid Master if it's already the main Output)
    uint32_t defaultTarget = 0; // Fallback to Master
    std::string defaultName = "Master";
    
    auto available = getAvailableDestinations(channelId);
    for (const auto& dest : available) {
        // Pick the first available Send that isn't Master (0).
        // e.g., a Reverb Bus or Group Channel
        if (dest.id != 0) {
            defaultTarget = dest.id;
            defaultName = dest.name;
            break;
        }
    }
    // Contract D4: sends to master are illegal. No non-master destination
    // means no send — do not fall back to a master edge.
    if (defaultTarget == 0) {
        m_blockedRoutingWarnings[channelId] = "No send destinations available";
        return;
    }

    Audio::AudioRoute route{};
    route.targetChannelId = defaultTarget;
    route.gain = 0.25f; // -12 dB leaves headroom when adding a parallel path
    route.sidechainOnly = false;

    // Update Local Model
    ChannelViewModel::SendViewModel send{};
    send.targetId = defaultTarget;
    send.targetName = defaultName;
    send.gain = 0.25f;
    send.sidechainOnly = false;
    ch->sends.push_back(send);

    // Update Engine through the command seam (undoable, validated)
    if (auto mc = ch->channel) {
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(std::make_shared<Audio::AddSendCommand>(*m_trackManager, channelId, route));
        } else {
            mc->addSend(route);
        }
        refreshLocalSendId(ch, mc);

        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::addSidechain(uint32_t channelId) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;

    uint32_t defaultTarget = 0;
    std::string defaultName = "Master";
    auto available = getAvailableDestinations(channelId);
    for (const auto& dest : available) {
        if (dest.id != 0) {
            defaultTarget = dest.id;
            defaultName = dest.name;
            break;
        }
    }
    // Contract D4: sends (incl. sidechain) to master are illegal.
    if (defaultTarget == 0) {
        m_blockedRoutingWarnings[channelId] = "No send destinations available";
        return;
    }

    Audio::AudioRoute route{};
    route.targetChannelId = defaultTarget;
    route.gain = 1.0f;
    route.sidechainOnly = true;

    ChannelViewModel::SendViewModel send{};
    send.targetId = defaultTarget;
    send.targetName = defaultName;
    send.gain = 1.0f;
    send.sidechainOnly = true;
    ch->sends.push_back(send);

    if (auto mc = ch->channel) {
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(std::make_shared<Audio::AddSendCommand>(*m_trackManager, channelId, route));
        } else {
            mc->addSend(route);
        }
        refreshLocalSendId(ch, mc);

        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::addSend(uint32_t channelId, uint32_t targetId, bool sidechainOnly) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    if (targetId == 0) {
        m_blockedRoutingWarnings[channelId] = "Sends to master are not allowed";
        return;
    }
    if (!canRouteTo(channelId, targetId)) {
        m_blockedRoutingWarnings[channelId] = "Routing loop blocked";
        return;
    }

    ChannelViewModel::SendViewModel send{};
    send.targetId = targetId;
    if (auto* target = getChannelById(targetId)) {
        send.targetName = target->name.empty() ? "Bus" : target->name;
    } else if (targetId == 0) {
        send.targetName = "Master";
    } else {
        send.targetName = "Unknown";
    }
    send.gain = 1.0f;
    send.sidechainOnly = sidechainOnly;
    ch->sends.push_back(send);

    if (auto mc = ch->channel) {
        Audio::AudioRoute route{};
        route.targetChannelId = (targetId == 0) ? 0xFFFFFFFFu : targetId;
        route.gain = 1.0f;
        route.sidechainOnly = sidechainOnly;
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(std::make_shared<Audio::AddSendCommand>(*m_trackManager, channelId, route));
        } else {
            mc->addSend(route);
        }
        refreshLocalSendId(ch, mc);
        graphDirty.emit();
        projectModified.emit();
    }
    m_blockedRoutingWarnings.erase(channelId);
}

void MixerViewModel::toggleMute(uint32_t channelId) {
    auto* ch = getChannelById(channelId);
    if (!ch || !ch->channel) return;
    bool newMute = !ch->channel->isMuted();
    ch->channel->setMute(newMute);
    ch->muted = newMute;
    graphDirty.emit();
    projectModified.emit();
}

void MixerViewModel::toggleSolo(uint32_t channelId) {
    auto* ch = getChannelById(channelId);
    if (!ch || !ch->channel) return;
    bool newSolo = !ch->channel->isSoloed();
    ch->channel->setSolo(newSolo);
    ch->soloed = newSolo;
    graphDirty.emit();
    projectModified.emit();
}

void MixerViewModel::removeSend(uint32_t channelId, uint64_t sendId) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    const int localIndex = findLocalSendIndex(*ch, sendId);
    if (localIndex < 0) return;

    // Update Local Model
    ch->sends.erase(ch->sends.begin() + localIndex);

    // Update Engine through the command seam (undoable, identity-stable)
    if (auto mc = ch->channel) {
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(
                std::make_shared<Audio::RemoveSendCommand>(*m_trackManager, channelId, sendId));
        } else {
            mc->removeSend(sendId);
        }

        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::setSendLevel(uint32_t channelId, uint64_t sendId, float linearGain) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    const int localIndex = findLocalSendIndex(*ch, sendId);
    if (localIndex < 0) return;

    linearGain = std::isfinite(linearGain) ? std::clamp(linearGain, 0.0f, 4.0f) : 0.0f;
    ch->sends[static_cast<size_t>(localIndex)].gain = linearGain;

    // Update Engine through the command seam (undoable)
    if (auto mc = ch->channel) {
        Audio::AudioRoute route;
        if (!tryGetEngineRoute(mc, sendId, route)) {
            return;
        }
        route.gain = linearGain;
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(
                std::make_shared<Audio::EditSendCommand>(*m_trackManager, channelId, sendId, route));
        } else {
            mc->setSend(sendId, route);
        }
        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::setSendDestination(uint32_t channelId, uint64_t sendId, uint32_t targetId) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    const int localIndex = findLocalSendIndex(*ch, sendId);
    if (localIndex < 0) return;
    if (targetId == 0) {
        m_blockedRoutingWarnings[channelId] = "Sends to master are not allowed";
        return;
    }
    if (!canRouteTo(channelId, targetId)) {
        m_blockedRoutingWarnings[channelId] = "Routing loop blocked";
        return;
    }

    ch->sends[static_cast<size_t>(localIndex)].targetId = targetId;
    m_blockedRoutingWarnings.erase(channelId);
    
    // Resolve name
    if (targetId == 0) {
        ch->sends[static_cast<size_t>(localIndex)].targetName = "Master";
    } else {
        auto* target = getChannelById(targetId);
        ch->sends[static_cast<size_t>(localIndex)].targetName = target ? target->name : "Unknown";
    }

    // Update Engine through the command seam (undoable, validated)
    if (auto mc = ch->channel) {
        // Normalize 0 to 0xFFFFFFFF for engine master
        const uint32_t engineId = (targetId == 0) ? 0xFFFFFFFFu : targetId;
        Audio::AudioRoute route;
        if (!tryGetEngineRoute(mc, sendId, route)) {
            return;
        }
        route.targetChannelId = engineId;
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(
                std::make_shared<Audio::EditSendCommand>(*m_trackManager, channelId, sendId, route));
        } else {
            mc->setSend(sendId, route);
        }

        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::setSendPostFader(uint32_t channelId, uint64_t sendId, bool postFader) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    const int localIndex = findLocalSendIndex(*ch, sendId);
    if (localIndex < 0) return;

    ch->sends[static_cast<size_t>(localIndex)].postFader = postFader;

    if (auto mc = ch->channel) {
        Audio::AudioRoute route;
        if (!tryGetEngineRoute(mc, sendId, route)) {
            return;
        }
        route.postFader = postFader;
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(
                std::make_shared<Audio::EditSendCommand>(*m_trackManager, channelId, sendId, route));
        } else {
            mc->setSend(sendId, route);
        }
        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::setSendSidechainOnly(uint32_t channelId, uint64_t sendId, bool sidechainOnly) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;
    const int localIndex = findLocalSendIndex(*ch, sendId);
    if (localIndex < 0) return;

    ch->sends[static_cast<size_t>(localIndex)].sidechainOnly = sidechainOnly;

    if (auto mc = ch->channel) {
        Audio::AudioRoute route;
        if (!tryGetEngineRoute(mc, sendId, route)) {
            return;
        }
        route.sidechainOnly = sidechainOnly;
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(
                std::make_shared<Audio::EditSendCommand>(*m_trackManager, channelId, sendId, route));
        } else {
            mc->setSend(sendId, route);
        }
        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::setMainOutputDestination(uint32_t channelId, uint32_t targetId) {
    auto* ch = getChannelById(channelId);
    if (!ch || ch->id == 0) return;
    if (!canRouteTo(channelId, targetId)) {
        m_blockedRoutingWarnings[channelId] = "Routing loop blocked";
        return;
    }

    ch->mainOutputId = targetId;
    ch->masterSendEnabled = (targetId == 0);
    m_blockedRoutingWarnings.erase(channelId);
    ch->routeName = (targetId == 0) ? "Master" : "Unknown";

    if (targetId != 0) {
        if (auto* target = getChannelById(targetId)) {
            ch->routeName = target->name;
        } else {
            ch->routeName = "Unknown (" + std::to_string(targetId) + ")";
        }
    }

    if (auto mc = ch->channel) {
        const uint32_t engineId = (targetId == 0) ? 0xFFFFFFFFu : targetId;
        if (m_commandHistory && m_trackManager) {
            m_commandHistory->pushAndExecute(
                std::make_shared<Audio::SetMainOutputCommand>(*m_trackManager, channelId, engineId));
        } else {
            mc->setMainOutputId(engineId);
        }
        graphDirty.emit();
        projectModified.emit();
    }
}


// Returns the engine-side route for a sendId, or false when the engine no
// longer holds it (undo/redo or a rejected command can leave the local
// mirror ahead of the engine). Callers must never index getSends() with an
// unchecked findSendIndex result.
bool MixerViewModel::tryGetEngineRoute(const Audio::MixerChannel* mc, uint64_t sendId, Audio::AudioRoute& out) const {
    if (!mc || sendId == 0) {
        return false;
    }
    const int engineIndex = mc->findSendIndex(sendId);
    if (engineIndex < 0) {
        return false;
    }
    out = mc->getSends()[static_cast<size_t>(engineIndex)];
    return true;
}

int MixerViewModel::findLocalSendIndex(const ChannelViewModel& ch, uint64_t sendId) const {
    for (size_t i = 0; i < ch.sends.size(); ++i) {
        if (ch.sends[i].sendId == sendId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void MixerViewModel::refreshLocalSendId(ChannelViewModel* ch, const Audio::MixerChannel* mc) {
    if (!ch || !mc) {
        return;
    }
    const auto engineSends = mc->getSends();
    if (engineSends.empty()) {
        return;
    }
    // Match by identity, not position: the local entry that still has no id
    // is the one the command just created. A rejected command leaves the
    // engine list unchanged, so the mirror below also pops the phantom.
    for (auto& local : ch->sends) {
        if (local.sendId == 0) {
            local.sendId = engineSends.back().sendId;
            break;
        }
    }
    if (ch->sends.size() > engineSends.size()) {
        // The command was rejected (cycle/master) after the local push: drop
        // the phantom so the mirror never outlives the engine.
        ch->sends.resize(engineSends.size());
    }
}

std::string MixerViewModel::getRoutingWarning(uint32_t channelId) const {
    const auto* ch = getChannelById(channelId);
    if (!ch || ch->id == 0) return {};
    auto blockedIt = m_blockedRoutingWarnings.find(channelId);
    if (blockedIt != m_blockedRoutingWarnings.end()) {
        return blockedIt->second;
    }

    std::unordered_map<uint32_t, int> audibleRouteCounts;
    auto addAudibleTarget = [&](uint32_t targetId) {
        audibleRouteCounts[targetId] += 1;
    };

    const uint32_t mainTarget = (ch->masterSendEnabled || ch->mainOutputId == 0) ? 0u : ch->mainOutputId;
    addAudibleTarget(mainTarget);

    for (const auto& send : ch->sends) {
        if (send.muted || send.sidechainOnly) continue;
        addAudibleTarget(send.targetId);
    }

    int duplicateDestinations = 0;
    for (const auto& entry : audibleRouteCounts) {
        if (entry.second > 1) {
            duplicateDestinations++;
        }
    }

    if (duplicateDestinations <= 0) return {};
    if (duplicateDestinations == 1) return "Duplicate audible route";
    return std::to_string(duplicateDestinations) + " duplicate audible routes";
}

bool MixerViewModel::canRouteTo(uint32_t sourceId, uint32_t targetId) const {
    // Routing Contract D1: TrackManager is the single validation authority.
    if (m_trackManager) {
        return m_trackManager->canRouteTo(sourceId, targetId);
    }
    // Degraded path (no TrackManager wired): VM-local topology check.
    return !routeWouldCreateCycle(sourceId, targetId);
}

bool MixerViewModel::routeWouldCreateCycle(uint32_t sourceId, uint32_t targetId) const {
    if (sourceId == 0 || targetId == 0) return false;
    if (sourceId == targetId) return true;
    return hasRoutePath(targetId, sourceId);
}

bool MixerViewModel::hasRoutePath(uint32_t fromId, uint32_t targetId) const {
    if (fromId == 0 || targetId == 0) return false;

    std::vector<uint32_t> stack{fromId};
    std::unordered_map<uint32_t, bool> visited;

    while (!stack.empty()) {
        const uint32_t currentId = stack.back();
        stack.pop_back();

        if (currentId == targetId) {
            return true;
        }
        if (visited[currentId]) {
            continue;
        }
        visited[currentId] = true;

        const auto* ch = getChannelById(currentId);
        if (!ch) {
            continue;
        }

        if (!ch->masterSendEnabled && ch->mainOutputId != 0 && !visited[ch->mainOutputId]) {
            stack.push_back(ch->mainOutputId);
        }

        for (const auto& send : ch->sends) {
            if (send.targetId == 0 || visited[send.targetId]) {
                continue;
            }
            stack.push_back(send.targetId);
        }
    }

    return false;
}



void MixerViewModel::setInsertBypass(uint32_t channelId, int slotIndex, bool bypassed) {
    auto* ch = getChannelById(channelId);
    if (!ch || slotIndex < 0 || slotIndex >= static_cast<int>(ch->inserts.size())) return;

    // Update Local
    ch->inserts[slotIndex].bypassed = bypassed;
    ch->inserts[slotIndex].bypassDirty = true; // Mark as pending sync

    // Update Engine
    if (auto mc = ch->channel) {
        mc->getEffectChain().setSlotBypassed(slotIndex, bypassed);
        if (!bypassed) {
            if (auto plugin = mc->getEffectChain().getPlugin(static_cast<size_t>(slotIndex))) {
                plugin->resetWatchdog();
                if (!plugin->isActive()) {
                    plugin->activate();
                }
            }
        }
        graphDirty.emit();
        projectModified.emit();
    }
}

void MixerViewModel::setInsertMix(uint32_t channelId, int slotIndex, float mix) {
    auto* ch = getChannelById(channelId);
    if (!ch || slotIndex < 0 || slotIndex >= static_cast<int>(ch->inserts.size())) return;

    // Update Local
    ch->inserts[slotIndex].mix = mix;

    // Update Engine
    if (auto mc = ch->channel) {
        mc->getEffectChain().setSlotDryWetMix(slotIndex, mix);
        graphDirty.emit();
         // Often mix changes don't need project dirty flag every frame, 
         // but for now we can trigger it or handle throttling elsewhere.
    }
}

void MixerViewModel::moveInsert(uint32_t channelId, int fromSlot, int toSlot) {
    auto* ch = getChannelById(channelId);
    if (!ch) return;

    // Validate bounds
    if (fromSlot < 0 || fromSlot >= (int)ch->inserts.size() || 
        toSlot < 0 || toSlot >= (int)ch->inserts.size()) return;
    
    if (fromSlot == toSlot) return;

    // Update Local (Swap/Move)
    std::swap(ch->inserts[fromSlot], ch->inserts[toSlot]);

    // Update Engine
    if (auto mc = ch->channel) {
        auto& chain = mc->getEffectChain();
        const auto* targetSlotPtr = chain.getSlot(toSlot);
        
        // Since we already swapped locally, we assume engine handles it.
        // We check target slot state *before* logic if possible, but weak ptr makes it tricky.
        // Let's assume we want to swap if target has plugin, move if empty.
        
        // However, EffectChain API is locked.
        // If we call movePlugin(from, to) and it fails (occupied), we should try swapPlugins.
        // But checking `isEmpty()` first is safer.
        
        bool targetEmpty = (!targetSlotPtr || targetSlotPtr->isEmpty());
        
        if (targetEmpty) {
            chain.movePlugin(fromSlot, toSlot);
        } else {
            chain.swapPlugins(fromSlot, toSlot);
        }
        
        graphDirty.emit();
        projectModified.emit();
    }
}

// ... inside removeInsert ...

void MixerViewModel::removeInsert(uint32_t channelId, int slot) {
    if (auto* ch = getChannelById(channelId)) {
        // Validate bounds
        if (slot < 0 || slot >= static_cast<int>(ch->inserts.size())) {
            char logBuf[128];
            std::snprintf(logBuf, sizeof(logBuf), "[MixerVM] removeInsert: Invalid slot %d for Ch %u", slot, channelId);
            Aestra::Log::warning(logBuf);
            return;
        }

        // Optimistic update
        ch->inserts[slot] = ChannelViewModel::InsertViewModel{};
        
        // Mark as pending removal so sync doesn't overwrite it immediately
        ch->inserts[slot].pendingRemoval = true;
        ch->inserts[slot].isEmpty = true; 
        
        if (auto mc = ch->channel) {
            auto& chain = mc->getEffectChain();
            if (m_commandHistory) {
                m_commandHistory->pushAndExecute(
                    std::make_shared<Audio::RemovePluginCommand>(*mc, static_cast<size_t>(slot)));
            } else {
                chain.removePlugin(slot);
            }

            graphDirty.emit();
            projectModified.emit();
        }
    }
}

} // namespace Aestra
