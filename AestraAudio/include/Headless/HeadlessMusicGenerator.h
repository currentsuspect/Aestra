// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Models/TrackManager.h"
#include "../Models/PatternManager.h"
#include "../Models/PlaylistModel.h"
#include "../IO/AudioExporter.h"
#include "../Core/AudioEngine.h"
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <memory>

namespace Aestra {
namespace Audio {

/**
 * @brief Programmatic music generation API for headless/batch processing (v1.0)
 * 
 * Provides a fluent, chainable API for creating music without UI.
 * Useful for:
 * - Automated testing and CI/CD
 * - Batch music generation
 * - Procedural composition
 * - Audio pipeline validation
 * 
 * Usage:
 *   HeadlessMusicGenerator gen(engine, trackManager);
 *   gen.createProject("TechnoTrack")
 *      .setTempo(130)
 *      .createPattern("Kick", 16)
 *          .addNote(0, 60, 100, 1.0)   // C4, velocity 100, full step
 *          .addNote(4, 60, 100, 1.0)
 *          .addNote(8, 60, 100, 1.0)
 *          .addNote(12, 60, 100, 1.0)
 *      .createPattern("Bass", 16)
 *          .addNote(0, 36, 90, 2.0)    // C2, longer note
 *          .addNote(8, 36, 90, 2.0)
 *      .addClipToPlaylist("Kick", 0.0, 4.0)   // 4 bars
 *      .addClipToPlaylist("Bass", 0.0, 4.0)
 *      .exportTo("techno.wav", 48000, AudioExporter::BitDepth::PCM_24);
 */
class HeadlessMusicGenerator {
public:
    // =============================================================================
    // Types
    // =============================================================================
    
    struct Note {
        uint8_t step;      // Step within pattern (0 to patternLength-1)
        uint8_t pitch;     // MIDI note number (0-127)
        uint8_t velocity;  // Velocity (0-127)
        double duration;   // Duration in steps
        
        Note(uint8_t s, uint8_t p, uint8_t v, double d) 
            : step(s), pitch(p), velocity(v), duration(d) {}
    };
    
    struct PatternData {
        std::string name;
        uint32_t length;  // Length in steps (e.g., 16 for 16-step sequencer)
        std::vector<Note> notes;
    };
    
    struct ClipPlacement {
        std::string patternName;
        double startBeat;
        double durationBeats;
        uint32_t laneIndex;
    };
    
    // =============================================================================
    // Lifecycle
    // =============================================================================
    
    HeadlessMusicGenerator(AudioEngine& engine, TrackManager& trackManager);
    ~HeadlessMusicGenerator() = default;
    
    // Non-copyable
    HeadlessMusicGenerator(const HeadlessMusicGenerator&) = delete;
    HeadlessMusicGenerator& operator=(const HeadlessMusicGenerator&) = delete;
    
    // =============================================================================
    // Project Setup (Fluent API - returns *this)
    // =============================================================================
    
    /**
     * @brief Initialize a new project
     */
    HeadlessMusicGenerator& createProject(const std::string& name);
    
    /**
     * @brief Set project tempo (BPM)
     */
    HeadlessMusicGenerator& setTempo(double bpm);
    
    /**
     * @brief Set project sample rate
     */
    HeadlessMusicGenerator& setSampleRate(uint32_t sampleRate);
    
    /**
     * @brief Set time signature (numerator/denominator)
     */
    HeadlessMusicGenerator& setTimeSignature(uint8_t numerator, uint8_t denominator);
    
    // =============================================================================
    // Pattern Creation (Fluent API)
    // =============================================================================
    
    /**
     * @brief Create a new empty pattern
     * @param name Pattern name (unique identifier)
     * @param length Length in steps (e.g., 16 for 4/4 bar)
     */
    HeadlessMusicGenerator& createPattern(const std::string& name, uint32_t length);
    
    /**
     * @brief Add a note to the current pattern
     */
    HeadlessMusicGenerator& addNote(uint8_t step, uint8_t pitch, uint8_t velocity, double duration);
    
    /**
     * @brief Add multiple notes (chord) at the same step
     */
    HeadlessMusicGenerator& addChord(uint8_t step, const std::vector<uint8_t>& pitches, 
                                      uint8_t velocity, double duration);
    
    /**
     * @brief Add a drum hit (convenience for percussion)
     */
    HeadlessMusicGenerator& addHit(uint8_t step);
    
    /**
     * @brief Fill every Nth step (e.g., every 4th for quarter notes)
     */
    HeadlessMusicGenerator& fillEvery(uint8_t interval, uint8_t pitch, uint8_t velocity);
    
    /**
     * @brief Create a Euclidean rhythm pattern
     * @param pulses Number of pulses
     * @param steps Total steps
     * @param pitch Note pitch
     * @param velocity Note velocity
     */
    HeadlessMusicGenerator& addEuclideanRhythm(uint8_t pulses, uint8_t steps, 
                                                uint8_t pitch, uint8_t velocity);
    
    // =============================================================================
    // Playlist Assembly (Fluent API)
    // =============================================================================
    
    /**
     * @brief Add a pattern as a clip to the playlist
     * @param patternName Name of pattern to add
     * @param startBeat Start position in beats
     * @param durationBeats Duration in beats (can be longer than pattern for looping)
     * @param laneIndex Which lane (track) to place on (default: auto)
     */
    HeadlessMusicGenerator& addClipToPlaylist(const std::string& patternName, 
                                               double startBeat, 
                                               double durationBeats,
                                               uint32_t laneIndex = 0);
    
    /**
     * @brief Repeat a clip N times
     */
    HeadlessMusicGenerator& repeatClip(const std::string& patternName, 
                                        double startBeat, 
                                        double durationBeats,
                                        uint32_t repetitions,
                                        uint32_t laneIndex = 0);
    
    /**
     * @brief Add a blank clip (silence)
     */
    HeadlessMusicGenerator& addSilence(double startBeat, double durationBeats);
    
    // =============================================================================
    // Mixer (Fluent API)
    // =============================================================================
    
    /**
     * @brief Add a mixer channel
     */
    HeadlessMusicGenerator& addChannel(const std::string& name);
    
    /**
     * @brief Set channel volume (0.0 to 1.0)
     */
    HeadlessMusicGenerator& setChannelVolume(uint32_t channelIndex, float volume);
    
    /**
     * @brief Set channel pan (-1.0 = left, 0.0 = center, 1.0 = right)
     */
    HeadlessMusicGenerator& setChannelPan(uint32_t channelIndex, float pan);
    
    /**
     * @brief Mute a channel
     */
    HeadlessMusicGenerator& muteChannel(uint32_t channelIndex, bool muted = true);
    
    // =============================================================================
    // Presets
    // =============================================================================
    
    /**
     * @brief Load a preset drum pattern (Four-on-the-floor)
     */
    HeadlessMusicGenerator& loadFourOnTheFloor(const std::string& name = "Kick");
    
    /**
     * @brief Load a preset hi-hat pattern
     */
    HeadlessMusicGenerator& loadHiHatPattern(const std::string& name = "HiHat");
    
    /**
     * @brief Load a preset bassline
     */
    HeadlessMusicGenerator& loadBassline(const std::string& name = "Bass");
    
    /**
     * @brief Generate a complete track from a preset template
     * @param templateName "Techno", "House", "HipHop", "Minimal"
     */
    HeadlessMusicGenerator& generateFromTemplate(const std::string& templateName, 
                                                  uint32_t durationBars = 8);
    
    // =============================================================================
    // Export
    // =============================================================================
    
    /**
     * @brief Export the project to an audio file
     * @param outputPath Output file path (WAV)
     * @param sampleRate Target sample rate (0 = use project rate)
     * @param bitDepth Bit depth for export
     * @param scope Render scope
     * @return true if export succeeded
     */
    bool exportTo(const std::string& outputPath,
                  uint32_t sampleRate = 48000,
                  AudioExporter::BitDepth bitDepth = AudioExporter::BitDepth::PCM_24,
                  AudioExporter::RenderScope scope = AudioExporter::RenderScope::FullSong);
    
    /**
     * @brief Export with progress callback
     */
    bool exportTo(const std::string& outputPath,
                  std::function<void(float)> progressCallback,
                  uint32_t sampleRate = 48000,
                  AudioExporter::BitDepth bitDepth = AudioExporter::BitDepth::PCM_24);
    
    // =============================================================================
    // Validation & Debug
    // =============================================================================
    
    /**
     * @brief Validate the project is ready for export
     * @return Error message if invalid, empty string if valid
     */
    std::string validate() const;
    
    /**
     * @brief Print project info to log
     */
    void printInfo() const;
    
    /**
     * @brief Get total project duration in beats
     */
    double getDurationBeats() const;
    
private:
    // =============================================================================
    // Internal
    // =============================================================================
    
    PatternData* getCurrentPattern();
    PatternData* findPattern(const std::string& name);
    
    void commitPatternsToManager();
    void commitPlaylistToModel();
    
private:
    AudioEngine& m_engine;
    TrackManager& m_trackManager;
    
    // Project state
    std::string m_projectName;
    double m_tempo = 120.0;
    uint32_t m_sampleRate = 48000;
    uint8_t m_timeSigNum = 4;
    uint8_t m_timeSigDenom = 4;
    
    // Patterns
    std::map<std::string, PatternData> m_patterns;
    std::string m_currentPatternName;
    
    // Playlist
    std::vector<ClipPlacement> m_playlistClips;
    
    // Mixer
    std::vector<std::string> m_channelNames;
};

} // namespace Audio
} // namespace Aestra
