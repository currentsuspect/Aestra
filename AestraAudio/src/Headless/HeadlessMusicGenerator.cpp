// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Headless/HeadlessMusicGenerator.h"
#include "AestraLog.h"
#include "Core/AudioGraphBuilder.h"
#include "Models/UnitManager.h"
#include "Models/PatternSource.h"
#include "Plugin/PluginManager.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <numeric>

namespace Aestra {
namespace Audio {

//==============================================================================
// Lifecycle
//==============================================================================

HeadlessMusicGenerator::HeadlessMusicGenerator(AudioEngine& engine, TrackManager& trackManager)
    : m_engine(engine)
    , m_trackManager(trackManager)
{
}

//==============================================================================
// Project Setup
//==============================================================================

HeadlessMusicGenerator& HeadlessMusicGenerator::createProject(const std::string& name) {
    m_projectName = name;
    m_patterns.clear();
    m_playlistClips.clear();
    m_channelNames.clear();
    m_currentPatternName.clear();
    m_committed = false;
    m_toneSamplePath.clear();
    m_laneUnits.clear();
    m_laneIds.clear();
    
    // Reset track manager
    m_trackManager.getPlaylistModel().clear();
    
    Log::info("[HeadlessGenerator] Created project: " + name);
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::setTempo(double bpm) {
    m_tempo = bpm;
    m_trackManager.getPlaylistModel().setBPM(bpm);
    m_trackManager.getTimelineClock().setTempo(bpm);
    m_trackManager.getPatternPlaybackEngine().flush();
    m_engine.setBPM(static_cast<float>(bpm));
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::setSampleRate(uint32_t sampleRate) {
    m_sampleRate = sampleRate;
    m_engine.setSampleRate(sampleRate);
    m_trackManager.setOutputSampleRate(static_cast<double>(sampleRate));
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::setTimeSignature(uint8_t numerator, uint8_t denominator) {
    m_timeSigNum = numerator;
    m_timeSigDenom = denominator;
    return *this;
}

//==============================================================================
// Pattern Creation
//==============================================================================

HeadlessMusicGenerator& HeadlessMusicGenerator::createPattern(const std::string& name, uint32_t length) {
    PatternData pattern;
    pattern.name = name;
    pattern.length = length;
    
    m_patterns[name] = std::move(pattern);
    m_currentPatternName = name;
    
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::addNote(uint8_t step, uint8_t pitch, uint8_t velocity, double duration) {
    auto* pattern = getCurrentPattern();
    if (!pattern) {
        Log::warning("[HeadlessGenerator] No current pattern, call createPattern() first");
        return *this;
    }
    
    if (step >= pattern->length) {
        Log::warning("[HeadlessGenerator] Step " + std::to_string(step) + " out of range for pattern '" + pattern->name + "'");
        return *this;
    }
    
    pattern->notes.emplace_back(step, pitch, velocity, duration);
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::addChord(uint8_t step, const std::vector<uint8_t>& pitches, 
                                                          uint8_t velocity, double duration) {
    for (uint8_t pitch : pitches) {
        addNote(step, pitch, velocity, duration);
    }
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::addHit(uint8_t step) {
    return addNote(step, 60, 100, 0.25);  // Default C4 hit
}

HeadlessMusicGenerator& HeadlessMusicGenerator::fillEvery(uint8_t interval, uint8_t pitch, uint8_t velocity) {
    auto* pattern = getCurrentPattern();
    if (!pattern) return *this;
    
    for (uint32_t step = 0; step < pattern->length; step += interval) {
        addNote(static_cast<uint8_t>(step), pitch, velocity, 0.5);
    }
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::addEuclideanRhythm(uint8_t pulses, uint8_t steps, 
                                                                    uint8_t pitch, uint8_t velocity) {
    if (pulses == 0 || steps == 0 || pulses > steps) {
        Log::warning("[HeadlessGenerator] Invalid Euclidean rhythm parameters");
        return *this;
    }
    
    auto* pattern = getCurrentPattern();
    if (!pattern) return *this;
    
    // Generate Euclidean rhythm using Bresenham's algorithm
    std::vector<bool> rhythm(steps, false);
    int error = steps / 2;
    int step = 0;
    
    for (int i = 0; i < pulses; ++i) {
        rhythm[step] = true;
        step += steps / pulses;
        error += steps % pulses;
        if (error >= pulses) {
            step++;
            error -= pulses;
        }
    }
    
    // Add notes where rhythm is true
    for (size_t i = 0; i < rhythm.size(); ++i) {
        if (rhythm[i]) {
            addNote(static_cast<uint8_t>(i), pitch, velocity, 0.25);
        }
    }
    
    return *this;
}

//==============================================================================
// Playlist Assembly
//==============================================================================

HeadlessMusicGenerator& HeadlessMusicGenerator::addClipToPlaylist(const std::string& patternName, 
                                                                   double startBeat, 
                                                                   double durationBeats,
                                                                   uint32_t laneIndex) {
    m_playlistClips.push_back({patternName, startBeat, durationBeats, laneIndex});
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::repeatClip(const std::string& patternName, 
                                                            double startBeat, 
                                                            double durationBeats,
                                                            uint32_t repetitions,
                                                            uint32_t laneIndex) {
    for (uint32_t i = 0; i < repetitions; ++i) {
        addClipToPlaylist(patternName, startBeat + (i * durationBeats), durationBeats, laneIndex);
    }
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::addSilence(double startBeat, double durationBeats) {
    // Just adds empty space - no clip needed
    // The playlist will naturally have silence where there are no clips
    (void)startBeat;
    (void)durationBeats;
    return *this;
}

//==============================================================================
// Mixer
//==============================================================================

HeadlessMusicGenerator& HeadlessMusicGenerator::addChannel(const std::string& name) {
    m_trackManager.addChannel(name);
    m_channelNames.push_back(name);
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::setChannelVolume(uint32_t channelIndex, float volume) {
    auto channel = m_trackManager.getChannel(channelIndex);
    if (channel) {
        channel->setVolume(std::clamp(volume, 0.0f, 1.0f));
    }
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::setChannelPan(uint32_t channelIndex, float pan) {
    auto channel = m_trackManager.getChannel(channelIndex);
    if (channel) {
        channel->setPan(std::clamp(pan, -1.0f, 1.0f));
    }
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::muteChannel(uint32_t channelIndex, bool muted) {
    auto channel = m_trackManager.getChannel(channelIndex);
    if (channel) {
        channel->setMute(muted);
    }
    return *this;
}

//==============================================================================
// Presets
//==============================================================================

HeadlessMusicGenerator& HeadlessMusicGenerator::loadFourOnTheFloor(const std::string& name) {
    createPattern(name, 16)
        .addNote(0, 36, 110, 0.5)   // C2 - Kick
        .addNote(4, 36, 110, 0.5)
        .addNote(8, 36, 110, 0.5)
        .addNote(12, 36, 110, 0.5);
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::loadHiHatPattern(const std::string& name) {
    createPattern(name, 16)
        .addNote(0, 42, 80, 0.25)   // F#4 - Closed HH
        .addNote(2, 42, 60, 0.25)
        .addNote(4, 42, 80, 0.25)
        .addNote(6, 42, 60, 0.25)
        .addNote(8, 42, 80, 0.25)
        .addNote(10, 42, 60, 0.25)
        .addNote(12, 42, 80, 0.25)
        .addNote(14, 42, 60, 0.25);
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::loadBassline(const std::string& name) {
    createPattern(name, 16)
        .addNote(0, 24, 100, 1.0)   // C1
        .addNote(4, 24, 90, 1.0)
        .addNote(8, 31, 100, 1.0)   // G1
        .addNote(12, 29, 90, 1.0);  // F1
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::generateFromTemplate(const std::string& templateName, 
                                                                       uint32_t durationBars) {
    if (templateName == "Techno") {
        setTempo(130);
        
        // Create patterns
        loadFourOnTheFloor("Kick");
        loadHiHatPattern("HiHat");
        createPattern("Clap", 16)
            .addNote(4, 39, 100, 0.5)
            .addNote(12, 39, 100, 0.5);
        loadBassline("Bass");
        
        // Assemble playlist
        double barBeats = static_cast<double>(m_timeSigNum);
        for (uint32_t bar = 0; bar < durationBars; ++bar) {
            double startBeat = bar * barBeats;
            addClipToPlaylist("Kick", startBeat, barBeats, 0);
            addClipToPlaylist("HiHat", startBeat, barBeats, 1);
            addClipToPlaylist("Clap", startBeat, barBeats, 2);
            addClipToPlaylist("Bass", startBeat, barBeats, 3);
        }
        
        // Setup mixer
        addChannel("Kick Channel");
        addChannel("HiHat Channel");
        addChannel("Clap Channel");
        addChannel("Bass Channel");
        
    } else if (templateName == "House") {
        setTempo(125);
        
        loadFourOnTheFloor("Kick");
        createPattern("OffBeatBass", 16)
            .addNote(2, 33, 100, 0.5)
            .addNote(6, 33, 100, 0.5)
            .addNote(10, 33, 100, 0.5)
            .addNote(14, 33, 100, 0.5);
        
        double barBeats = static_cast<double>(m_timeSigNum);
        for (uint32_t bar = 0; bar < durationBars; ++bar) {
            double startBeat = bar * barBeats;
            addClipToPlaylist("Kick", startBeat, barBeats, 0);
            addClipToPlaylist("OffBeatBass", startBeat, barBeats, 1);
        }
        
        addChannel("Kick");
        addChannel("Bass");
        
    } else if (templateName == "Minimal") {
        setTempo(128);
        
        // Minimal kick - sparse
        createPattern("MinKick", 16)
            .addNote(0, 36, 110, 0.5)
            .addNote(10, 36, 90, 0.5);
        
        // Click hi-hat
        createPattern("Click", 16)
            .addEuclideanRhythm(5, 16, 42, 70);
        
        double barBeats = static_cast<double>(m_timeSigNum);
        for (uint32_t bar = 0; bar < durationBars; ++bar) {
            double startBeat = bar * barBeats;
            addClipToPlaylist("MinKick", startBeat, barBeats, 0);
            addClipToPlaylist("Click", startBeat, barBeats, 1);
        }
        
        addChannel("Kick");
        addChannel("Perc");
    } else {
        Log::warning("[HeadlessGenerator] Unknown template: " + templateName);
    }
    
    return *this;
}

//==============================================================================
// Export
//==============================================================================

bool HeadlessMusicGenerator::exportTo(const std::string& outputPath,
                                       uint32_t sampleRate,
                                       AudioExporter::BitDepth bitDepth,
                                       AudioExporter::RenderScope scope) {
    return exportTo(outputPath, nullptr, sampleRate, bitDepth);
}

bool HeadlessMusicGenerator::exportTo(const std::string& outputPath,
                                       std::function<void(float)> progressCallback,
                                       uint32_t sampleRate,
                                       AudioExporter::BitDepth bitDepth) {
    // Validate
    std::string error = validate();
    if (!error.empty()) {
        Log::error("[HeadlessGenerator] Validation failed: " + error);
        return false;
    }

    // Reconcile the render sample rate before committing so the sampler
    // instruments are initialized at the same rate the engine renders at (the
    // exportTo sampleRate argument overrides the configured project rate).
    const uint32_t effectiveRate = sampleRate > 0 ? sampleRate : m_sampleRate;
    m_sampleRate = effectiveRate;

    // Commit patterns to track manager
    commitPatternsToManager();

    // Commit playlist
    commitPlaylistToModel();

    // Remove the temp tone sample on every exit path, including an exception
    // out of render() (the sampler decoded it into memory at load time).
    struct ToneSampleCleanup {
        const std::string& path;
        ~ToneSampleCleanup() {
            if (!path.empty()) {
                std::error_code ec;
                std::filesystem::remove(path, ec);
            }
        }
    } toneCleanup{m_toneSamplePath};

    // --- Wire the export engine to render the committed timeline ---------------
    // AudioExporter::render drives AudioEngine::processBlock — the same path the
    // live engine uses — so the committed MIDI clips synthesize through their
    // sampler units (live/export parity, AGENTS.md §20).
    //
    // setTrackManager stores a weak_ptr, so `borrowedTrackManager` must stay
    // alive for the whole render. The guard restores the engine to an unwired
    // state on every exit path (including failures) — the engine keeps a raw
    // UnitManager*, an owned slot map and a graph copy, none of which may
    // outlive this call. The render is fully synchronous: no callback or
    // retained object can touch this wiring once exportTo returns.
    // Restores everything this render mutates on every exit path, so a
    // synchronous export leaves the caller's engine and session unchanged:
    //  - engine wiring (weak TrackManager ref, raw UnitManager*, owned slot map,
    //    graph copy, pattern-engine pointer) is cleared;
    //  - the pattern-playback engine is flushed so the timeline instances this
    //    render scheduled do not leak into the caller's TrackManager.
    // The transport flags/position are never touched (see
    // scheduleTimelineForOfflineRender below), so there is nothing else to undo.
    struct RenderStateGuard {
        AudioEngine& engine;
        TrackManager& trackManager;
        ~RenderStateGuard() {
            engine.setGraph(AudioGraph{});
            engine.setPatternPlaybackEngine(nullptr);
            engine.setUnitManager(nullptr);
            engine.setChannelSlotMap(nullptr);
            engine.setTrackManager(nullptr);
            // Remove, don't rewind: this render's instances must not survive into the
            // caller's session (flush() would leave them scheduled and merely restarted).
            trackManager.getPatternPlaybackEngine().clearInstances();
        }
    };

    // Non-owning shared_ptr: setTrackManager currently requires shared ownership,
    // but the caller owns m_trackManager and guarantees it outlives this render.
    // The no-op deleter means the borrowed TrackManager is never freed here.
    std::shared_ptr<TrackManager> borrowedTrackManager(&m_trackManager, [](TrackManager*) {});
    RenderStateGuard renderGuard{m_engine, m_trackManager}; // destroyed before borrowedTrackManager

    m_engine.setSampleRate(effectiveRate);
    m_engine.setBufferConfig(512, 2);
    m_engine.setBPM(static_cast<float>(m_tempo));
    m_engine.setTrackManager(borrowedTrackManager);
    m_engine.setUnitManager(&m_trackManager.getUnitManager());
    m_engine.setPatternPlaybackEngine(&m_trackManager.getPatternPlaybackEngine());
    m_trackManager.buildAndShareSlotMap();
    if (auto slotMap = m_trackManager.getChannelSlotMapShared()) {
        m_engine.setChannelSlotMap(slotMap);
    }
    m_engine.setGraph(AudioGraphBuilder::buildFromTrackManager(m_trackManager));
    m_engine.initialize();

    // MIDI clips reach their units through the pattern-playback engine
    // (AudioEngine::processBlock pops scheduled notes into unit MIDI routes).
    // Schedule the committed timeline into it WITHOUT starting live transport —
    // the exporter drives the engine's own transport, so we must not mutate the
    // caller's playing flag / position. clearInstances() first removes any prior
    // contents; the render guard clears again on exit so these instances don't leak.
    // flush() cannot serve here — it only REWINDS active instances (re-emit from the
    // top, which a loop restart wants), so anything already scheduled would have been
    // rendered into the export alongside the timeline we just asked for.
    m_trackManager.getPatternPlaybackEngine().clearInstances();
    m_trackManager.scheduleTimelineForOfflineRender(0.0);

    // Setup exporter
    AudioExporter exporter(m_engine, m_trackManager);
    
    if (progressCallback) {
        exporter.setProgressCallback(progressCallback);
    }
    
    AudioExporter::Config config;
    config.outputPath = outputPath;
    config.sampleRate = sampleRate > 0 ? sampleRate : m_sampleRate;
    config.bitDepth = bitDepth;
    config.scope = AudioExporter::RenderScope::FullSong;
    
    // Render
    Log::info("[HeadlessGenerator] Exporting to: " + outputPath);
    auto result = exporter.render(config); // toneCleanup removes the temp sample on scope exit

    if (result.success) {
        Log::info("[HeadlessGenerator] Export complete: " + 
                  std::to_string(result.durationSeconds) + "s, peak: " +
                  std::to_string(result.peakDb) + " dB");
        return true;
    } else {
        Log::error("[HeadlessGenerator] Export failed: " + result.errorMessage);
        return false;
    }
}

//==============================================================================
// Validation & Debug
//==============================================================================

std::string HeadlessMusicGenerator::validate() const {
    if (m_projectName.empty()) {
        return "No project name set";
    }
    
    if (m_patterns.empty()) {
        return "No patterns created";
    }
    
    if (m_playlistClips.empty()) {
        return "No clips added to playlist";
    }
    
    // Check all playlist clips reference valid patterns
    for (const auto& clip : m_playlistClips) {
        if (m_patterns.find(clip.patternName) == m_patterns.end()) {
            return "Playlist references unknown pattern: " + clip.patternName;
        }
    }
    
    return "";
}

void HeadlessMusicGenerator::printInfo() const {
    Log::info("[HeadlessGenerator] Project: " + m_projectName);
    Log::info("[HeadlessGenerator] Tempo: " + std::to_string(m_tempo) + " BPM");
    Log::info("[HeadlessGenerator] Sample Rate: " + std::to_string(m_sampleRate));
    Log::info("[HeadlessGenerator] Patterns: " + std::to_string(m_patterns.size()));
    
    for (const auto& [name, pattern] : m_patterns) {
        Log::info("[HeadlessGenerator]   - " + name + ": " + 
                  std::to_string(pattern.notes.size()) + " notes");
    }
    
    Log::info("[HeadlessGenerator] Playlist clips: " + std::to_string(m_playlistClips.size()));
    Log::info("[HeadlessGenerator] Duration: " + std::to_string(getDurationBeats()) + " beats");
}

double HeadlessMusicGenerator::getDurationBeats() const {
    double maxEnd = 0.0;
    for (const auto& clip : m_playlistClips) {
        double end = clip.startBeat + clip.durationBeats;
        maxEnd = std::max(maxEnd, end);
    }
    return maxEnd;
}

//==============================================================================
// Internal
//==============================================================================

HeadlessMusicGenerator::PatternData* HeadlessMusicGenerator::getCurrentPattern() {
    if (m_currentPatternName.empty()) return nullptr;
    return findPattern(m_currentPatternName);
}

HeadlessMusicGenerator::PatternData* HeadlessMusicGenerator::findPattern(const std::string& name) {
    auto it = m_patterns.find(name);
    if (it != m_patterns.end()) return &it->second;
    return nullptr;
}

// Steps map to sixteenth notes — 16 steps per 4/4 bar — matching the set_steps
// Muse command and the header's fluent-API examples.
static constexpr double kStepBeats = 0.25;

bool HeadlessMusicGenerator::prepareToneSample() {
    // A short synthetic sine the built-in sampler loads as its source and
    // pitch-shifts per MIDI note. Written to a unique temp file because the
    // sampler loads by path (UnitManager::setUnitAudioClip -> loadSample).
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path dir = fs::temp_directory_path(ec);
    if (ec) {
        Log::error("[HeadlessGenerator] No temp directory for tone sample: " + ec.message());
        return false;
    }
    const fs::path path =
        dir / ("aestra_headless_tone_" + std::to_string(reinterpret_cast<uintptr_t>(this)) + ".wav");

    const uint32_t rate = m_sampleRate > 0 ? m_sampleRate : 48000u;
    const uint32_t frames = rate / 2; // 0.5 s
    const double freq = 440.0;
    const double twoPi = 6.283185307179586;

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        Log::error("[HeadlessGenerator] Cannot write tone sample: " + path.string());
        return false;
    }
    auto w16 = [&](uint16_t v) { out.put(static_cast<char>(v & 0xff)); out.put(static_cast<char>((v >> 8) & 0xff)); };
    auto w32 = [&](uint32_t v) {
        out.put(static_cast<char>(v & 0xff));
        out.put(static_cast<char>((v >> 8) & 0xff));
        out.put(static_cast<char>((v >> 16) & 0xff));
        out.put(static_cast<char>((v >> 24) & 0xff));
    };
    const uint32_t dataBytes = frames * 2; // mono, 16-bit
    out.write("RIFF", 4); w32(36 + dataBytes); out.write("WAVE", 4);
    out.write("fmt ", 4); w32(16); w16(1); w16(1); w32(rate); w32(rate * 2); w16(2); w16(16);
    out.write("data", 4); w32(dataBytes);
    for (uint32_t i = 0; i < frames; ++i) {
        // Short fades at both ends so the looped/one-shot sample has no click.
        double env = 1.0;
        const uint32_t fade = rate / 100; // 10 ms
        if (i < fade) env = static_cast<double>(i) / fade;
        else if (i >= frames - fade) env = static_cast<double>(frames - i) / fade;
        const double s = std::sin(twoPi * freq * static_cast<double>(i) / rate) * 0.8 * env;
        w16(static_cast<uint16_t>(static_cast<int16_t>(std::clamp(s, -1.0, 1.0) * 32767.0)));
    }
    if (!out.good()) {
        Log::error("[HeadlessGenerator] Failed writing tone sample: " + path.string());
        return false;
    }
    m_toneSamplePath = path.string();
    return true;
}

uint64_t HeadlessMusicGenerator::ensureLaneInstrument(uint32_t laneIndex) {
    if (auto it = m_laneUnits.find(laneIndex); it != m_laneUnits.end()) {
        return it->second;
    }

    auto& um = m_trackManager.getUnitManager();
    const std::string label = "Lane " + std::to_string(laneIndex + 1);

    // Mixer channel that carries this lane's instrument to the master bus.
    auto* mixerChannel = m_trackManager.addChannel(label);

    // Playlist lane the clips are placed on.
    PlaylistLaneID laneId = m_trackManager.getPlaylistModel().createLane(label);
    m_laneIds[laneIndex] = laneId;

    // Built-in sampler — the only always-available (license-free) instrument.
    auto& pluginManager = PluginManager::getInstance();
    pluginManager.initialize();
    auto sampler = pluginManager.createInstanceById("com.Aestrastudios.sampler");
    if (!sampler) {
        Log::error("[HeadlessGenerator] Failed to create built-in sampler instrument");
        return 0;
    }
    sampler->initialize(m_sampleRate > 0 ? m_sampleRate : 48000u, 512);
    sampler->activate();

    const UnitID unit = um.createUnit("Instrument " + std::to_string(laneIndex + 1), UnitType::Sampler);
    um.attachPlugin(unit, "com.Aestrastudios.sampler", sampler);
    um.setUnitEnabled(unit, true);
    if (!m_toneSamplePath.empty()) {
        um.setUnitAudioClip(unit, m_toneSamplePath); // loads the tone into the sampler
    }
    um.setUnitMixerChannel(unit, mixerChannel ? mixerChannel->getChannelId() : MASTER_MIXER_CHANNEL_ID);
    um.assignUnitToTimelineLane(unit, static_cast<int>(laneIndex));

    m_laneUnits[laneIndex] = unit;
    return unit;
}

void HeadlessMusicGenerator::commitPatternsToManager() {
    // Commit once per project: re-committing would append duplicate lanes,
    // units and clips to the TrackManager on a second exportTo() call. State is
    // reset by createProject(). The MIDI PatternSources themselves are created
    // in commitPlaylistToModel(): each note routes to the sampler unit backing
    // the lane its clip is placed on, so the pattern can only be built
    // once the placement (and thus the lane instrument) is known. Here we just
    // prepare the shared sampler source.
    if (m_committed) {
        return;
    }
    if (!prepareToneSample()) {
        Log::warning("[HeadlessGenerator] Tone sample unavailable — render will be silent");
    }
    Log::info("[HeadlessGenerator] Prepared " + std::to_string(m_patterns.size()) +
              " patterns for placement");
}

void HeadlessMusicGenerator::commitPlaylistToModel() {
    if (m_committed) {
        return;
    }
    auto& patternManager = m_trackManager.getPatternManager();
    auto& playlist = m_trackManager.getPlaylistModel();

    size_t committed = 0;
    for (const auto& clip : m_playlistClips) {
        auto patternIt = m_patterns.find(clip.patternName);
        if (patternIt == m_patterns.end()) {
            continue; // validate() already rejected unknown patterns
        }
        const PatternData& data = patternIt->second;

        const uint64_t unitId = ensureLaneInstrument(clip.laneIndex);
        if (unitId == 0) {
            continue;
        }

        // Convert the step-grid notes to beat-domain MidiNotes routed to the
        // lane's sampler unit.
        MidiPayload payload;
        payload.notes.reserve(data.notes.size());
        for (const auto& n : data.notes) {
            MidiNote mn;
            mn.pitch = n.pitch;
            mn.startBeat = static_cast<double>(n.step) * kStepBeats;
            mn.durationBeats = n.duration * kStepBeats;
            mn.velocity = std::clamp(static_cast<float>(n.velocity) / 127.0f, 0.0f, 1.0f);
            mn.unitId = unitId;
            payload.notes.push_back(mn);
        }

        const double lengthBeats = static_cast<double>(data.length) * kStepBeats;
        const PatternID patternId = patternManager.createMidiPattern(clip.patternName, lengthBeats, payload);

        auto laneIt = m_laneIds.find(clip.laneIndex);
        if (laneIt == m_laneIds.end()) {
            continue;
        }
        playlist.addClipFromPattern(laneIt->second, patternId, clip.startBeat, clip.durationBeats);
        ++committed;
    }

    m_committed = true;
    Log::info("[HeadlessGenerator] Committed " + std::to_string(committed) + " clips to playlist");
}

} // namespace Audio
} // namespace Aestra
