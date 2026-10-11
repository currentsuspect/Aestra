// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "IO/StemExport.h"

#include "Core/MixerChannel.h"
#include "Models/TrackManager.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace Aestra {
namespace Audio {

std::vector<uint32_t> stemChannelIds(TrackManager& trackManager) {
    std::vector<uint32_t> routed;
    auto& playlist = trackManager.getPlaylistModel();
    for (size_t l = 0; l < playlist.getLaneCount(); ++l) {
        const PlaylistLane* lane = playlist.getLane(playlist.getLaneId(l));
        if (!lane)
            continue;
        for (const auto& clip : lane->clips) {
            const PatternSource* pattern = trackManager.getPatternManager().getPattern(clip.patternId);
            if (pattern && pattern->getMixerChannelId() != 0)
                routed.push_back(pattern->getMixerChannelId());
        }
    }
    const auto& units = trackManager.getUnitManager();
    for (const UnitID id : units.getAllUnitIDs()) {
        const auto* unit = units.getUnit(id);
        if (unit && unit->targetMixerChannelId != 0)
            routed.push_back(unit->targetMixerChannelId);
    }

    std::vector<uint32_t> ids; // mixer order, each once
    for (size_t i = 0; i < trackManager.getChannelCount(); ++i) {
        const MixerChannel* channel = trackManager.getChannel(i);
        if (channel && std::find(routed.begin(), routed.end(), channel->getChannelId()) != routed.end())
            ids.push_back(channel->getChannelId());
    }
    return ids;
}

std::string stemFileName(const std::string& prefix, int number, const std::string& trackName) {
    const auto safe = [](std::string text) {
        for (char& c : text) {
            if (static_cast<unsigned char>(c) < 0x20 || std::string("\\/:*?\"<>|").find(c) != std::string::npos)
                c = '_';
        }
        while (!text.empty() && (text.back() == ' ' || text.back() == '.'))
            text.pop_back(); // Windows refuses a name that ends this way
        return text;
    };
    char num[16];
    std::snprintf(num, sizeof(num), "%02d", number);
    std::string name = safe(trackName);
    if (name.empty())
        name = "Track";
    return safe(prefix.empty() ? std::string("Stem") : prefix) + " - " + num + " " + name + ".wav";
}

StemExportResult exportStems(AudioEngine& engine, TrackManager& trackManager, const AudioExporter::Config& base,
                             const std::string& directory, const std::string& prefix, bool includeMaster,
                             const std::function<void(float)>& progress, const std::function<bool()>& cancelled) {
    StemExportResult out;
    const std::vector<uint32_t> ids = stemChannelIds(trackManager);
    if (ids.empty() && !includeMaster) {
        out.error = "No track has any sound to export.";
        return out;
    }
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        out.error = "Cannot create the folder: " + directory;
        return out;
    }

    const size_t total = ids.size() + (includeMaster ? 1u : 0u);
    size_t done = 0;
    const auto renderOne = [&](uint32_t channelId, const std::string& name, const std::string& file) {
        AudioExporter exporter(engine, trackManager);
        if (cancelled)
            exporter.setCancelCheck(cancelled);
        if (progress) {
            exporter.setProgressCallback([&](float pct) {
                progress((static_cast<float>(done) + std::clamp(pct, 0.0f, 1.0f)) / static_cast<float>(total));
            });
        }
        AudioExporter::Config config = base;
        config.blockSink = nullptr;
        config.outputPath = (std::filesystem::path(directory) / file).string();
        config.numChannels = 2;
        if (channelId != 0) {
            config.captureMixerChannelId = channelId;
            config.captureAfterFader = true;
        } else {
            config.captureMixerChannelId = TrackOutputTap::kNoChannel;
        }
        StemFile stem;
        stem.mixerChannelId = channelId;
        stem.name = name;
        stem.path = config.outputPath;
        stem.result = exporter.render(config);
        out.files.push_back(stem);
        ++done;
        return stem.result.success;
    };

    int number = 1;
    for (const uint32_t id : ids) {
        const MixerChannel* channel = trackManager.getChannelById(id);
        const std::string name = channel ? channel->getName() : std::string();
        if (!renderOne(id, name, stemFileName(prefix, number++, name))) {
            out.error = "\"" + (name.empty() ? std::string("Track") : name) + "\": " +
                        out.files.back().result.errorMessage;
            return out;
        }
    }
    if (includeMaster && !renderOne(0, "Master", (prefix.empty() ? std::string("Stem") : prefix) + " - Master.wav")) {
        out.error = "Master: " + out.files.back().result.errorMessage;
        return out;
    }
    if (progress)
        progress(1.0f); // the exporter throttles its own updates; the last one must arrive
    out.ok = true;
    return out;
}

} // namespace Audio
} // namespace Aestra
