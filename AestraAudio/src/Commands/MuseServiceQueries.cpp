// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Session state reads: transport, tracks, clips, units.
//

//
// Part of the split that took MuseService.cpp from one 2200-line dispatch
// chain to one file per family. See Commands/MuseServiceInternal.h for why, and
// for the contract these handlers share.
//
// The body below is the text it was before the move, dedented by four spaces
// and otherwise untouched. If you change a verb's behaviour, change it here.

#include "Commands/MuseServiceInternal.h"
#include "Core/AudioEngine.h"
#include "Models/TrackManager.h"

#include "AestraJSON.h"
#include <optional>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {

// Inside Aestra::Audio, as it was in MuseService.cpp: JSON is Aestra::JSON, so
// these helpers cannot sit at global scope and still name it unqualified.

namespace {

JSON trackToJson(const MixerChannel& channel, size_t index) {
    JSON t = JSON::object();
    t.set("index", JSON(static_cast<double>(index)));
    t.set("id", JSON(static_cast<double>(channel.getChannelId())));
    t.set("name", JSON(channel.getName()));
    t.set("volume", JSON(static_cast<double>(channel.getVolume())));
    t.set("pan", JSON(static_cast<double>(channel.getPan())));
    t.set("muted", JSON(channel.isMuted()));
    t.set("soloed", JSON(channel.isSoloed()));
    return t;
}

} // namespace

namespace MuseInternal {

std::optional<std::string> handleSessionVerbs(const RequestContext& ctx, const ResponseEnvelope& env) {
    // Bound to the names the moved bodies already use, so no verb body had to be
    // edited to make the move.
    const double id = ctx.id;
    const std::string& verb = ctx.verb;
    JSON& request = *ctx.request;
    TrackManager* const m_trackManager = ctx.trackManager;
    AudioEngine* const m_engine = ctx.engine;
    // A reference, not a pointer: get_project_load_report tests this for a value
    // and then dereferences it, so it has to carry std::optional's own semantics.
    // A pointer would be truthy whenever the address is non-null — including when
    // the optional it points at is empty — which is not the same question.
    const std::optional<JSON>& m_projectLoadReport = *ctx.projectLoadReport;

    const auto makeOk = [&env]() { return env.ok(); };
    const auto finish = [&env](JSON& response) { return env.finish(response); };

    if (verb == "get_transport") {
        JSON result = JSON::object();
        if (m_engine) {
            result.set("bpm", JSON(static_cast<double>(m_engine->getBPM())));
            result.set("playing", JSON(m_engine->isTransportPlaying()));
            result.set("positionSeconds", JSON(m_engine->getPositionSeconds()));
        } else if (m_trackManager) {
            result.set("bpm", JSON(m_trackManager->getTimelineClock().getCurrentTempo()));
            result.set("playing", JSON(m_trackManager->isPlaying()));
            result.set("positionSeconds", JSON(m_trackManager->getPosition()));
        } else {
            return makeError(id, "execution_error", "no engine or track manager", verb).toString();
        }
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "list_tracks") {
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }
        JSON tracks = JSON::array();
        const size_t count = m_trackManager->getChannelCount();
        for (size_t i = 0; i < count; ++i) {
            if (const MixerChannel* ch = m_trackManager->getChannel(i)) {
                tracks.push(trackToJson(*ch, i));
            }
        }
        JSON result = JSON::object();
        result.set("tracks", tracks);
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "list_clips") {
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }
        auto& playlist = m_trackManager->getPlaylistModel();
        JSON lanes = JSON::array();
        const size_t laneCount = playlist.getLaneCount();
        for (size_t i = 0; i < laneCount; ++i) {
            PlaylistLaneID laneId = playlist.getLaneId(i);
            PlaylistLane* lane = playlist.getLane(laneId);
            if (!lane) continue;
            JSON laneJson = JSON::object();
            laneJson.set("index", JSON(static_cast<double>(i)));
            laneJson.set("id", JSON(laneId.toString())); // full UUID — lossless
            laneJson.set("name", JSON(lane->name));
            JSON clips = JSON::array();
            for (const auto& clip : lane->clips) {
                JSON c = JSON::object();
                c.set("id", JSON(clip.id.toString())); // full UUID — lossless
                c.set("name", JSON(clip.name));
                c.set("startBeat", JSON(clip.startBeat));
                c.set("durationBeats", JSON(clip.durationBeats));
                // 0 = not a pattern clip
                c.set("pattern", JSON(static_cast<double>(clip.patternId.value)));
                clips.push(c);
            }
            laneJson.set("clips", clips);
            lanes.push(laneJson);
        }
        JSON result = JSON::object();
        result.set("lanes", lanes);
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "get_session_state") {
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }
        JSON result = JSON::object();

        JSON transport = JSON::object();
        if (m_engine) {
            transport.set("bpm", JSON(static_cast<double>(m_engine->getBPM())));
            transport.set("playing", JSON(m_engine->isTransportPlaying()));
        } else {
            transport.set("bpm", JSON(m_trackManager->getTimelineClock().getCurrentTempo()));
            transport.set("playing", JSON(m_trackManager->isPlaying()));
        }
        result.set("transport", transport);

        JSON tracks = JSON::array();
        const size_t count = m_trackManager->getChannelCount();
        for (size_t i = 0; i < count; ++i) {
            if (const MixerChannel* ch = m_trackManager->getChannel(i)) {
                tracks.push(trackToJson(*ch, i));
            }
        }
        result.set("tracks", tracks);
        result.set("laneCount",
                   JSON(static_cast<double>(m_trackManager->getPlaylistModel().getLaneCount())));
        result.set("unitCount",
                   JSON(static_cast<double>(m_trackManager->getUnitManager().getUnitCount())));
        result.set("canUndo", JSON(m_trackManager->getCommandHistory().canUndo()));

        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }

    if (verb == "list_units") {
        if (!m_trackManager) {
            return makeError(id, "execution_error", "no track manager", verb).toString();
        }
        auto& unitManager = m_trackManager->getUnitManager();
        JSON units = JSON::array();
        for (UnitID unitId : unitManager.getAllUnitIDs()) {
            const UnitInfo* unit = unitManager.getUnit(unitId);
            if (!unit) continue;
            JSON u = JSON::object();
            u.set("id", JSON(static_cast<double>(unit->id)));
            u.set("name", JSON(unit->name));
            u.set("type", JSON(unitTypeName(unit->type)));
            u.set("enabled", JSON(unit->isEnabled));
            u.set("gain", JSON(static_cast<double>(unit->gain)));
            u.set("muted", JSON(unit->isMuted));
            u.set("soloed", JSON(unit->isSolo));
            u.set("defaultPatternId",
                  JSON(static_cast<double>(unit->defaultPatternId.value)));
            u.set("mixerChannelId", JSON(static_cast<double>(unitManager.getUnitMixerChannel(unitId))));
            // Compatibility metadata retained for older clients. It no longer controls audio routing.
            u.set("timelineLane",
                  JSON(static_cast<double>(unitManager.getUnitTimelineLane(unitId))));
            u.set("samplePath", JSON(unit->audioClipPath));
            u.set("sampleDurationSeconds", JSON(unit->audioDurationSeconds));
            units.push(u);
        }
        JSON result = JSON::object();
        result.set("units", units);
        JSON response = makeOk();
        response.set("result", result);
        return finish(response);
    }
    // No verb matched. In the single-file version this fell out of the if-chain
    // into the next family's checks and eventually the mutation path, which is
    // exactly what nullopt asks handleRequest to do.
    return std::nullopt;
}

} // namespace MuseInternal
} // namespace Audio
} // namespace Aestra
