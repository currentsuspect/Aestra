// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "ProjectSerializer.h"
#include "../NomadCore/include/NomadLog.h"
#include <filesystem>
#include <fstream>

using namespace Nomad;
using namespace Nomad::Audio;

namespace {
    constexpr int PROJECT_VERSION = 1;
}

bool ProjectSerializer::save(const std::string& path,
                             const std::shared_ptr<TrackManager>& trackManager,
                             double tempo,
                             double playheadSeconds) {
    if (!trackManager) return false;

    JSON root = JSON::object();
    root.set("version", JSON(static_cast<double>(PROJECT_VERSION)));
    root.set("tempo", JSON(tempo));
    root.set("playhead", JSON(playheadSeconds));

    JSON tracksJson = JSON::array();
    for (size_t i = 0; i < trackManager->getTrackCount(); ++i) {
        auto track = trackManager->getTrack(i);
        if (!track || track->isSystemTrack()) continue;

        JSON t = JSON::object();
        // Save UUID for stable identity across sessions
        t.set("uuid", JSON(track->getUUID().toString()));
        t.set("name", JSON(track->getName()));
        t.set("color", JSON(static_cast<double>(track->getColor())));
        t.set("volume", JSON(track->getVolume()));
        t.set("pan", JSON(track->getPan()));
        t.set("mute", JSON(track->isMuted()));
        t.set("solo", JSON(track->isSoloed()));
        t.set("start", JSON(track->getStartPositionInTimeline()));
        t.set("file", JSON(track->getSourcePath()));
        t.set("sampleRate", JSON(static_cast<double>(track->getSampleRate())));
        t.set("channels", JSON(static_cast<double>(track->getNumChannels())));
        // Save trim settings
        t.set("trimStart", JSON(track->getTrimStart()));
        t.set("trimEnd", JSON(track->getTrimEnd()));

        tracksJson.push(t);
    }

    root.set("tracks", tracksJson);

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        Log::error("Project save failed (cannot open file): " + path);
        return false;
    }
    out << root.toString(2);
    Log::info("Project saved to " + path);
    return true;
}

ProjectSerializer::LoadResult ProjectSerializer::load(const std::string& path,
                                                      const std::shared_ptr<TrackManager>& trackManager) {
    LoadResult result;
    if (!trackManager) return result;
    if (!std::filesystem::exists(path)) {
        Log::warning("Project file not found: " + path);
        return result;
    }

    std::ifstream in(path);
    if (!in) {
        Log::error("Project load failed (cannot open file): " + path);
        return result;
    }
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    JSON root = JSON::parse(contents);
    if (!root.isObject()) {
        Log::error("Project load failed (invalid JSON root)");
        return result;
    }

    int version = root.has("version") ? root["version"].asInt() : 0;
    if (version != PROJECT_VERSION) {
        Log::warning("Project version mismatch");
    }

    result.tempo = root.has("tempo") ? root["tempo"].asNumber() : 120.0;
    result.playhead = root.has("playhead") ? root["playhead"].asNumber() : 0.0;

    // Clear existing tracks
    trackManager->clearAllTracks();

    const JSON& tracksJson = root["tracks"];
    if (tracksJson.isArray()) {
        for (size_t i = 0; i < tracksJson.size(); ++i) {
            const JSON& t = tracksJson[i];
            if (!t.isObject()) continue;

            std::string name = t.has("name") ? t["name"].asString() : "Track";
            auto track = trackManager->addTrack(name);
            
            if (!track) {
                Log::error("Failed to create track: " + name);
                continue;
            }

            // Restore UUID if present (for stable identity)
            if (t.has("uuid")) {
                std::string uuidStr = t["uuid"].asString();
                TrackUUID uuid = TrackUUID::fromString(uuidStr);
                if (uuid.isValid()) {
                    track->setUUID(uuid);
                }
            }

            if (t.has("color")) track->setColor(static_cast<uint32_t>(t["color"].asNumber()));
            if (t.has("volume")) track->setVolume(static_cast<float>(t["volume"].asNumber()));
            if (t.has("pan")) track->setPan(static_cast<float>(t["pan"].asNumber()));
            if (t.has("mute")) track->setMute(t["mute"].asBool());
            if (t.has("solo")) track->setSolo(t["solo"].asBool());
            if (t.has("start")) track->setStartPositionInTimeline(t["start"].asNumber());

            // Store the audio file path but DON'T load it during project restore
            // This prevents hangs from loading large audio files synchronously
            std::string file = t.has("file") ? t["file"].asString() : "";
            if (!file.empty()) {
                // Just store the source path - don't load the audio data
                // User can drag the file again to reload, or we can add async loading later
                track->setSourcePath(file);
                Log::info("Track '" + name + "' references audio: " + file + " (not loaded - drag to reload)");
            }
            
            // Restore trim settings
            if (t.has("trimStart")) track->setTrimStart(t["trimStart"].asNumber());
            if (t.has("trimEnd")) track->setTrimEnd(t["trimEnd"].asNumber());
        }
    }

    result.ok = true;
    Log::info("Project loaded from " + path);
    return result;
}
