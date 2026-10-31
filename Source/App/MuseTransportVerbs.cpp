// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// transport.* host verbs: play, stop, pause, seek and loop the way the app's
// own transport does them (V82-M1, FD-53).
//
// The legacy `play`/`stop` verbs start and stop the audio engine directly. In the
// running app that is not what the Play button does: the button goes through
// AestraContent's focus-aware transport, which also moves the playhead, honours
// the count-in and picks the Arsenal or the Timeline. An agent driving the app
// for a screen recording got meters but a frozen playhead. These verbs call the
// same functions the transport bar and the ruler call, so the two cannot drift.
#include "MuseHostVerbs.h"

#include "AestraApp.h"
#include "Commands/HostVerbRegistry.h"
#include "Commands/MuseService.h"
#include "TrackManagerUI.h"
#include "Core/AestraContent.h"
#include "Core/AudioEngine.h"
#include "Core/WorkspaceFocus.h"
#include "Models/TrackManager.h"

#include "AestraLog.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Aestra {
namespace {

using Audio::HostThreadAffinity;
using Audio::HostVerbArg;
using Audio::HostVerbDomain;
using Audio::HostVerbResult;
using Audio::HostVerbSpec;

HostVerbArg numberArg(const char* name, bool required, const char* description) {
    HostVerbArg arg;
    arg.name = name;
    arg.type = Audio::FlagType::Float;
    arg.required = required;
    arg.description = description;
    return arg;
}

HostVerbSpec transportSpec(const char* name, bool mutates, std::string description) {
    HostVerbSpec spec;
    spec.name = name;
    spec.domain = HostVerbDomain::Transport;
    spec.affinity = HostThreadAffinity::HostUiThread;
    // Playhead, loop and play state are session navigation, not project edits:
    // the UI does not put them on the undo stack either.
    spec.mutates = mutates;
    spec.description = std::move(description);
    return spec;
}

void registerOrLog(Audio::MuseService& service, HostVerbSpec spec, Audio::HostVerbHandler handler) {
    std::string error;
    const auto status = service.hostVerbs().registerVerb(std::move(spec), std::move(handler), error);
    if (status != Audio::HostVerbRegistry::RegisterStatus::Ok) {
        Log::warning("[MuseTransportVerbs] refused: " + error);
    }
}

// The two transports a play can mean. Audition has its own player and RoutingMap
// has none, so neither is a valid target for "play from beat N".
bool parsePlayTarget(const std::string& name, ViewFocus& out) {
    if (name == "timeline") {
        out = ViewFocus::Timeline;
        return true;
    }
    if (name == "arsenal") {
        out = ViewFocus::Arsenal;
        return true;
    }
    return false;
}

double tempoBpm(Audio::TrackManager& tm) {
    return std::max(1.0, tm.getTimelineClock().getCurrentTempo());
}

// The playhead in beats, read the way the focused surface reads it: the Arsenal
// maps seconds straight through the tempo, the Timeline through the playlist's
// tempo map.
double positionBeats(::AestraContent& content, Audio::TrackManager& tm) {
    const double seconds = std::max(0.0, tm.getPosition());
    if (content.getViewFocus() == ViewFocus::Arsenal) {
        return seconds * tempoBpm(tm) / 60.0;
    }
    return tm.getPlaylistModel().secondsToBeats(seconds);
}

// Move the playhead as the ruler click (Timeline) or the progress-header scrub
// (Arsenal) does. Both end the same way: the UI cue AND the engine's sample
// position move. A ruler drag reaches the engine through the app loop's
// per-frame scrub sync while the button is held; a one-shot seek has no held
// button, so it moves the engine itself — without that, a seek while playing
// is overwritten by the engine position on the next frame.
// Beats past this are not a session position (about 2.5 hours at 400 BPM);
// refusing them also keeps the sample-position cast below well defined.
constexpr double kMaxSeekBeat = 60000.0;

// Why a seek to @p beat cannot happen in the current focus, or empty when it can.
std::string seekRefusal(::AestraContent& content, double beat) {
    const ViewFocus focus = content.getViewFocus();
    if (focus != ViewFocus::Timeline && focus != ViewFocus::Arsenal) {
        return std::string("the ") + WorkspaceFocusModel::workspaceFocusName(focus) +
               " workspace has no beat transport; use transport.play {in: timeline|arsenal}";
    }
    if (!std::isfinite(beat) || beat < 0.0 || beat > kMaxSeekBeat) {
        return "beat must be between 0 and " + std::to_string(static_cast<int>(kMaxSeekBeat));
    }
    return {};
}

void seekTo(::AestraContent& content, Audio::TrackManager& tm, Audio::AudioEngine* engine, double beat) {
    const double seconds = content.getViewFocus() == ViewFocus::Arsenal
                               ? std::max(0.0, beat * 60.0 / tempoBpm(tm))
                               : std::max(0.0, tm.getPlaylistModel().beatToSeconds(beat));
    tm.setPosition(seconds);
    tm.setPlayStartPosition(seconds);
    if (engine) {
        // seekRefusal bounds the beat, but a tempo map can still stretch it:
        // only convert a sample position that fits, never cast past uint64_t.
        const double samples = seconds * static_cast<double>(engine->getSampleRate());
        if (std::isfinite(samples) && samples >= 0.0 && samples < 9.0e18) {
            engine->setGlobalSamplePos(static_cast<uint64_t>(samples));
        }
    }
}

JSON transportState(::AestraContent& content, Audio::TrackManager& tm) {
    JSON result = JSON::object();
    result.set("playing", JSON(tm.isPlaying()));
    result.set("countInPending", JSON(tm.isCountInPending()));
    result.set("positionBeats", JSON(positionBeats(content, tm)));
    result.set("positionSeconds", JSON(std::max(0.0, tm.getPosition())));
    result.set("bpm", JSON(tempoBpm(tm)));
    result.set("focus", JSON(std::string(WorkspaceFocusModel::workspaceFocusName(content.getViewFocus()))));
    JSON loop = JSON::object();
    if (auto ui = content.getTrackManagerUI()) {
        loop.set("enabled", JSON(ui->isLoopEnabled()));
        loop.set("startBeat", JSON(ui->getLoopStartBeat()));
        loop.set("endBeat", JSON(ui->getLoopEndBeat()));
    }
    result.set("loop", loop);
    return result;
}

HostVerbResult noTimeline() {
    return HostVerbResult::failure("host_unavailable", "the app has no track manager yet");
}

} // namespace

void registerMuseTransportVerbs(Audio::MuseService& service, ::AestraContent& content, ::AestraApp& app) {
    ::AestraContent* c = &content;
    ::AestraApp* a = &app;

    // --- transport.state -----------------------------------------------------
    registerOrLog(service,
                  transportSpec("transport.state", false,
                                "Where the transport is: playing, countInPending, positionBeats, "
                                "positionSeconds, bpm, focus (which transport Play drives) and the "
                                "timeline loop {enabled, startBeat, endBeat}."),
                  [c](const JSON&) {
                      auto tm = c->getTrackManager();
                      if (!tm) return noTimeline();
                      return HostVerbResult::success(transportState(*c, *tm));
                  });

    // --- transport.play ------------------------------------------------------
    {
        auto spec = transportSpec(
            "transport.play", false,
            "Press Play, exactly as the transport bar does: the playhead moves, the count-in is "
            "honoured, and the focused workspace decides what plays. 'in' switches to 'timeline' or "
            "'arsenal' first; 'from' moves the playhead to that beat first. Already playing is a "
            "success with changed=false.");
        spec.args.push_back(numberArg("from", false, "beat to start from"));
        HostVerbArg in;
        in.name = "in";
        in.type = Audio::FlagType::String;
        in.required = false;
        in.description = "timeline | arsenal";
        spec.args.push_back(in);
        registerOrLog(service, std::move(spec), [c, a](const JSON& args) {
            auto tm = c->getTrackManager();
            if (!tm) return noTimeline();
            // Validate everything before changing anything: a refused request
            // must leave the workspace and playhead as they were.
            ViewFocus target = c->getViewFocus();
            if (args.has("in") && !parsePlayTarget(args["in"].asString(), target)) {
                return HostVerbResult::failure("invalid_args", "'in' must be 'timeline' or 'arsenal'");
            }
            const bool hasFrom = args.has("from");
            const double from = hasFrom ? args["from"].asNumber() : 0.0;
            if (hasFrom && (!std::isfinite(from) || from < 0.0 || from > kMaxSeekBeat)) {
                return HostVerbResult::failure("invalid_args", "'from' must be a beat between 0 and " +
                                                                   std::to_string(static_cast<int>(kMaxSeekBeat)));
            }
            if (hasFrom && target != ViewFocus::Timeline && target != ViewFocus::Arsenal) {
                return HostVerbResult::failure("invalid_args", "'from' needs the timeline or the arsenal; pass 'in'");
            }
            if (c->getViewFocus() != target) {
                c->setViewFocus(target);
            }
            if (hasFrom) {
                seekTo(*c, *tm, a->getAudioEngine(), from);
            }
            const bool wasPlaying = tm->isPlaying() || tm->isCountInPending();
            if (!wasPlaying) {
                c->requestTransportPlay();
            }
            JSON result = transportState(*c, *tm);
            result.set("changed", JSON(!wasPlaying));
            return HostVerbResult::success(result);
        });
    }

    // --- transport.stop / transport.pause ------------------------------------
    {
        auto spec = transportSpec("transport.stop", false,
                                  "Press Stop. rewind=true is the double-stop: the playhead goes back to 0.");
        HostVerbArg rewind;
        rewind.name = "rewind";
        rewind.type = Audio::FlagType::Bool;
        rewind.required = false;
        rewind.description = "return the playhead to beat 0";
        spec.args.push_back(rewind);
        registerOrLog(service, std::move(spec), [c](const JSON& args) {
            auto tm = c->getTrackManager();
            if (!tm) return noTimeline();
            const bool rewindToStart = args.has("rewind") && args["rewind"].asBool();
            c->stopFromCurrentFocus(rewindToStart);
            return HostVerbResult::success(transportState(*c, *tm));
        });
    }
    registerOrLog(service,
                  transportSpec("transport.pause", false,
                                "Pause: playback stops and the playhead stays where it is, so the next "
                                "transport.play resumes from there."),
                  [c](const JSON&) {
                      auto tm = c->getTrackManager();
                      if (!tm) return noTimeline();
                      c->pauseFromCurrentFocus();
                      return HostVerbResult::success(transportState(*c, *tm));
                  });

    // --- transport.seek ------------------------------------------------------
    {
        auto spec = transportSpec("transport.seek", false,
                                  "Move the playhead to a beat, as a click on the ruler does. Works while "
                                  "playing or stopped; the next Play starts from here.");
        spec.args.push_back(numberArg("beat", true, "beat position >= 0"));
        registerOrLog(service, std::move(spec), [c, a](const JSON& args) {
            auto tm = c->getTrackManager();
            if (!tm) return noTimeline();
            const double beat = args["beat"].asNumber();
            if (const std::string refusal = seekRefusal(*c, beat); !refusal.empty()) {
                return HostVerbResult::failure("invalid_args", refusal);
            }
            seekTo(*c, *tm, a->getAudioEngine(), beat);
            return HostVerbResult::success(transportState(*c, *tm));
        });
    }

    // --- transport.setLoop / transport.clearLoop -----------------------------
    {
        auto spec = transportSpec("transport.setLoop", false,
                                  "Loop the timeline over [start, end) beats, as dragging that range on "
                                  "the ruler does (the loop preset becomes Selection).");
        spec.args.push_back(numberArg("start", true, "loop start beat >= 0"));
        spec.args.push_back(numberArg("end", true, "loop end beat, after start"));
        registerOrLog(service, std::move(spec), [c](const JSON& args) {
            auto tm = c->getTrackManager();
            auto ui = c->getTrackManagerUI();
            if (!tm || !ui) return noTimeline();
            const double start = args["start"].asNumber();
            const double end = args["end"].asNumber();
            if (!std::isfinite(start) || !std::isfinite(end) || start < 0.0 || !(end > start)) {
                return HostVerbResult::failure("invalid_args", "need 0 <= start < end");
            }
            ui->selectLoopRange(start, end);
            return HostVerbResult::success(transportState(*c, *tm));
        });
    }
    registerOrLog(service,
                  transportSpec("transport.clearLoop", false,
                                "Turn the timeline loop off and clear its range, as a click on the ruler "
                                "without a drag does."),
                  [c](const JSON&) {
                      auto tm = c->getTrackManager();
                      auto ui = c->getTrackManagerUI();
                      if (!tm || !ui) return noTimeline();
                      ui->clearLoopRange();
                      return HostVerbResult::success(transportState(*c, *tm));
                  });
}

} // namespace Aestra
