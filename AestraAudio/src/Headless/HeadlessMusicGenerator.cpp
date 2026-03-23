// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Headless/HeadlessMusicGenerator.h"
#include "AestraLog.h"
#include <algorithm>
#include <cmath>
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
    
    // Reset track manager
    m_trackManager.getPlaylistModel().clear();
    
    Log::info("[HeadlessGenerator] Created project: " + name);
    return *this;
}

HeadlessMusicGenerator& HeadlessMusicGenerator::setTempo(double bpm) {
    m_tempo = bpm;
    // m_trackManager.getTimelineClock().setTempo(bpm);  // TODO: Set tempo on track manager
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
    
    // Commit patterns to track manager
    commitPatternsToManager();
    
    // Commit playlist
    commitPlaylistToModel();
    
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
    auto result = exporter.render(config);
    
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

void HeadlessMusicGenerator::commitPatternsToManager() {
    auto& patternManager = m_trackManager.getPatternManager();
    
    // TODO: Convert PatternData to PatternSource and add to manager
    // This is a simplified version - real implementation would create actual PatternSource objects
    Log::info("[HeadlessGenerator] Committed " + std::to_string(m_patterns.size()) + " patterns");
}

void HeadlessMusicGenerator::commitPlaylistToModel() {
    auto& playlist = m_trackManager.getPlaylistModel();
    
    // Ensure we have enough lanes
    uint32_t maxLane = 0;
    for (const auto& clip : m_playlistClips) {
        maxLane = std::max(maxLane, clip.laneIndex);
    }
    
    // Create lanes
    for (uint32_t i = 0; i <= maxLane; ++i) {
        playlist.createLane("Lane " + std::to_string(i + 1));
    }
    
    // Add clips to playlist
    // TODO: Convert to actual ClipInstance and add to playlist
    // This requires PatternIDs from commitPatternsToManager
    
    Log::info("[HeadlessGenerator] Committed " + std::to_string(m_playlistClips.size()) + " clips to playlist");
}

} // namespace Audio
} // namespace Aestra
