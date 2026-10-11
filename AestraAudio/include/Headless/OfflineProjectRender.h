// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "../Core/AudioEngine.h"
#include "../IO/AudioExporter.h"
#include "../Models/TrackManager.h"

#include <cstdint>
#include <functional>
#include <string>

namespace Aestra {
namespace Audio {

/**
 * @brief What one offline render of a project should produce.
 */
struct OfflineRenderRequest {
    std::string outputPath;
    uint32_t sampleRate = 48000;
    AudioExporter::BitDepth bitDepth = AudioExporter::BitDepth::PCM_24;
    double tempoBpm = 120.0;
    std::function<void(float)> progress;
};

/**
 * @brief Render a loaded project's timeline to a file, offline, through
 *        AudioExporter -- the path the app's own Export uses.
 *
 * The one place that wires an AudioEngine to a TrackManager for an offline
 * render: engine rate and buffer, tempo, the track manager (borrowed, never
 * owned), the unit manager, the pattern-playback engine, the channel slot map
 * and the graph, then the committed timeline scheduled into pattern playback
 * WITHOUT touching the live transport. Everything it wires is unwound on every
 * exit path, so the caller's engine and session are left as they were.
 *
 * Before this existed the wiring lived in HeadlessMusicGenerator::exportTo, and
 * AestraHeadless's project mode kept its own, shorter copy that never connected
 * the unit manager or pattern playback -- so MIDI and pattern clips were silent
 * in a headless project render. One function means one set of wiring to keep in
 * step with the app.
 *
 * Synchronous: no callback or retained object can touch the wiring after it
 * returns. @p trackManager must outlive the call.
 */
AudioExporter::Result renderProjectOffline(AudioEngine& engine, TrackManager& trackManager,
                                           const OfflineRenderRequest& request);

} // namespace Audio
} // namespace Aestra
