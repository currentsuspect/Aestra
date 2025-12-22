// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "TimeTypes.h"
#include "PlaylistModel.h"
#include <cmath>
#include <algorithm>

namespace Nomad {
namespace Audio {

// =============================================================================
// PlaylistGeometry - UI ↔ Timeline conversion utilities
// =============================================================================

/**
 * @brief Manages the mapping between pixel coordinates and timeline positions
 * 
 * This class handles all conversions between:
 * - Timeline positions (samples)
 * - Screen positions (pixels)
 * - Musical time (beats, bars)
 * 
 * Used by playlist UI components to:
 * - Convert mouse clicks to timeline positions
 * - Calculate clip rectangles
 * - Handle zoom and scroll
 */
class PlaylistGeometry {
public:
    PlaylistGeometry();
    ~PlaylistGeometry() = default;
    
    // === Viewport Settings ===
    
    /// Set the sample rate for conversions
    void setSampleRate(double sampleRate);
    double getSampleRate() const { return m_sampleRate; }
    
    /// Set BPM for beat/bar calculations
    void setBPM(double bpm);
    double getBPM() const { return m_bpm; }
    
    /// Set the horizontal scroll offset (in samples)
    void setScrollOffset(SampleIndex offset);
    SampleIndex getScrollOffset() const { return m_scrollOffset; }
    
    /// Set the zoom level (pixels per second)
    void setPixelsPerSecond(double pps);
    double getPixelsPerSecond() const { return m_pixelsPerSecond; }
    
    /// Convenience: get pixels per sample
    double getPixelsPerSample() const;
    
    /// Convenience: get samples per pixel
    double getSamplesPerPixel() const;
    
    /// Set viewport width in pixels
    void setViewportWidth(float width);
    float getViewportWidth() const { return m_viewportWidth; }
    
    /// Set track height
    void setTrackHeight(float height);
    float getTrackHeight() const { return m_trackHeight; }
    
    /// Set track header width (area before timeline)
    void setTrackHeaderWidth(float width);
    float getTrackHeaderWidth() const { return m_trackHeaderWidth; }
    
    // === Pixel ↔ Sample Conversion ===
    
    /// Convert pixel X coordinate to timeline sample position
    SampleIndex pixelToSample(float pixelX) const;
    
    /// Convert timeline sample to pixel X coordinate
    float sampleToPixel(SampleIndex sample) const;
    
    /// Convert a pixel width to sample duration
    SampleIndex pixelWidthToSamples(float pixelWidth) const;
    
    /// Convert sample duration to pixel width
    float samplesToPixelWidth(SampleIndex samples) const;
    
    // === Pixel ↔ Time Conversion ===
    
    /// Convert pixel X to time in seconds
    double pixelToSeconds(float pixelX) const;
    
    /// Convert time in seconds to pixel X
    float secondsToPixel(double seconds) const;
    
    // === Pixel ↔ Beat Conversion ===
    
    /// Convert pixel X to beat number
    double pixelToBeats(float pixelX) const;
    
    /// Convert beat number to pixel X
    float beatsToPixel(double beats) const;
    
    // === Track/Lane Geometry ===
    
    /// Convert pixel Y to track index
    int pixelToTrackIndex(float pixelY) const;
    
    /// Get the Y coordinate of a track's top edge
    float trackIndexToPixelY(int trackIndex) const;
    
    /// Get the rectangle for a track
    struct TrackRect {
        float x, y, width, height;
    };
    TrackRect getTrackRect(int trackIndex) const;
    
    // === Clip Geometry ===
    
    /// Get the rectangle for a clip
    struct ClipRect {
        float x, y, width, height;
        bool visible;  // True if any part is in viewport
    };
    ClipRect getClipRect(const PlaylistClip& clip, int trackIndex) const;
    
    /// Check if a clip is visible in current viewport
    bool isClipVisible(const PlaylistClip& clip) const;
    
    // === Hit Testing ===
    
    /// Hit test result for clip interaction
    enum class ClipHitZone {
        None,           ///< Not in clip
        Body,           ///< Main body (move)
        LeftEdge,       ///< Left trim handle
        RightEdge,      ///< Right trim handle
        FadeIn,         ///< Fade in handle
        FadeOut         ///< Fade out handle
    };
    
    /// Perform hit test on a clip
    ClipHitZone hitTestClip(const ClipRect& clipRect, float mouseX, float mouseY,
                            float edgeMargin = 8.0f) const;
    
    // === Visible Range ===
    
    /// Get the visible sample range
    SampleRange getVisibleRange() const;
    
    /// Get the visible time range in seconds
    std::pair<double, double> getVisibleTimeRange() const;
    
    // === Zoom Control ===
    
    /// Zoom in/out by factor, centered on a pixel position
    void zoom(float factor, float centerPixelX);
    
    /// Zoom to fit a time range
    void zoomToFitRange(SampleIndex startSample, SampleIndex endSample);
    
    /// Set zoom to show a specific samples-per-pixel ratio
    void setSamplesPerPixel(double spp);
    
    // === Grid ===
    
    /// Get the pixel position of grid lines in visible range
    struct GridLine {
        float pixelX;
        bool major;         ///< Major line (bar) or minor (beat)
        int barNumber;
        int beatNumber;
    };
    std::vector<GridLine> getVisibleGridLines(GridSubdivision subdivision) const;
    
    // === Constraints ===
    
    /// Minimum zoom level (samples per pixel)
    static constexpr double MIN_SAMPLES_PER_PIXEL = 1.0;
    
    /// Maximum zoom level (samples per pixel) 
    static constexpr double MAX_SAMPLES_PER_PIXEL = 100000.0;
    
    /// Default zoom level (samples per pixel)
    static constexpr double DEFAULT_SAMPLES_PER_PIXEL = 500.0;
    
    /// Default track height
    static constexpr float DEFAULT_TRACK_HEIGHT = 100.0f;

private:
    double m_sampleRate = DEFAULT_SAMPLE_RATE;
    double m_bpm = DEFAULT_BPM;
    double m_pixelsPerSecond = 100.0;  // Will be recalculated from samples/pixel
    SampleIndex m_scrollOffset = 0;
    
    float m_viewportWidth = 800.0f;
    float m_trackHeight = DEFAULT_TRACK_HEIGHT;
    float m_trackHeaderWidth = 150.0f;
    
    // Cached values
    void updateCachedValues();
    double m_samplesPerPixel = DEFAULT_SAMPLES_PER_PIXEL;
    double m_pixelsPerSample = 1.0 / DEFAULT_SAMPLES_PER_PIXEL;
};

// =============================================================================
// PlaylistGeometry Inline Implementation
// =============================================================================

inline PlaylistGeometry::PlaylistGeometry() {
    updateCachedValues();
}

inline void PlaylistGeometry::setSampleRate(double sampleRate) {
    if (sampleRate > 0) {
        m_sampleRate = sampleRate;
        updateCachedValues();
    }
}

inline void PlaylistGeometry::setBPM(double bpm) {
    if (bpm > 0) {
        m_bpm = bpm;
    }
}

inline void PlaylistGeometry::setScrollOffset(SampleIndex offset) {
    m_scrollOffset = std::max(offset, static_cast<SampleIndex>(0));
}

inline void PlaylistGeometry::setPixelsPerSecond(double pps) {
    if (pps > 0) {
        m_pixelsPerSecond = pps;
        m_samplesPerPixel = m_sampleRate / pps;
        m_samplesPerPixel = std::clamp(m_samplesPerPixel, MIN_SAMPLES_PER_PIXEL, MAX_SAMPLES_PER_PIXEL);
        m_pixelsPerSample = 1.0 / m_samplesPerPixel;
    }
}

inline double PlaylistGeometry::getPixelsPerSample() const {
    return m_pixelsPerSample;
}

inline double PlaylistGeometry::getSamplesPerPixel() const {
    return m_samplesPerPixel;
}

inline void PlaylistGeometry::setViewportWidth(float width) {
    m_viewportWidth = std::max(width, 1.0f);
}

inline void PlaylistGeometry::setTrackHeight(float height) {
    m_trackHeight = std::max(height, 20.0f);
}

inline void PlaylistGeometry::setTrackHeaderWidth(float width) {
    m_trackHeaderWidth = std::max(width, 0.0f);
}

inline void PlaylistGeometry::setSamplesPerPixel(double spp) {
    m_samplesPerPixel = std::clamp(spp, MIN_SAMPLES_PER_PIXEL, MAX_SAMPLES_PER_PIXEL);
    m_pixelsPerSample = 1.0 / m_samplesPerPixel;
    m_pixelsPerSecond = m_sampleRate / m_samplesPerPixel;
}

inline void PlaylistGeometry::updateCachedValues() {
    m_pixelsPerSecond = m_sampleRate / m_samplesPerPixel;
}

inline SampleIndex PlaylistGeometry::pixelToSample(float pixelX) const {
    float timelinePixelX = pixelX - m_trackHeaderWidth;
    return m_scrollOffset + static_cast<SampleIndex>(timelinePixelX * m_samplesPerPixel);
}

inline float PlaylistGeometry::sampleToPixel(SampleIndex sample) const {
    return m_trackHeaderWidth + static_cast<float>((sample - m_scrollOffset) * m_pixelsPerSample);
}

inline SampleIndex PlaylistGeometry::pixelWidthToSamples(float pixelWidth) const {
    return static_cast<SampleIndex>(pixelWidth * m_samplesPerPixel);
}

inline float PlaylistGeometry::samplesToPixelWidth(SampleIndex samples) const {
    return static_cast<float>(samples * m_pixelsPerSample);
}

inline double PlaylistGeometry::pixelToSeconds(float pixelX) const {
    SampleIndex sample = pixelToSample(pixelX);
    return samplesToSeconds(sample, m_sampleRate);
}

inline float PlaylistGeometry::secondsToPixel(double seconds) const {
    SampleIndex sample = secondsToSamples(seconds, m_sampleRate);
    return sampleToPixel(sample);
}

inline double PlaylistGeometry::pixelToBeats(float pixelX) const {
    SampleIndex sample = pixelToSample(pixelX);
    return samplesToBeats(sample, m_bpm, m_sampleRate);
}

inline float PlaylistGeometry::beatsToPixel(double beats) const {
    SampleIndex sample = beatsToSamples(beats, m_bpm, m_sampleRate);
    return sampleToPixel(sample);
}

inline int PlaylistGeometry::pixelToTrackIndex(float pixelY) const {
    if (pixelY < 0) return -1;
    return static_cast<int>(pixelY / m_trackHeight);
}

inline float PlaylistGeometry::trackIndexToPixelY(int trackIndex) const {
    return static_cast<float>(trackIndex) * m_trackHeight;
}

inline PlaylistGeometry::TrackRect PlaylistGeometry::getTrackRect(int trackIndex) const {
    return {
        0.0f,
        trackIndexToPixelY(trackIndex),
        m_viewportWidth,
        m_trackHeight
    };
}

inline PlaylistGeometry::ClipRect PlaylistGeometry::getClipRect(const PlaylistClip& clip, int trackIndex) const {
    ClipRect rect;
    rect.x = sampleToPixel(clip.startTime);
    rect.y = trackIndexToPixelY(trackIndex);
    rect.width = samplesToPixelWidth(clip.length);
    rect.height = m_trackHeight;
    
    // Check visibility
    float viewStart = m_trackHeaderWidth;
    float viewEnd = m_trackHeaderWidth + m_viewportWidth;
    rect.visible = (rect.x + rect.width > viewStart) && (rect.x < viewEnd);
    
    return rect;
}

inline bool PlaylistGeometry::isClipVisible(const PlaylistClip& clip) const {
    SampleRange visible = getVisibleRange();
    return clip.overlapsRange(visible);
}

inline PlaylistGeometry::ClipHitZone PlaylistGeometry::hitTestClip(
    const ClipRect& clipRect, float mouseX, float mouseY, float edgeMargin) const {
    
    // Check if in Y range
    if (mouseY < clipRect.y || mouseY >= clipRect.y + clipRect.height) {
        return ClipHitZone::None;
    }
    
    // Check if in X range
    if (mouseX < clipRect.x || mouseX >= clipRect.x + clipRect.width) {
        return ClipHitZone::None;
    }
    
    // Check edge zones
    if (mouseX < clipRect.x + edgeMargin) {
        return ClipHitZone::LeftEdge;
    }
    if (mouseX >= clipRect.x + clipRect.width - edgeMargin) {
        return ClipHitZone::RightEdge;
    }
    
    return ClipHitZone::Body;
}

inline SampleRange PlaylistGeometry::getVisibleRange() const {
    float timelineWidth = m_viewportWidth - m_trackHeaderWidth;
    SampleIndex visibleSamples = pixelWidthToSamples(std::max(timelineWidth, 0.0f));
    return SampleRange(m_scrollOffset, m_scrollOffset + visibleSamples);
}

inline std::pair<double, double> PlaylistGeometry::getVisibleTimeRange() const {
    SampleRange range = getVisibleRange();
    return {
        samplesToSeconds(range.start, m_sampleRate),
        samplesToSeconds(range.end, m_sampleRate)
    };
}

inline void PlaylistGeometry::zoom(float factor, float centerPixelX) {
    // Get the sample at the center point before zoom
    SampleIndex centerSample = pixelToSample(centerPixelX);
    
    // Apply zoom
    double newSPP = m_samplesPerPixel / factor;
    setSamplesPerPixel(newSPP);
    
    // Adjust scroll to keep the center point at the same pixel
    float pixelOffset = centerPixelX - m_trackHeaderWidth;
    SampleIndex newScrollOffset = centerSample - static_cast<SampleIndex>(pixelOffset * m_samplesPerPixel);
    setScrollOffset(newScrollOffset);
}

inline void PlaylistGeometry::zoomToFitRange(SampleIndex startSample, SampleIndex endSample) {
    if (endSample <= startSample) return;
    
    SampleIndex duration = endSample - startSample;
    float timelineWidth = m_viewportWidth - m_trackHeaderWidth;
    
    if (timelineWidth <= 0) return;
    
    double newSPP = static_cast<double>(duration) / timelineWidth;
    setSamplesPerPixel(newSPP);
    setScrollOffset(startSample);
}

inline std::vector<PlaylistGeometry::GridLine> PlaylistGeometry::getVisibleGridLines(
    GridSubdivision subdivision) const {
    
    std::vector<GridLine> lines;
    
    if (subdivision == GridSubdivision::None) {
        return lines;
    }
    
    SampleRange visible = getVisibleRange();
    SampleIndex interval = getGridInterval(subdivision, m_bpm, m_sampleRate);
    SampleIndex barInterval = beatsToSamples(4.0, m_bpm, m_sampleRate);
    
    if (interval <= 0 || barInterval <= 0) {
        return lines;
    }
    
    // Start from first grid line before visible range
    SampleIndex firstLine = (visible.start / interval) * interval;
    
    for (SampleIndex pos = firstLine; pos < visible.end; pos += interval) {
        if (pos < 0) continue;
        
        GridLine line;
        line.pixelX = sampleToPixel(pos);
        line.major = (pos % barInterval) == 0;
        
        double beats = samplesToBeats(pos, m_bpm, m_sampleRate);
        line.barNumber = static_cast<int>(beats / 4.0) + 1;
        line.beatNumber = static_cast<int>(std::fmod(beats, 4.0)) + 1;
        
        lines.push_back(line);
    }
    
    return lines;
}

} // namespace Audio
} // namespace Nomad
