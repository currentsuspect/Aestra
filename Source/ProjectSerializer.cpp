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

    auto& playlist = trackManager->getPlaylistModel();
    auto& sourceManager = trackManager->getSourceManager();
    auto& patternManager = trackManager->getPatternManager();

    // 1. Save Sources
    JSON sourcesJson = JSON::array();
    std::vector<ClipSourceID> sourceIds = sourceManager.getAllSourceIDs();
    for (const auto& id : sourceIds) {
        if (const auto* source = sourceManager.getSource(id)) {
            JSON s = JSON::object();
            s.set("id", JSON(static_cast<double>(id.value)));
            s.set("path", JSON(source->getFilePath()));
            s.set("name", JSON(source->getName()));
            sourcesJson.push(s);
        }
    }
    root.set("sources", sourcesJson);

    // 2. Save Patterns
    JSON patternsJson = JSON::array();
    std::vector<std::shared_ptr<PatternSource>> patterns = patternManager.getAllPatterns();
    for (const auto& p : patterns) {
        JSON pjs = JSON::object();
        pjs.set("id", JSON(static_cast<double>(p->id.value)));
        pjs.set("name", JSON(p->name));
        pjs.set("length", JSON(p->lengthBeats));
        
        if (p->isAudio()) {
            pjs.set("type", JSON("audio"));
            const auto& payload = std::get<AudioSlicePayload>(p->payload);
            pjs.set("sourceId", JSON(static_cast<double>(payload.audioSourceId.value)));
            
            JSON slicesArray = JSON::array();
            for (const auto& slice : payload.slices) {
                JSON sl = JSON::object();
                sl.set("start", JSON(slice.startSamples));
                sl.set("length", JSON(slice.lengthSamples));
                slicesArray.push(sl);
            }
            pjs.set("slices", slicesArray);
        } else {
            pjs.set("type", JSON("midi"));
            // TODO: Save MIDI notes
        }
        patternsJson.push(pjs);
    }
    root.set("patterns", patternsJson);

    // 3. Save Lanes and Clips
    JSON lanesJson = JSON::array();
    for (const auto& laneId : playlist.getLaneIDs()) {
        if (const auto* lane = playlist.getLane(laneId)) {
            JSON ljs = JSON::object();
            ljs.set("id", JSON(lane->id.toString()));
            ljs.set("name", JSON(lane->name));
            ljs.set("color", JSON(static_cast<double>(lane->colorRGBA)));
            ljs.set("volume", JSON(lane->volume));
            ljs.set("pan", JSON(lane->pan));
            ljs.set("mute", JSON(lane->muted));
            ljs.set("solo", JSON(lane->solo));

            // Automation (v3.1)
            JSON autoJson = JSON::array();
            for (const auto& curve : lane->automationCurves) {
                JSON cj = JSON::object();
                cj.set("param", JSON(curve.getTarget()));
                cj.set("targetEnum", JSON(static_cast<double>(curve.getAutomationTarget())));
                cj.set("default", JSON(curve.getDefaultValue()));
                
                JSON ptsJson = JSON::array();
                for (const auto& p : curve.getPoints()) {
                    JSON pj = JSON::object();
                    pj.set("b", JSON(p.beat));
                    pj.set("v", JSON(p.value));
                    pj.set("c", JSON(static_cast<double>(p.curve)));
                    ptsJson.push(pj);
                }
                cj.set("points", ptsJson);
                autoJson.push(cj);
            }
            ljs.set("automation", autoJson);

            JSON clipsJson = JSON::array();
            for (const auto& clip : lane->clips) {
                JSON cjs = JSON::object();
                cjs.set("id", JSON(clip.id.toString()));
                cjs.set("patternId", JSON(static_cast<double>(clip.patternId.value)));
                cjs.set("start", JSON(clip.startBeat));
                cjs.set("duration", JSON(clip.durationBeats));
                cjs.set("name", JSON(clip.name));
                cjs.set("color", JSON(static_cast<double>(clip.colorRGBA)));

                // Edits
                JSON ejs = JSON::object();
                ejs.set("gain", JSON(static_cast<double>(clip.edits.gainLinear)));
                ejs.set("pan", JSON(static_cast<double>(clip.edits.pan)));
                ejs.set("muted", JSON(clip.edits.muted));
                ejs.set("playbackRate", JSON(static_cast<double>(clip.edits.playbackRate)));
                ejs.set("fadeIn", JSON(clip.edits.fadeInBeats));
                ejs.set("fadeOut", JSON(clip.edits.fadeOutBeats));
                ejs.set("sourceStart", JSON(static_cast<double>(clip.edits.sourceStart)));
                cjs.set("edits", ejs);

                clipsJson.push(cjs);
            }
            ljs.set("clips", clipsJson);
            lanesJson.push(ljs);
        }
    }
    root.set("lanes", lanesJson);

    // 4. Save Arsenal Units
    root.set("arsenal", trackManager->getUnitManager().saveToJSON());

    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        Log::error("Project save failed: " + path);
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
    if (!std::filesystem::exists(path)) return result;

    std::ifstream in(path);
    if (!in) return result;
    
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    JSON root = JSON::parse(contents);
    if (!root.isObject()) return result;

    result.tempo = root.has("tempo") ? root["tempo"].asNumber() : 120.0;
    result.playhead = root.has("playhead") ? root["playhead"].asNumber() : 0.0;

    auto& playlist = trackManager->getPlaylistModel();
    auto& sourceManager = trackManager->getSourceManager();
    auto& patternManager = trackManager->getPatternManager();

    playlist.clear();
    sourceManager.clear();
    // patternManager.clear(); // TODO: If clear exists

    // 1. Load Sources
    std::unordered_map<uint32_t, ClipSourceID> idMap;
    if (root.has("sources")) {
        const JSON& sj = root["sources"];
        for (size_t i = 0; i < sj.size(); ++i) {
            uint32_t oldId = static_cast<uint32_t>(sj[i]["id"].asNumber());
            std::string filePath = sj[i]["path"].asString();
            ClipSourceID newId = sourceManager.getOrCreateSource(filePath);
            idMap[oldId] = newId;
        }
    }

    // 2. Load Patterns
    std::unordered_map<uint64_t, PatternID> patternMap;
    if (root.has("patterns")) {
        const JSON& pj = root["patterns"];
        for (size_t i = 0; i < pj.size(); ++i) {
            uint64_t oldId = static_cast<uint64_t>(pj[i]["id"].asNumber());
            std::string name = pj[i]["name"].asString();
            double length = pj[i]["length"].asNumber();
            std::string type = pj[i]["type"].asString();

            if (type == "audio") {
                uint32_t oldSrcId = static_cast<uint32_t>(pj[i]["sourceId"].asNumber());
                if (idMap.count(oldSrcId)) {
                    AudioSlicePayload payload;
                    payload.audioSourceId = idMap[oldSrcId];
                    if (pj[i].has("slices")) {
                        const JSON& slj = pj[i]["slices"];
                        for (size_t s = 0; s < slj.size(); ++s) {
                            payload.slices.push_back({slj[s]["start"].asNumber(), slj[s]["length"].asNumber()});
                        }
                    }
                    PatternID newId = patternManager.createAudioPattern(name, length, payload);
                    patternMap[oldId] = newId;
                }
            } else {
                // TODO: Load MIDI patterns
            }
        }
    }

    // 3. Load Lanes and Clips
    if (root.has("lanes")) {
        const JSON& lj = root["lanes"];
        for (size_t i = 0; i < lj.size(); ++i) {
            PlaylistLaneID laneId = playlist.createLane(lj[i]["name"].asString());
            if (auto* lane = playlist.getLane(laneId)) {
                lane->colorRGBA = static_cast<uint32_t>(lj[i]["color"].asNumber());
                lane->volume = static_cast<float>(lj[i]["volume"].asNumber());
                lane->pan = static_cast<float>(lj[i]["pan"].asNumber());
                lane->muted = lj[i]["mute"].asBool();
                lane->solo = lj[i]["solo"].asBool();

                if (lj[i].has("automation")) {
                    const JSON& aj = lj[i]["automation"];
                    for (size_t a = 0; a < aj.size(); ++a) {
                        std::string param = aj[a]["param"].asString();
                        auto target = static_cast<AutomationTarget>(aj[a]["targetEnum"].asNumber());
                        
                        AutomationCurve curve(param, target);
                        curve.setDefaultValue(aj[a]["default"].asNumber());
                        
                        const JSON& pts = aj[a]["points"];
                        for (size_t p = 0; p < pts.size(); ++p) {
                            curve.addPoint(pts[p]["b"].asNumber(), 
                                         pts[p]["v"].asNumber(), 
                                         static_cast<float>(pts[p]["c"].asNumber()));
                        }
                        lane->automationCurves.push_back(curve);
                    }
                }

                if (lj[i].has("clips")) {
                    const JSON& cj = lj[i]["clips"];
                    for (size_t c = 0; c < cj.size(); ++c) {
                        uint64_t oldPatId = static_cast<uint64_t>(cj[c]["patternId"].asNumber());
                        if (patternMap.count(oldPatId)) {
                            ClipInstance clip;
                            clip.id = ClipInstanceID::fromString(cj[c]["id"].asString());
                            clip.patternId = patternMap[oldPatId];
                            clip.startBeat = cj[c]["start"].asNumber();
                            clip.durationBeats = cj[c]["duration"].asNumber();
                            clip.name = cj[c]["name"].asString();
                            clip.colorRGBA = static_cast<uint32_t>(cj[c]["color"].asNumber());

                            if (cj[c].has("edits")) {
                                const JSON& ej = cj[c]["edits"];
                                clip.edits.gainLinear = static_cast<float>(ej["gain"].asNumber());
                                clip.edits.pan = static_cast<float>(ej["pan"].asNumber());
                                clip.edits.muted = ej["muted"].asBool();
                                clip.edits.playbackRate = static_cast<float>(ej["playbackRate"].asNumber());
                                clip.edits.fadeInBeats = ej["fadeIn"].asNumber();
                                clip.edits.fadeOutBeats = ej["fadeOut"].asNumber();
                                clip.edits.sourceStart = ej["sourceStart"].asNumber();
                            }
                            playlist.addClip(laneId, clip);
                        }
                    }
                }
            }
        }
    }

    // 4. Load Arsenal Units
    if (root.has("arsenal")) {
        trackManager->getUnitManager().loadFromJSON(root["arsenal"]);
    }

    result.ok = true;
    Log::info("Project loaded: " + path);
    return result;
}

