// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "ClipInstance.h"
#include "ClipSource.h"
#include "PatternSource.h"

#include <cstdint>
#include <memory>
#include <string>

namespace Aestra {
namespace Audio {

class PatternManager;
class SourceManager;

/**
 * @brief Renders *source-domain* audio into new, committed source files.
 *
 * Scope, deliberately narrow: this service only ever reads a clip's own source
 * buffer and its clip-local edits. It powers reverse, commit-clip-edits, and
 * the source-copying half of consolidate. Each of those is "take some frames,
 * transform them, and hand back a new source that stands on its own", so the
 * file writing, source registration and pattern creation live here once
 * instead of in each command.
 *
 * NOT the universal render primitive. Freeze, stem printing and bus printing
 * must reflect plugins, automation, sends, sidechains, routing, PDC and an
 * output scope — none of which exist at this layer. Those belong to a separate
 * range/graph renderer driven by the audio graph. The two may share the WAV
 * writer and source registration below, but must not share rendering
 * semantics: anything that needs the graph does not belong in this file.
 *
 * The transforms are deliberately static and free of any manager dependency so
 * they can be tested on a bare buffer. Only commit() needs project state.
 */
class ClipRenderService {
public:
    /** Frames rendered plus the file they were committed to. */
    struct CommitResult {
        PatternID patternId;
        ClipSourceID sourceId;
        std::string filePath;

        bool isValid() const { return patternId.isValid() && sourceId.isValid(); }
    };

    /**
     * @brief The concrete audio a clip currently points at.
     *
     * Resolving a clip means walking clip -> pattern -> audio payload -> source
     * buffer, and every destructive op starts by doing exactly that.
     */
    struct SourceRegion {
        std::shared_ptr<const AudioBufferData> buffer;
        uint64_t startFrame{0};
        uint64_t frameCount{0};
        uint32_t mixerChannelId{0};
        double lengthBeats{4.0};
        std::string name;

        bool isValid() const { return buffer != nullptr && frameCount > 0; }
    };

    ClipRenderService(SourceManager& sources, PatternManager& patterns) : m_sources(sources), m_patterns(patterns) {}

    /**
     * @brief Resolve the audio a clip currently plays.
     * @return An invalid region when the clip is missing, is not an audio
     *         clip, or its source has no decoded buffer.
     */
    SourceRegion resolveClipRegion(const ClipInstance& clip) const;

    // --- Pure transforms -----------------------------------------------------

    /**
     * @brief Copy a frame range out of @p source into a fresh buffer.
     *
     * A range that starts past the end yields an empty (invalid) buffer rather
     * than a zero-filled one, so callers can tell "nothing to render" from
     * "rendered silence". Ranges that overhang the end are clamped.
     */
    static std::shared_ptr<AudioBufferData> extractRegion(const AudioBufferData& source, uint64_t startFrame,
                                                          uint64_t frameCount);

    /** @brief Reverse @p buffer in place, keeping channel interleaving intact. */
    static void reverseInPlace(AudioBufferData& buffer);

    /** @brief Scale every sample by @p gainLinear. Non-finite gains are ignored. */
    static void applyGain(AudioBufferData& buffer, float gainLinear);

    /**
     * @brief Apply linear fade-in/fade-out ramps measured in frames.
     *
     * Overlapping ramps (fadeIn + fadeOut longer than the buffer) are each
     * clamped so the two never multiply into an unintended notch.
     */
    static void applyFades(AudioBufferData& buffer, uint64_t fadeInFrames, uint64_t fadeOutFrames);

    /** @brief Peak absolute sample value, or 0 for an empty buffer. */
    static float peakMagnitude(const AudioBufferData& buffer);

    // --- Commit --------------------------------------------------------------

    /**
     * @brief Write @p buffer to disk and register it as a new source + pattern.
     *
     * The file lands next to the project's other rendered material under
     * @p renderDirectory. @p baseName is only a hint: the final filename is
     * uniquified so two bounces of the same clip never collide.
     *
     * @return An invalid result if the buffer is unusable or the write failed;
     *         no source or pattern is registered in that case.
     */
    CommitResult commit(const AudioBufferData& buffer, const std::string& renderDirectory,
                        const std::string& baseName, double lengthBeats, uint32_t mixerChannelId);

private:
    static bool writeFloatWav(const std::string& path, const AudioBufferData& buffer);
    static std::string uniqueRenderPath(const std::string& directory, const std::string& baseName);

    SourceManager& m_sources;
    PatternManager& m_patterns;
};

} // namespace Audio
} // namespace Aestra
