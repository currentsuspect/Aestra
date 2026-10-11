// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "IO/AudioExporter.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

/** @brief One stem that was (or failed to be) written. */
struct StemFile {
    uint32_t mixerChannelId = 0; ///< 0 for the master mix
    std::string name;            ///< the track's name as the user sees it
    std::string path;
    AudioExporter::Result result;
};

struct StemExportResult {
    bool ok = false;
    std::string error; ///< one sentence; empty on success
    std::vector<StemFile> files;
};

/** @brief The tracks that make sound of their own and so get a stem: clips or instruments routed to them. */
std::vector<uint32_t> stemChannelIds(TrackManager& trackManager);

/** @brief "<prefix> - 01 Kick.wav": numbered in mixer order, name made safe for every filesystem. */
std::string stemFileName(const std::string& prefix, int number, const std::string& trackName);

/**
 * @brief Render every sounding track to its own file, over the same range (V8-S1).
 *
 * Each stem is the track as it feeds the mix: inserts, automation, delay
 * compensation, fader and pan, read before it is routed anywhere (the same tap
 * Bounce in Place uses, placed after the fader). All stems have the same length
 * and start, so they line up in any other program. Group/bus tracks are not
 * given stems of their own; what they carry is already in the tracks routed to
 * them. With @p includeMaster the full mix is written too, as "<prefix> - Master".
 *
 * @p base supplies scope, range, rate, bit depth and tail. Its output path is
 * ignored. Blocking: run on a worker thread with the audio stream stopped.
 */
StemExportResult exportStems(AudioEngine& engine, TrackManager& trackManager, const AudioExporter::Config& base,
                             const std::string& directory, const std::string& prefix, bool includeMaster,
                             const std::function<void(float)>& progress = {},
                             const std::function<bool()>& cancelled = {});

} // namespace Audio
} // namespace Aestra
