// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PlaylistModel.h"
#include "ClipSource.h"
#include "NomadJSON.h"
#include <string>

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
    
    static JSON::Object serializeLane(const PlaylistLane& lane, 
                                       const SourceManager& sourceManager);
    static JSON::Object serializeClip(const PlaylistClip& clip,
                                       const SourceManager& sourceManager);
    
    static bool deserializeLane(const JSON::Object& obj, 
                                PlaylistModel& model,
                                SourceManager& sourceManager,
                                AudioLoaderFunc audioLoader);
    static bool deserializeClip(const JSON::Object& obj,
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
    JSON::Object root;
    
    root["projectSampleRate"] = JSON::Value(model.getProjectSampleRate());
    root["bpm"] = JSON::Value(model.getBPM());
    root["gridSubdivision"] = JSON::Value(gridSubdivisionToString(model.getGridSubdivision()));
    root["snapEnabled"] = JSON::Value(model.isSnapEnabled());
    
    JSON::Array lanesArray;
    auto laneIds = model.getLaneIDs();
    
    for (const auto& laneId : laneIds) {
        const PlaylistLane* lane = model.getLane(laneId);
        if (lane) {
            lanesArray.push_back(JSON::Value(serializeLane(*lane, sourceManager)));
        }
    }
    
    root["lanes"] = JSON::Value(std::move(lanesArray));
    
    return JSON::stringify(JSON::Value(std::move(root)), true);
}

inline JSON::Object PlaylistSerializer::serializeLane(const PlaylistLane& lane,
                                                        const SourceManager& sourceManager) {
    JSON::Object obj;
    
    obj["id"] = JSON::Value(lane.id.toString());
    obj["name"] = JSON::Value(lane.name);
    obj["color"] = JSON::Value(static_cast<double>(lane.colorRGBA));
    obj["volume"] = JSON::Value(static_cast<double>(lane.volume));
    obj["pan"] = JSON::Value(static_cast<double>(lane.pan));
    obj["muted"] = JSON::Value(lane.muted);
    obj["solo"] = JSON::Value(lane.solo);
    obj["height"] = JSON::Value(static_cast<double>(lane.height));
    obj["collapsed"] = JSON::Value(lane.collapsed);
    
    JSON::Array clipsArray;
    for (const auto& clip : lane.clips) {
        clipsArray.push_back(JSON::Value(serializeClip(clip, sourceManager)));
    }
    obj["clips"] = JSON::Value(std::move(clipsArray));
    
    return obj;
}

inline JSON::Object PlaylistSerializer::serializeClip(const PlaylistClip& clip,
                                                        const SourceManager& sourceManager) {
    JSON::Object obj;
    
    obj["id"] = JSON::Value(clip.id.toString());
    
    // Resolve source file path
    const ClipSource* source = sourceManager.getSource(clip.sourceId);
    std::string sourcePath = source ? source->getFilePath() : "";
    obj["sourceFile"] = JSON::Value(sourcePath);
    
    obj["startTime"] = JSON::Value(static_cast<double>(clip.startTime));
    obj["length"] = JSON::Value(static_cast<double>(clip.length));
    obj["sourceStart"] = JSON::Value(static_cast<double>(clip.sourceStart));
    obj["gain"] = JSON::Value(static_cast<double>(clip.gainLinear));
    obj["pan"] = JSON::Value(static_cast<double>(clip.pan));
    obj["muted"] = JSON::Value(clip.muted);
    obj["playbackRate"] = JSON::Value(clip.playbackRate);
    obj["fadeInLength"] = JSON::Value(static_cast<double>(clip.fadeInLength));
    obj["fadeOutLength"] = JSON::Value(static_cast<double>(clip.fadeOutLength));
    obj["flags"] = JSON::Value(static_cast<double>(clip.flags));
    obj["color"] = JSON::Value(static_cast<double>(clip.colorRGBA));
    obj["name"] = JSON::Value(clip.name);
    
    return obj;
}

inline bool PlaylistSerializer::deserialize(const std::string& json,
                                              PlaylistModel& model,
                                              SourceManager& sourceManager,
                                              AudioLoaderFunc audioLoader) {
    auto result = JSON::parse(json);
    if (!result.has_value() || !result->isObject()) {
        return false;
    }
    
    model.clear();
    
    const JSON::Object& root = result->asObject();
    
    // Load project settings
    if (root.count("projectSampleRate")) {
        model.setProjectSampleRate(root.at("projectSampleRate").asNumber());
    }
    if (root.count("bpm")) {
        model.setBPM(root.at("bpm").asNumber());
    }
    if (root.count("gridSubdivision")) {
        model.setGridSubdivision(stringToGridSubdivision(root.at("gridSubdivision").asString()));
    }
    if (root.count("snapEnabled")) {
        model.setSnapEnabled(root.at("snapEnabled").asBool());
    }
    
    // Load lanes
    if (root.count("lanes") && root.at("lanes").isArray()) {
        const JSON::Array& lanesArray = root.at("lanes").asArray();
        for (const auto& laneValue : lanesArray) {
            if (laneValue.isObject()) {
                deserializeLane(laneValue.asObject(), model, sourceManager, audioLoader);
            }
        }
    }
    
    return true;
}

inline bool PlaylistSerializer::deserializeLane(const JSON::Object& obj,
                                                  PlaylistModel& model,
                                                  SourceManager& sourceManager,
                                                  AudioLoaderFunc audioLoader) {
    std::string name = obj.count("name") ? obj.at("name").asString() : "Track";
    PlaylistLaneID laneId = model.createLane(name);
    
    PlaylistLane* lane = model.getLane(laneId);
    if (!lane) return false;
    
    if (obj.count("color")) lane->colorRGBA = static_cast<uint32_t>(obj.at("color").asNumber());
    if (obj.count("volume")) lane->volume = static_cast<float>(obj.at("volume").asNumber());
    if (obj.count("pan")) lane->pan = static_cast<float>(obj.at("pan").asNumber());
    if (obj.count("muted")) lane->muted = obj.at("muted").asBool();
    if (obj.count("solo")) lane->solo = obj.at("solo").asBool();
    if (obj.count("height")) lane->height = static_cast<float>(obj.at("height").asNumber());
    if (obj.count("collapsed")) lane->collapsed = obj.at("collapsed").asBool();
    
    // Load clips
    if (obj.count("clips") && obj.at("clips").isArray()) {
        const JSON::Array& clipsArray = obj.at("clips").asArray();
        for (const auto& clipValue : clipsArray) {
            if (clipValue.isObject()) {
                deserializeClip(clipValue.asObject(), laneId, model, sourceManager, audioLoader);
            }
        }
    }
    
    return true;
}

inline bool PlaylistSerializer::deserializeClip(const JSON::Object& obj,
                                                  PlaylistLaneID laneId,
                                                  PlaylistModel& model,
                                                  SourceManager& sourceManager,
                                                  AudioLoaderFunc audioLoader) {
    // Get source file path
    std::string sourceFile = obj.count("sourceFile") ? obj.at("sourceFile").asString() : "";
    if (sourceFile.empty()) {
        return false;
    }
    
    // Get or create source
    ClipSourceID sourceId = sourceManager.getOrCreateSource(sourceFile);
    ClipSource* source = sourceManager.getSource(sourceId);
    
    // Load audio if not already loaded
    if (source && !source->isReady() && audioLoader) {
        auto buffer = audioLoader(sourceFile);
        if (buffer) {
            source->setBuffer(buffer);
        }
    }
    
    // Create clip
    PlaylistClip clip(sourceId);
    
    if (obj.count("id")) {
        clip.id = PlaylistClipID::fromString(obj.at("id").asString());
        if (!clip.id.isValid()) {
            clip.id = PlaylistClipID::generate();
        }
    }
    
    if (obj.count("startTime")) clip.startTime = static_cast<SampleIndex>(obj.at("startTime").asNumber());
    if (obj.count("length")) clip.length = static_cast<SampleIndex>(obj.at("length").asNumber());
    if (obj.count("sourceStart")) clip.sourceStart = static_cast<SampleIndex>(obj.at("sourceStart").asNumber());
    if (obj.count("gain")) clip.gainLinear = static_cast<float>(obj.at("gain").asNumber());
    if (obj.count("pan")) clip.pan = static_cast<float>(obj.at("pan").asNumber());
    if (obj.count("muted")) clip.muted = obj.at("muted").asBool();
    if (obj.count("playbackRate")) clip.playbackRate = obj.at("playbackRate").asNumber();
    if (obj.count("fadeInLength")) clip.fadeInLength = static_cast<SampleIndex>(obj.at("fadeInLength").asNumber());
    if (obj.count("fadeOutLength")) clip.fadeOutLength = static_cast<SampleIndex>(obj.at("fadeOutLength").asNumber());
    if (obj.count("flags")) clip.flags = static_cast<uint32_t>(obj.at("flags").asNumber());
    if (obj.count("color")) clip.colorRGBA = static_cast<uint32_t>(obj.at("color").asNumber());
    if (obj.count("name")) clip.name = obj.at("name").asString();
    
    model.addClip(laneId, clip);
    
    return true;
}

inline bool PlaylistSerializer::saveToFile(const std::string& filepath,
                                            const PlaylistModel& model,
                                            const SourceManager& sourceManager) {
    std::string json = serialize(model, sourceManager);
    
    std::ofstream file(filepath);
    if (!file.is_open()) {
        return false;
    }
    
    file << json;
    return file.good();
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
