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

static bool writeAtomicallyImpl(const std::string& path, const std::string& contents) {
    namespace fs = std::filesystem;

    std::error_code ec;
    fs::path target(path);
    fs::path tmp = target;
    tmp += ".tmp";

    // Ensure parent exists
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), ec);
        ec.clear();
    }

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            Log::error("Project save failed: cannot open temp file: " + tmp.string());
            return false;
        }
        out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        out.flush();
        if (!out) {
            Log::error("Project save failed: write error: " + tmp.string());
            return false;
        }
    }

    // On Windows, rename() fails if the destination exists.
    if (fs::exists(target, ec)) {
        ec.clear();
        fs::remove(target, ec);
        ec.clear();
    }

    fs::rename(tmp, target, ec);
    if (ec) {
        Log::error("Project save failed: cannot replace target: " + target.string() + " (" + ec.message() + ")");
        // Best-effort cleanup
        fs::remove(tmp, ec);
        return false;
    }

    return true;
}

ProjectSerializer::SerializeResult ProjectSerializer::serialize(const std::shared_ptr<TrackManager>& trackManager,
                                                               double tempo,
                                                               double playheadSeconds,
                                                               int indentSpaces,
                                                               const UIState* uiState) {
    SerializeResult result;
    if (!trackManager) return result;

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
            // Ensure JSON-safe paths on Windows (avoid unescaped backslashes).
            s.set("path", JSON(std::filesystem::path(source->getFilePath()).generic_string()));
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
            // Store colors as strings to avoid scientific notation (NomadJSON parser can't parse exponent form).
            ljs.set("color", JSON(std::to_string(lane->colorRGBA)));
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
                cjs.set("color", JSON(std::to_string(clip.colorRGBA)));

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

    // 5. Optional UI state (panels, dialog, etc.)
    if (uiState) {
        JSON ui = JSON::object();

        JSON settings = JSON::object();
        settings.set("visible", JSON(uiState->settingsDialogVisible));
        settings.set("activePage", JSON(uiState->settingsDialogActivePage));
        ui.set("settingsDialog", settings);

        JSON panels = JSON::array();
        for (const auto& p : uiState->panels) {
            JSON pj = JSON::object();
            pj.set("title", JSON(p.title));
            pj.set("x", JSON(p.x));
            pj.set("y", JSON(p.y));
            pj.set("width", JSON(p.width));
            pj.set("height", JSON(p.height));
            pj.set("expandedHeight", JSON(p.expandedHeight));
            pj.set("minimized", JSON(p.minimized));
            pj.set("maximized", JSON(p.maximized));
            pj.set("userPositioned", JSON(p.userPositioned));
            panels.push(pj);
        }
        ui.set("panels", panels);

        root.set("ui", ui);
    }

    result.contents = root.toString(indentSpaces);
    result.ok = true;
    return result;
}

bool ProjectSerializer::writeAtomically(const std::string& path, const std::string& contents) {
    return writeAtomicallyImpl(path, contents);
}

bool ProjectSerializer::save(const std::string& path,
                             const std::shared_ptr<TrackManager>& trackManager,
                             double tempo,
                             double playheadSeconds,
                             const UIState* uiState) {
    auto ser = serialize(trackManager, tempo, playheadSeconds, 2, uiState);
    if (!ser.ok) return false;
    if (!writeAtomicallyImpl(path, ser.contents)) {
        Log::error("Project save failed: " + path);
        return false;
    }
    Log::info("Project saved to " + path);
    return true;
}

ProjectSerializer::LoadResult ProjectSerializer::load(const std::string& path,
                                                      const std::shared_ptr<TrackManager>& trackManager) {
    LoadResult result;
    if (!trackManager) return result;
    if (!std::filesystem::exists(path)) return result;

#if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
    Log::info(std::string("[ProjectLoad] Begin: ") + path);
#endif

    std::ifstream in(path);
    if (!in) return result;
    
    std::string contents((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
#if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Read bytes=" + std::to_string(contents.size()));
#endif
    JSON root = JSON::parse(contents);
    if (!root.isObject()) return result;
#if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Parsed JSON ok");
#endif

    result.tempo = root.has("tempo") ? root["tempo"].asNumber() : 120.0;
    result.playhead = root.has("playhead") ? root["playhead"].asNumber() : 0.0;

    // Optional UI state
    if (root.has("ui") && root["ui"].isObject()) {
        UIState uiState;
        const JSON& ui = root["ui"];

        if (ui.has("settingsDialog") && ui["settingsDialog"].isObject()) {
            const JSON& sd = ui["settingsDialog"];
            if (sd.has("visible") && sd["visible"].isBool()) {
                uiState.settingsDialogVisible = sd["visible"].asBool();
            }
            if (sd.has("activePage") && sd["activePage"].isString()) {
                uiState.settingsDialogActivePage = sd["activePage"].asString();
            }
        }

        if (ui.has("panels") && ui["panels"].isArray()) {
            const JSON& panels = ui["panels"];
            for (size_t i = 0; i < panels.size(); ++i) {
                const JSON& pj = panels[i];
                if (!pj.isObject()) continue;

                PanelState p;
                if (pj.has("title") && pj["title"].isString()) p.title = pj["title"].asString();
                if (pj.has("x")) p.x = pj["x"].asNumber();
                if (pj.has("y")) p.y = pj["y"].asNumber();
                if (pj.has("width")) p.width = pj["width"].asNumber();
                if (pj.has("height")) p.height = pj["height"].asNumber();
                if (pj.has("expandedHeight")) p.expandedHeight = pj["expandedHeight"].asNumber();
                if (pj.has("minimized") && pj["minimized"].isBool()) p.minimized = pj["minimized"].asBool();
                if (pj.has("maximized") && pj["maximized"].isBool()) p.maximized = pj["maximized"].asBool();
                if (pj.has("userPositioned") && pj["userPositioned"].isBool()) p.userPositioned = pj["userPositioned"].asBool();

                if (!p.title.empty()) uiState.panels.push_back(std::move(p));
            }
        }

        result.ui = std::move(uiState);
    }

    auto& playlist = trackManager->getPlaylistModel();
    auto& sourceManager = trackManager->getSourceManager();
    auto& patternManager = trackManager->getPatternManager();

    auto batch = playlist.scopedBatchUpdate();

#if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
    Log::info("[ProjectLoad] Clearing existing state");
#endif

    playlist.clear();
    sourceManager.clear();
    // patternManager.clear(); // TODO: If clear exists

    // 1. Load Sources
    std::unordered_map<uint32_t, ClipSourceID> idMap;
    if (root.has("sources")) {
        const JSON& sj = root["sources"];
    #if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
        Log::info("[ProjectLoad] Loading sources count=" + std::to_string(sj.size()));
    #endif
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
    #if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
        Log::info("[ProjectLoad] Loading patterns count=" + std::to_string(pj.size()));
    #endif
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
    #if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
        Log::info("[ProjectLoad] Loading lanes count=" + std::to_string(lj.size()));
    #endif
        for (size_t i = 0; i < lj.size(); ++i) {
    #if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
            Log::info("[ProjectLoad] Lane[" + std::to_string(i) + "] name='" + lj[i]["name"].asString() + "'");
    #endif
            PlaylistLaneID laneId = playlist.createLane(lj[i]["name"].asString());
            if (auto* lane = playlist.getLane(laneId)) {
                if (lj[i]["color"].isString()) {
                    lane->colorRGBA = static_cast<uint32_t>(std::stoul(lj[i]["color"].asString()));
                } else {
                    lane->colorRGBA = static_cast<uint32_t>(lj[i]["color"].asNumber());
                }
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
#if defined(NOMAD_ENABLE_PROJECT_LOAD_LOGS)
                    Log::info("[ProjectLoad]  clips count=" + std::to_string(cj.size()));
#endif
                    for (size_t c = 0; c < cj.size(); ++c) {
                        uint64_t oldPatId = static_cast<uint64_t>(cj[c]["patternId"].asNumber());
                        if (patternMap.count(oldPatId)) {
                            ClipInstance clip;
                            clip.id = ClipInstanceID::fromString(cj[c]["id"].asString());
                            clip.patternId = patternMap[oldPatId];
                            clip.startBeat = cj[c]["start"].asNumber();
                            clip.durationBeats = cj[c]["duration"].asNumber();
                            clip.name = cj[c]["name"].asString();
                            if (cj[c]["color"].isString()) {
                                clip.colorRGBA = static_cast<uint32_t>(std::stoul(cj[c]["color"].asString()));
                            } else {
                                clip.colorRGBA = static_cast<uint32_t>(cj[c]["color"].asNumber());
                            }

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

