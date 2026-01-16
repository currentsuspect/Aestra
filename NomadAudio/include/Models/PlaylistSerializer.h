// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PlaylistModel.h"
#include "ClipSource.h"
#include "NomadJSON.h"
#include <string>
#include <filesystem>
#include <fstream>

namespace Nomad {
namespace Audio {

// =============================================================================
// PlaylistSerializer - JSON persistence for playlist data
// =============================================================================

/**
 * @brief Serialization/deserialization for playlist data
 * 
 * Handles saving and loading of:
 * - PlaylistModel (lanes, clips)
 * - Audio file references (resolved via SourceManager on load)
 * 
 * JSON format:
 * ```json
 * {
 *   "projectSampleRate": 48000,
 *   "bpm": 120,
 *   "gridSubdivision": "beat",
 *   "lanes": [
 *     {
 *       "id": "uuid-string",
 *       "name": "Track 1",
 *       "color": 4286611711,
 *       "volume": 1.0,
 *       "pan": 0.0,
 *       "muted": false,
 *       "solo": false,
 *       "clips": [
 *         {
 *           "id": "uuid-string",
 *           "sourceFile": "path/to/audio.wav",
 *           "startTime": 48000,
 *           "length": 96000,
 *           "sourceStart": 0,
 *           "gain": 1.0,
 *           "pan": 0.0,
 *           "muted": false,
 *           "playbackRate": 1.0,
 *           "fadeInLength": 0,
 *           "fadeOutLength": 0,
 *           "flags": 0,
 *           "color": 4286611711,
 *           "name": "Clip 1"
 *         }
 *       ]
 *     }
 *   ]
 * }
 * ```
 */
class PlaylistSerializer {
public:
    /**
     * @brief Serialize playlist to JSON string
     * 
     * @param model The playlist model to serialize
     * @param sourceManager Source manager for file path lookup
     * @return JSON string
     */
    static std::string serialize(const PlaylistModel& model, 
                                  const SourceManager& sourceManager);
    
    /**
     * @brief Deserialize playlist from JSON string
     * 
     * @param json JSON string to parse
     * @param model Target playlist model (will be cleared first)
     * @param sourceManager Source manager for loading audio files
     * @param audioLoader Callback to load audio files: (path) -> AudioBufferData
     * @return true if successful
     */
    using AudioLoaderFunc = std::function<std::shared_ptr<AudioBufferData>(const std::string& path)>;
    
    static bool deserialize(const std::string& json,
                            PlaylistModel& model,
                            SourceManager& sourceManager,
                            AudioLoaderFunc audioLoader);
    
    /**
     * @brief Save playlist to file
     */
    static bool saveToFile(const std::string& filepath,
                           const PlaylistModel& model,
                           const SourceManager& sourceManager);
    
    /**
     * @brief Load playlist from file
     */
    static bool loadFromFile(const std::string& filepath,
                             PlaylistModel& model,
                             SourceManager& sourceManager,
                             AudioLoaderFunc audioLoader);

private:
    static std::string gridSubdivisionToString(GridSubdivision grid);
    static GridSubdivision stringToGridSubdivision(const std::string& str);
    
    static JSON serializeLane(const PlaylistLane& lane, 
                                       const SourceManager& sourceManager);
    static JSON serializeClip(const ClipInstance& clip,
                                       const SourceManager& sourceManager);
    
    static bool deserializeLane(const JSON& obj, 
                                PlaylistModel& model,
                                SourceManager& sourceManager,
                                AudioLoaderFunc audioLoader);
    static bool deserializeClip(const JSON& obj,
                                PlaylistLaneID laneId,
                                PlaylistModel& model,
                                SourceManager& sourceManager,
                                AudioLoaderFunc audioLoader);
};

// =============================================================================
// Implementation
// =============================================================================

inline std::string PlaylistSerializer::gridSubdivisionToString(GridSubdivision grid) {
    switch (grid) {
        case GridSubdivision::Bar:      return "bar";
        case GridSubdivision::Beat:     return "beat";
        case GridSubdivision::Half:     return "half";
        case GridSubdivision::Quarter:  return "quarter";
        case GridSubdivision::Eighth:   return "eighth";
        case GridSubdivision::Triplet:  return "triplet";
        case GridSubdivision::None:     return "none";
        default:                        return "beat";
    }
}

inline GridSubdivision PlaylistSerializer::stringToGridSubdivision(const std::string& str) {
    if (str == "bar")      return GridSubdivision::Bar;
    if (str == "beat")     return GridSubdivision::Beat;
    if (str == "half")     return GridSubdivision::Half;
    if (str == "quarter")  return GridSubdivision::Quarter;
    if (str == "eighth")   return GridSubdivision::Eighth;
    if (str == "triplet")  return GridSubdivision::Triplet;
    if (str == "none")     return GridSubdivision::None;
    return GridSubdivision::Beat;
}

inline std::string PlaylistSerializer::serialize(const PlaylistModel& model,
                                                   const SourceManager& sourceManager) {
    JSON root = JSON::object();
    
    root.set("projectSampleRate", JSON(model.getProjectSampleRate()));
    root.set("bpm", JSON(model.getBPM()));
    
    JSON lanesArray = JSON::array();
    auto laneIds = model.getLaneIDs();
    
    for (const auto& laneId : laneIds) {
        const PlaylistLane* lane = model.getLane(laneId);
        if (lane) {
            lanesArray.push(serializeLane(*lane, sourceManager));
        }
    }
    
    root.set("lanes", lanesArray);
    
    return root.toString(4);
}

inline JSON PlaylistSerializer::serializeLane(const PlaylistLane& lane,
                                                        const SourceManager& sourceManager) {
    JSON obj = JSON::object();
    
    obj.set("id", JSON(lane.id.toString()));
    obj.set("name", JSON(lane.name));
    obj.set("color", JSON(static_cast<double>(lane.colorRGBA)));
    obj.set("volume", JSON(static_cast<double>(lane.volume)));
    obj.set("pan", JSON(static_cast<double>(lane.pan)));
    obj.set("muted", JSON(lane.muted));
    obj.set("solo", JSON(lane.solo));
    obj.set("height", JSON(static_cast<double>(lane.height)));
    obj.set("collapsed", JSON(lane.collapsed));
    
    JSON clipsArray = JSON::array();
    for (const auto& clip : lane.clips) {
        clipsArray.push(serializeClip(clip, sourceManager));
    }
    obj.set("clips", clipsArray);
    
    return obj;
}

inline JSON PlaylistSerializer::serializeClip(const ClipInstance& clip,
                                                        const SourceManager& sourceManager) {
    JSON obj = JSON::object();
    
    obj.set("id", JSON(clip.id.toString()));
    
    obj.set("startBeat", JSON(clip.startBeat));
    obj.set("durationBeats", JSON(clip.durationBeats));
    obj.set("sourceStart", JSON(static_cast<double>(clip.edits.sourceStart)));
    obj.set("gain", JSON(static_cast<double>(clip.edits.gainLinear)));
    obj.set("pan", JSON(static_cast<double>(clip.edits.pan)));
    obj.set("muted", JSON(clip.muted));
    obj.set("playbackRate", JSON(clip.edits.playbackRate));
    obj.set("fadeInBeats", JSON(clip.edits.fadeInBeats));
    obj.set("fadeOutBeats", JSON(clip.edits.fadeOutBeats));
    obj.set("color", JSON(static_cast<double>(clip.colorRGBA)));
    obj.set("name", JSON(clip.name));
    
    return obj;
}

inline bool PlaylistSerializer::deserialize(const std::string& json,
                                              PlaylistModel& model,
                                              SourceManager& sourceManager,
                                              AudioLoaderFunc audioLoader) {
    auto root = JSON::parse(json);
    if (!root.isObject()) {
        return false;
    }
    
    model.clear();
    
    // Load project settings
    if (root.has("projectSampleRate")) {
        model.setProjectSampleRate(root["projectSampleRate"].asNumber());
    }
    if (root.has("bpm")) {
        model.setBPM(root["bpm"].asNumber());
    }
    
    // Load lanes
    if (root.has("lanes") && root["lanes"].isArray()) {
        const auto& vec = root["lanes"].asArray(); 
        for (const auto& laneValue : vec) {
            if (laneValue.isObject()) {
                deserializeLane(laneValue, model, sourceManager, audioLoader);
            }
        }
    }
    
    return true;
}

inline bool PlaylistSerializer::deserializeLane(const JSON& obj,
                                                  PlaylistModel& model,
                                                  SourceManager& sourceManager,
                                                  AudioLoaderFunc audioLoader) {
    std::string name = obj.has("name") ? obj["name"].asString() : "Track";
    PlaylistLaneID laneId = model.createLane(name);
    
    PlaylistLane* lane = model.getLane(laneId);
    if (!lane) return false;
    
    if (obj.has("color")) lane->colorRGBA = static_cast<uint32_t>(obj["color"].asNumber());
    if (obj.has("volume")) lane->volume = static_cast<float>(obj["volume"].asNumber());
    if (obj.has("pan")) lane->pan = static_cast<float>(obj["pan"].asNumber());
    if (obj.has("muted")) lane->muted = obj["muted"].asBool();
    if (obj.has("solo")) lane->solo = obj["solo"].asBool();
    if (obj.has("height")) lane->height = static_cast<float>(obj["height"].asNumber());
    if (obj.has("collapsed")) lane->collapsed = obj["collapsed"].asBool();
    
    // Load clips
    if (obj.has("clips") && obj["clips"].isArray()) {
        const auto& vec = obj["clips"].asArray();
        for (const auto& clipValue : vec) {
            if (clipValue.isObject()) {
                deserializeClip(clipValue, laneId, model, sourceManager, audioLoader);
            }
        }
    }
    
    return true;
}

inline bool PlaylistSerializer::deserializeClip(const JSON& obj,
                                                  PlaylistLaneID laneId,
                                                  PlaylistModel& model,
                                                  SourceManager& sourceManager,
                                                  AudioLoaderFunc audioLoader) {
    ClipInstance clip;
    
    if (obj.has("id")) {
        clip.id = ClipInstanceID::fromString(obj["id"].asString());
        if (!clip.id.isValid()) {
            clip.id = ClipInstanceID::generate();
        }
    }
    
    if (obj.has("startBeat")) clip.startBeat = obj["startBeat"].asNumber();
    if (obj.has("durationBeats")) clip.durationBeats = obj["durationBeats"].asNumber();
    if (obj.has("sourceStart")) clip.edits.sourceStart = static_cast<SampleIndex>(obj["sourceStart"].asNumber());
    if (obj.has("gain")) clip.edits.gainLinear = static_cast<float>(obj["gain"].asNumber());
    if (obj.has("pan")) clip.edits.pan = static_cast<float>(obj["pan"].asNumber());
    if (obj.has("muted")) clip.muted = obj["muted"].asBool();
    if (obj.has("playbackRate")) clip.edits.playbackRate = obj["playbackRate"].asNumber();
    if (obj.has("fadeInBeats")) clip.edits.fadeInBeats = obj["fadeInBeats"].asNumber();
    if (obj.has("fadeOutBeats")) clip.edits.fadeOutBeats = obj["fadeOutBeats"].asNumber();
    if (obj.has("color")) clip.colorRGBA = static_cast<uint32_t>(obj["color"].asNumber());
    if (obj.has("name")) clip.name = obj["name"].asString();
    
    model.addClip(laneId, clip);
    
    return true;
}


inline bool PlaylistSerializer::saveToFile(const std::string& filepath,
                                            const PlaylistModel& model,
                                            const SourceManager& sourceManager) {
    std::string json = serialize(model, sourceManager);
    std::string tempPath = filepath + ".tmp";
    
    // 1. Write to temporary file
    {
        std::ofstream file(tempPath, std::ios::out | std::ios::trunc);
        if (!file.is_open()) {
            return false;
        }
        
        file << json;
        
        // Check for write errors
        if (file.fail()) {
            file.close();
            std::filesystem::remove(tempPath);
            return false;
        }
        
        // Ensure flush to disk
        file.flush();
        file.close();
    }
    
    // 2. Atomic Rename (Replace existing)
    try {
        // std::filesystem::rename is atomic on POSIX.
        // On Windows it typically uses MoveFileEx(MOVEFILE_REPLACE_EXISTING).
        std::filesystem::rename(tempPath, filepath);
        return true;
    } catch (const std::filesystem::filesystem_error&) {
        // Log error (needs logger access, but for now just cleanup)
        // If rename fails, we MUST remove the temp file
        std::error_code ec;
        std::filesystem::remove(tempPath, ec);
        return false;
    }
}

inline bool PlaylistSerializer::loadFromFile(const std::string& filepath,
                                              PlaylistModel& model,
                                              SourceManager& sourceManager,
                                              AudioLoaderFunc audioLoader) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    
    return deserialize(buffer.str(), model, sourceManager, audioLoader);
}

} // namespace Audio
} // namespace Nomad
