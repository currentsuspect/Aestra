#include "Commands/CommandRegistry.h"
#include "Commands/AddChannelCommand.h"
#include "Commands/AddClipCommand.h"
#include "Commands/AddNoteCommand.h"
#include "Commands/AddUnitCommand.h"
#include "Commands/ArrangePatternCommand.h"
#include "Commands/ClonePatternCommand.h"
#include "Commands/EffectCommands.h"
#include "Commands/SetPatternLengthCommand.h"
#include "Plugin/PluginManager.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/LoadSampleCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/MoveNoteCommand.h"
#include "Commands/QuantizePatternCommand.h"
#include "Commands/SetStepsCommand.h"
#include "Commands/TransposePatternCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/RemoveNoteCommand.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Commands/SetVolumeCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Commands/UpdateNoteCommand.h"
#include "Commands/MuseStubs.h"
#include "Models/ClipInstance.h"
#include "Models/TrackManager.h"

#include "AestraUUID.h"

#include <cctype>
#include <cmath>
#include <exception>
#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace Aestra {
namespace Audio {

namespace {
// Mirror of CommandParser bool spellings — keep in sync with
// CommandParser::convertAndValidateValue (FlagType::Bool).
bool parseFlagBool(std::string_view s) {
    std::string lower;
    lower.reserve(s.size());
    for (char c : s) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return lower == "true" || lower == "1" || lower == "yes";
}

// Safe flag lookup — returns nullopt if key is missing
std::optional<std::string_view> requireFlag(const std::unordered_map<std::string, std::string>& flags, const char* key) {
    auto it = flags.find(key);
    if (it == flags.end()) return std::nullopt;
    return it->second;
}

// Safe parsing helpers that return std::nullopt on malformed input
// Reject leading/trailing whitespace, non-finite floats, and negative unsigned strings
std::optional<int> safeStoi(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    try {
        size_t pos = 0;
        int val = std::stoi(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<float> safeStof(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    try {
        size_t pos = 0;
        float val = std::stof(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        if (!std::isfinite(val)) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<uint64_t> safeStoull(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (std::isspace(static_cast<unsigned char>(s.front())) ||
        std::isspace(static_cast<unsigned char>(s.back()))) return std::nullopt;
    if (s.front() == '-') return std::nullopt; // std::stoull wraps negatives to huge values
    try {
        size_t pos = 0;
        uint64_t val = std::stoull(std::string(s), &pos);
        if (pos != s.size()) return std::nullopt;
        return val;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

AudioEngine* s_audioEngine = nullptr;

// Reason recorded by the most recent factory refusal (see CommandRegistry::fail).
thread_local std::string t_lastBuildError;
} // namespace

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry s_instance;
    return s_instance;
}

std::unique_ptr<ICommand> CommandRegistry::fail(const std::string& reason) {
    t_lastBuildError = reason;
    return nullptr;
}

std::string CommandRegistry::consumeLastBuildError() {
    std::string reason = std::move(t_lastBuildError);
    t_lastBuildError.clear();
    return reason;
}

void CommandRegistry::registerCommand(const std::string& verb, Factory factory) {
    m_factories[verb] = std::move(factory);
}

std::unique_ptr<ICommand> CommandRegistry::build(
    const std::string& verb,
    const std::unordered_map<std::string, std::string>& flags)
{
    t_lastBuildError.clear();
    auto it = m_factories.find(verb);
    if (it == m_factories.end())
        return nullptr;
    return it->second(flags);
}

void CommandRegistry::setAudioEngine(AudioEngine* engine) {
    s_audioEngine = engine;
}

AudioEngine* CommandRegistry::getAudioEngine() {
    return s_audioEngine;
}

void CommandRegistry::initialize(TrackManager* trackManager) {
    auto& reg = instance();
    auto* pm = trackManager ? &trackManager->getPlaylistModel() : nullptr;

    // ===== Transport (3) =====
    reg.registerCommand("set_bpm", [](const auto& flags) -> std::unique_ptr<ICommand> {
        AudioEngine* engine = getAudioEngine();
        if (!engine) return nullptr;
        auto valueRaw = requireFlag(flags, "value");
        if (!valueRaw) return nullptr;
        auto valueOpt = safeStof(*valueRaw);
        if (!valueOpt) return nullptr;
        return std::make_unique<SetBpmCommand>(*engine, *valueOpt);
    });

    reg.registerCommand("play", [](const auto&) -> std::unique_ptr<ICommand> {
        AudioEngine* engine = getAudioEngine();
        if (!engine) return nullptr;
        return std::make_unique<PlayCommand>(*engine);
    });

    reg.registerCommand("stop", [](const auto&) -> std::unique_ptr<ICommand> {
        AudioEngine* engine = getAudioEngine();
        if (!engine) return nullptr;
        return std::make_unique<StopCommand>(*engine);
    });

    // ===== Track (8) =====
    if (!trackManager) {
        // Register no-op factories when no TrackManager available
        auto noopTrack = [](const auto&) -> std::unique_ptr<ICommand> { return nullptr; };
        reg.registerCommand("add_track", noopTrack);
        reg.registerCommand("delete_track", noopTrack);
        reg.registerCommand("rename_track", noopTrack);
        reg.registerCommand("mute_track", noopTrack);
        reg.registerCommand("solo_track", noopTrack);
        reg.registerCommand("set_volume", noopTrack);
        reg.registerCommand("set_pan", noopTrack);
        reg.registerCommand("add_clip", noopTrack);
        reg.registerCommand("delete_clip", noopTrack);
        reg.registerCommand("move_clip", noopTrack);
        reg.registerCommand("duplicate_clip", noopTrack);
        reg.registerCommand("trim_clip", noopTrack);
        reg.registerCommand("add_unit", noopTrack);
        reg.registerCommand("load_sample", noopTrack);
        reg.registerCommand("add_note", noopTrack);
        reg.registerCommand("delete_note", noopTrack);
        reg.registerCommand("move_note", noopTrack);
        reg.registerCommand("set_note", noopTrack);
        reg.registerCommand("arrange_pattern", noopTrack);
        reg.registerCommand("set_steps", noopTrack);
        reg.registerCommand("quantize_pattern", noopTrack);
        reg.registerCommand("transpose_pattern", noopTrack);
        reg.registerCommand("clone_pattern", noopTrack);
        reg.registerCommand("set_pattern_length", noopTrack);
        reg.registerCommand("add_effect", noopTrack);
        reg.registerCommand("remove_effect", noopTrack);
        reg.registerCommand("bypass_effect", noopTrack);
        reg.registerCommand("set_effect_param", noopTrack);
        return;
    }

    reg.registerCommand("add_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto it = flags.find("name");
        std::string name = (it != flags.end()) ? it->second : "";
        return std::make_unique<AddChannelCommand>(*tm, name);
    });

    reg.registerCommand("delete_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        if (*trackOpt < 0 || !tm->getChannel(static_cast<size_t>(*trackOpt)))
            return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        return std::make_unique<DeleteTrackCommand>(*tm, *trackOpt);
    });

    reg.registerCommand("rename_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto nameIt = flags.find("name");
        if (nameIt == flags.end()) return nullptr;
        if (*trackOpt < 0 || !tm->getChannel(static_cast<size_t>(*trackOpt)))
            return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        return std::make_unique<RenameTrackCommand>(*tm, *trackOpt, nameIt->second);
    });

    reg.registerCommand("mute_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto stateRaw = requireFlag(flags, "state");
        if (!stateRaw) return nullptr;
        bool state = parseFlagBool(*stateRaw);
        if (!trackOpt.has_value() || *trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        return std::make_unique<SetMuteCommand>(*ch, state);
    });

    reg.registerCommand("solo_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto stateRaw = requireFlag(flags, "state");
        if (!stateRaw) return nullptr;
        bool state = parseFlagBool(*stateRaw);
        if (*trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        return std::make_unique<SetSoloCommand>(*ch, state);
    });

    reg.registerCommand("set_volume", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto valueRaw = requireFlag(flags, "value");
        if (!valueRaw) return nullptr;
        auto valueOpt = safeStof(*valueRaw);
        if (!valueOpt) return nullptr;
        if (*trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        return std::make_unique<SetVolumeCommand>(*ch, *valueOpt);
    });

    reg.registerCommand("set_pan", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto valueRaw = requireFlag(flags, "value");
        if (!valueRaw) return nullptr;
        auto valueOpt = safeStof(*valueRaw);
        if (!valueOpt) return nullptr;
        if (*trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        return std::make_unique<SetPanCommand>(*ch, *valueOpt);
    });

    // ===== Clip (5) =====
    reg.registerCommand("add_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto barRaw = requireFlag(flags, "bar");
        if (!barRaw) return nullptr;
        auto barOpt = safeStoi(*barRaw);
        if (!barOpt) return nullptr;
        if (*trackOpt < 0) return nullptr;

        PlaylistLaneID laneId = pm->getLaneId(static_cast<size_t>(*trackOpt));
        if (!laneId.isValid())
            return CommandRegistry::fail("track " + std::string(*trackRaw) + " has no playlist lane");

        ClipInstance clip;
        clip.startBeat = (*barOpt - 1) * 4.0;
        clip.durationBeats = 4.0;
        auto fileIt = flags.find("file");
        if (fileIt == flags.end()) return nullptr;
        clip.name = fileIt->second;

        auto srcIt = flags.find("source");
        if (srcIt != flags.end()) {
            auto srcOpt = safeStoull(srcIt->second);
            if (!srcOpt) return nullptr;
            clip.sourceId = *srcOpt;
        }

        return std::make_unique<AddClipCommand>(*pm, laneId, clip);
    });

    reg.registerCommand("delete_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto idRaw = requireFlag(flags, "id");
        if (!idRaw) return nullptr;
        auto idOpt = safeStoull(*idRaw);
        if (!idOpt) return nullptr;

        ClipInstanceID clipId;
        clipId.low = *idOpt;
        return std::make_unique<RemoveClipCommand>(*pm, clipId);
    });

    reg.registerCommand("move_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto idRaw = requireFlag(flags, "id");
        if (!idRaw) return nullptr;
        auto idOpt = safeStoull(*idRaw);
        if (!idOpt) return nullptr;
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        if (*trackOpt < 0) return nullptr;
        auto startRaw = requireFlag(flags, "start");
        if (!startRaw) return nullptr;
        auto startOpt = safeStof(*startRaw);
        if (!startOpt) return nullptr;

        ClipInstanceID clipId;
        clipId.low = *idOpt;
        PlaylistLaneID laneId = pm->getLaneId(static_cast<size_t>(*trackOpt));
        return std::make_unique<MoveClipCommand>(*pm, clipId, *startOpt, laneId);
    });

    reg.registerCommand("duplicate_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto idRaw = requireFlag(flags, "id");
        if (!idRaw) return nullptr;
        auto idOpt = safeStoull(*idRaw);
        if (!idOpt) return nullptr;
        auto barRaw = requireFlag(flags, "bar");
        if (!barRaw) return nullptr;
        auto barOpt = safeStoi(*barRaw);
        if (!barOpt) return nullptr;

        ClipInstanceID clipId;
        clipId.low = *idOpt;
        return std::make_unique<DuplicateClipCommand>(*pm, clipId, static_cast<double>(*barOpt));
    });

    reg.registerCommand("trim_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto idRaw = requireFlag(flags, "id");
        if (!idRaw) return nullptr;
        auto idOpt = safeStoull(*idRaw);
        if (!idOpt) return nullptr;
        auto startRaw = requireFlag(flags, "start");
        if (!startRaw) return nullptr;
        auto startOpt = safeStof(*startRaw);
        if (!startOpt) return nullptr;
        auto endRaw = requireFlag(flags, "end");
        if (!endRaw) return nullptr;
        auto endOpt = safeStof(*endRaw);
        if (!endOpt) return nullptr;

        ClipInstanceID clipId;
        clipId.low = *idOpt;
        return std::make_unique<TrimClipCommand>(*pm, clipId, *startOpt, *endOpt);
    });

    // ===== Unit (2) =====
    reg.registerCommand("add_unit", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto nameIt = flags.find("name");
        std::string name = (nameIt != flags.end()) ? nameIt->second : "";
        UnitType type = UnitType::Sampler;
        auto typeIt = flags.find("type");
        if (typeIt != flags.end()) {
            if (typeIt->second == "808") {
                type = UnitType::PitchedSampler;
            } else if (typeIt->second != "sampler") {
                return CommandRegistry::fail("unknown unit type: " + typeIt->second +
                                             " (expected sampler or 808)");
            }
        }
        return std::make_unique<AddUnitCommand>(tm->getUnitManager(), name, type);
    });

    reg.registerCommand("load_sample", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto unitRaw = requireFlag(flags, "unit");
        if (!unitRaw) return nullptr;
        auto unitOpt = safeStoull(*unitRaw);
        if (!unitOpt) return nullptr;
        auto fileRaw = requireFlag(flags, "file");
        if (!fileRaw) return nullptr;

        if (!tm->getUnitManager().getUnit(*unitOpt))
            return CommandRegistry::fail("no such unit: " + std::string(*unitRaw));
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(std::string(*fileRaw)), ec))
            return CommandRegistry::fail("file not found: " + std::string(*fileRaw));
        return std::make_unique<LoadSampleCommand>(tm->getUnitManager(), *unitOpt, std::string(*fileRaw));
    });

    // ===== Pattern / note (4) =====
    // Notes are addressed by (pattern, unit, pitch, start) — the same key the
    // piano-roll note commands match on. `findNote` resolves that key against
    // stored notes with a small tolerance on start, because the caller's beat
    // value round-tripped through JSON text.
    const auto findNote = [](TrackManager& tm, PatternID patternId, uint64_t unitId, int pitch,
                             double startBeat) -> std::optional<MidiNote> {
        const PatternSource* pattern = tm.getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isMidi()) return std::nullopt;
        for (const MidiNote& note : std::get<MidiPayload>(pattern->payload).notes) {
            if (note.pitch == pitch && note.unitId == unitId &&
                std::abs(note.startBeat - startBeat) < 1e-6) {
                return note;
            }
        }
        return std::nullopt;
    };

    // Shared front half: parse pattern/unit/pitch/start, which every note verb takes.
    struct NoteKey {
        PatternID patternId;
        uint64_t unitId;
        int pitch;
        double startBeat;
    };
    const auto parseNoteKey =
        [](const std::unordered_map<std::string, std::string>& flags) -> std::optional<NoteKey> {
        auto patternRaw = requireFlag(flags, "pattern");
        if (!patternRaw) return std::nullopt;
        auto patternOpt = safeStoull(*patternRaw);
        if (!patternOpt) return std::nullopt;
        auto unitRaw = requireFlag(flags, "unit");
        if (!unitRaw) return std::nullopt;
        auto unitOpt = safeStoull(*unitRaw);
        if (!unitOpt) return std::nullopt;
        auto pitchRaw = requireFlag(flags, "pitch");
        if (!pitchRaw) return std::nullopt;
        auto pitchOpt = safeStoi(*pitchRaw);
        if (!pitchOpt) return std::nullopt;
        auto startRaw = requireFlag(flags, "start");
        if (!startRaw) return std::nullopt;
        auto startOpt = safeStof(*startRaw);
        if (!startOpt) return std::nullopt;
        return NoteKey{PatternID{*patternOpt}, *unitOpt, *pitchOpt, static_cast<double>(*startOpt)};
    };

    reg.registerCommand("add_note", [tm = trackManager, parseNoteKey](const auto& flags) -> std::unique_ptr<ICommand> {
        auto key = parseNoteKey(flags);
        if (!key) return nullptr;
        auto durationRaw = requireFlag(flags, "duration");
        if (!durationRaw) return nullptr;
        auto durationOpt = safeStof(*durationRaw);
        if (!durationOpt) return nullptr;

        float velocity = 0.8f;
        if (auto it = flags.find("velocity"); it != flags.end()) {
            auto v = safeStof(it->second);
            if (!v) return nullptr;
            velocity = *v;
        }
        float pan = 0.0f;
        if (auto it = flags.find("pan"); it != flags.end()) {
            auto p = safeStof(it->second);
            if (!p) return nullptr;
            pan = *p;
        }

        const PatternSource* pattern = tm->getPatternManager().getPattern(key->patternId);
        if (!pattern || !pattern->isMidi())
            return CommandRegistry::fail("no such MIDI pattern: " +
                                         std::to_string(key->patternId.value));
        if (!tm->getUnitManager().getUnit(key->unitId))
            return CommandRegistry::fail("no such unit: " + std::to_string(key->unitId));

        MidiNote note;
        note.pitch = key->pitch;
        note.startBeat = key->startBeat;
        note.durationBeats = static_cast<double>(*durationOpt);
        note.velocity = velocity;
        note.pan = pan;
        note.unitId = key->unitId;
        return std::make_unique<AddNoteCommand>(tm->getPatternManager(), key->patternId, note);
    });

    reg.registerCommand("delete_note", [tm = trackManager, parseNoteKey, findNote](const auto& flags) -> std::unique_ptr<ICommand> {
        auto key = parseNoteKey(flags);
        if (!key) return nullptr;
        auto note = findNote(*tm, key->patternId, key->unitId, key->pitch, key->startBeat);
        if (!note)
            return CommandRegistry::fail("no note at pattern " +
                                         std::to_string(key->patternId.value) + ", unit " +
                                         std::to_string(key->unitId) + ", pitch " +
                                         std::to_string(key->pitch) + ", start " +
                                         std::to_string(key->startBeat));
        return std::make_unique<RemoveNoteCommand>(tm->getPatternManager(), key->patternId, *note);
    });

    reg.registerCommand("move_note", [tm = trackManager, parseNoteKey, findNote](const auto& flags) -> std::unique_ptr<ICommand> {
        auto key = parseNoteKey(flags);
        if (!key) return nullptr;
        auto toStartRaw = requireFlag(flags, "to_start");
        if (!toStartRaw) return nullptr;
        auto toStartOpt = safeStof(*toStartRaw);
        if (!toStartOpt) return nullptr;

        auto note = findNote(*tm, key->patternId, key->unitId, key->pitch, key->startBeat);
        if (!note)
            return CommandRegistry::fail("no note at pattern " +
                                         std::to_string(key->patternId.value) + ", unit " +
                                         std::to_string(key->unitId) + ", pitch " +
                                         std::to_string(key->pitch) + ", start " +
                                         std::to_string(key->startBeat));

        int toPitch = note->pitch;
        if (auto it = flags.find("to_pitch"); it != flags.end()) {
            auto p = safeStoi(it->second);
            if (!p) return nullptr;
            toPitch = *p;
        }
        return std::make_unique<MoveNoteCommand>(tm->getPatternManager(), key->patternId, *note,
                                                 static_cast<double>(*toStartOpt), toPitch);
    });

    reg.registerCommand("arrange_pattern", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto patternRaw = requireFlag(flags, "pattern");
        if (!patternRaw) return nullptr;
        auto patternOpt = safeStoull(*patternRaw);
        if (!patternOpt) return nullptr;
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt || *trackOpt < 0) return nullptr;
        auto startRaw = requireFlag(flags, "start");
        if (!startRaw) return nullptr;
        auto startOpt = safeStof(*startRaw);
        if (!startOpt) return nullptr;

        const PatternID patternId{*patternOpt};
        const PatternSource* pattern = tm->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isMidi())
            return CommandRegistry::fail("no such MIDI pattern: " + std::string(*patternRaw));
        // Tracks are mixer channels; the command creates matching playlist
        // lanes, but the channel itself must already exist.
        if (static_cast<size_t>(*trackOpt) >= tm->getChannelCount())
            return CommandRegistry::fail("no such track: " + std::string(*trackRaw));

        // A unit has a single timeline route: arranging it onto a different
        // track would silently reroute every earlier clip that uses it.
        // Reject the conflict instead of corrupting existing arrangements.
        for (const MidiNote& note : std::get<MidiPayload>(pattern->payload).notes) {
            if (note.unitId == 0) continue;
            const int route = tm->getUnitManager().getUnitTimelineLane(note.unitId);
            if (route >= 0 && route != *trackOpt)
                return CommandRegistry::fail(
                    "unit " + std::to_string(note.unitId) + " is already routed to track " +
                    std::to_string(route) + "; arrange on that track or undo the earlier arrange");
        }

        return std::make_unique<ArrangePatternCommand>(*tm, patternId,
                                                       static_cast<size_t>(*trackOpt),
                                                       static_cast<double>(*startOpt));
    });

    reg.registerCommand("set_steps", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto patternRaw = requireFlag(flags, "pattern");
        if (!patternRaw) return nullptr;
        auto patternOpt = safeStoull(*patternRaw);
        if (!patternOpt) return nullptr;
        auto unitRaw = requireFlag(flags, "unit");
        if (!unitRaw) return nullptr;
        auto unitOpt = safeStoull(*unitRaw);
        if (!unitOpt) return nullptr;
        auto pitchRaw = requireFlag(flags, "pitch");
        if (!pitchRaw) return nullptr;
        auto pitchOpt = safeStoi(*pitchRaw);
        if (!pitchOpt) return nullptr;
        auto stepsRaw = requireFlag(flags, "steps");
        if (!stepsRaw) return nullptr;

        double stepBeats = 0.25; // 16th notes
        if (auto it = flags.find("step"); it != flags.end()) {
            auto v = safeStof(it->second);
            if (!v) return nullptr;
            stepBeats = static_cast<double>(*v);
        }
        float velocity = 0.8f;
        if (auto it = flags.find("velocity"); it != flags.end()) {
            auto v = safeStof(it->second);
            if (!v) return nullptr;
            velocity = *v;
        }
        float gate = 0.9f;
        if (auto it = flags.find("gate"); it != flags.end()) {
            auto v = safeStof(it->second);
            if (!v) return nullptr;
            gate = *v;
        }
        double swing = 0.0;
        if (auto it = flags.find("swing"); it != flags.end()) {
            auto v = safeStof(it->second);
            if (!v) return nullptr;
            swing = static_cast<double>(*v);
        }

        const PatternID patternId{*patternOpt};
        const PatternSource* pattern = tm->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isMidi())
            return CommandRegistry::fail("no such MIDI pattern: " + std::string(*patternRaw));
        if (!tm->getUnitManager().getUnit(*unitOpt))
            return CommandRegistry::fail("no such unit: " + std::string(*unitRaw));

        // steps: 'x' hit, 'X' accent, digits '1'-'9' hit at velocity n/9,
        // '-' '.' or ' ' rest. Anything else is a typo the agent should hear
        // about, not a silent rest.
        const std::string steps(*stepsRaw);
        if (steps.empty() || steps.size() > 256)
            return CommandRegistry::fail("steps must be 1..256 characters");
        std::vector<MidiNote> rowNotes;
        for (size_t i = 0; i < steps.size(); ++i) {
            const char c = steps[i];
            if (c == '-' || c == '.' || c == ' ') continue;
            float noteVelocity;
            if (c == 'x') {
                noteVelocity = velocity;
            } else if (c == 'X') {
                noteVelocity = std::min(1.0f, velocity + 0.2f);
            } else if (c >= '1' && c <= '9') {
                noteVelocity = static_cast<float>(c - '0') / 9.0f;
            } else {
                return CommandRegistry::fail(std::string("invalid step character '") + c +
                                             "' (use x, X, 1-9, -, ., or space)");
            }
            MidiNote note;
            note.pitch = *pitchOpt;
            note.startBeat = static_cast<double>(i) * stepBeats;
            // Swing: every second step lands late by swing * step/2 — the
            // classic shuffle placement.
            if (i % 2 == 1) {
                note.startBeat += swing * stepBeats * 0.5;
            }
            note.durationBeats = stepBeats * static_cast<double>(gate);
            note.velocity = noteVelocity;
            note.unitId = *unitOpt;
            rowNotes.push_back(note);
        }

        const double rowLengthBeats = static_cast<double>(steps.size()) * stepBeats;
        return std::make_unique<SetStepsCommand>(tm->getPatternManager(), patternId,
                                                 std::move(rowNotes), *unitOpt, *pitchOpt,
                                                 rowLengthBeats);
    });

    reg.registerCommand("quantize_pattern", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto patternRaw = requireFlag(flags, "pattern");
        if (!patternRaw) return nullptr;
        auto patternOpt = safeStoull(*patternRaw);
        if (!patternOpt) return nullptr;
        auto gridRaw = requireFlag(flags, "grid");
        if (!gridRaw) return nullptr;
        auto gridOpt = safeStof(*gridRaw);
        if (!gridOpt) return nullptr;

        double strength = 1.0;
        if (auto it = flags.find("strength"); it != flags.end()) {
            auto v = safeStof(it->second);
            if (!v) return nullptr;
            strength = static_cast<double>(*v);
        }
        uint64_t unitId = 0; // all units
        if (auto it = flags.find("unit"); it != flags.end()) {
            auto v = safeStoull(it->second);
            if (!v) return nullptr;
            unitId = *v;
        }

        const PatternID patternId{*patternOpt};
        const PatternSource* pattern = tm->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isMidi())
            return CommandRegistry::fail("no such MIDI pattern: " + std::string(*patternRaw));

        return std::make_unique<QuantizePatternCommand>(tm->getPatternManager(), patternId,
                                                        static_cast<double>(*gridOpt), strength,
                                                        unitId);
    });

    reg.registerCommand("transpose_pattern", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto patternRaw = requireFlag(flags, "pattern");
        if (!patternRaw) return nullptr;
        auto patternOpt = safeStoull(*patternRaw);
        if (!patternOpt) return nullptr;
        auto semitonesRaw = requireFlag(flags, "semitones");
        if (!semitonesRaw) return nullptr;
        auto semitonesOpt = safeStoi(*semitonesRaw);
        if (!semitonesOpt) return nullptr;

        uint64_t unitId = 0; // all units
        if (auto it = flags.find("unit"); it != flags.end()) {
            auto v = safeStoull(it->second);
            if (!v) return nullptr;
            unitId = *v;
        }

        const PatternID patternId{*patternOpt};
        const PatternSource* pattern = tm->getPatternManager().getPattern(patternId);
        if (!pattern || !pattern->isMidi())
            return CommandRegistry::fail("no such MIDI pattern: " + std::string(*patternRaw));

        // Reject rather than clamp: a clamped transpose is not invertible,
        // which would corrupt undo.
        for (const MidiNote& note : std::get<MidiPayload>(pattern->payload).notes) {
            if (unitId != 0 && note.unitId != unitId) continue;
            const int shifted = note.pitch + *semitonesOpt;
            if (shifted < 0 || shifted > 127)
                return CommandRegistry::fail(
                    "transpose would move pitch " + std::to_string(note.pitch) + " to " +
                    std::to_string(shifted) + " (outside MIDI range 0..127)");
        }

        return std::make_unique<TransposePatternCommand>(tm->getPatternManager(), patternId,
                                                         *semitonesOpt, unitId);
    });

    // Shared by the effect factories: case-insensitive string equality.
    const auto equalsIgnoreCase = [](std::string_view a, std::string_view b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i])))
                return false;
        }
        return true;
    };

    reg.registerCommand("add_effect", [tm = trackManager, equalsIgnoreCase](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt || *trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        auto effectRaw = requireFlag(flags, "effect");
        if (!effectRaw) return nullptr;

        auto& pluginManager = PluginManager::getInstance();
        const PluginInfo* match = nullptr;
        const auto effects = pluginManager.getEffectPlugins();
        for (const auto& info : effects) {
            if (info.id == *effectRaw || equalsIgnoreCase(info.name, *effectRaw)) {
                match = &info;
                break;
            }
        }
        if (!match)
            return CommandRegistry::fail("unknown effect: " + std::string(*effectRaw) +
                                         " (list_plugins shows what is available)");

        auto& chain = ch->getEffectChain();
        size_t slot = chain.getFirstEmptySlot();
        if (auto it = flags.find("slot"); it != flags.end()) {
            auto s = safeStoi(it->second);
            if (!s) return nullptr;
            slot = static_cast<size_t>(*s);
            if (chain.getPlugin(slot))
                return CommandRegistry::fail("slot " + it->second + " on track " +
                                             std::string(*trackRaw) + " is occupied");
        }
        if (slot >= EffectChain::MAX_SLOTS)
            return CommandRegistry::fail("no empty effect slots on track " +
                                         std::string(*trackRaw));

        // Same lifecycle the app runs before inserting an effect.
        auto instance = pluginManager.createInstanceById(match->id);
        if (!instance)
            return CommandRegistry::fail("failed to create effect: " + match->id);
        if (!instance->initialize(pluginManager.getDefaultSampleRate(),
                                  pluginManager.getDefaultBlockSize()))
            return CommandRegistry::fail("failed to initialize effect: " + match->id);
        instance->activate();
        chain.prepare(pluginManager.getDefaultSampleRate(), pluginManager.getDefaultBlockSize());

        return std::make_unique<AddEffectCommand>(*tm, *ch, slot, std::move(instance),
                                                  match->name);
    });

    reg.registerCommand("remove_effect", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt || *trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        auto slotRaw = requireFlag(flags, "slot");
        if (!slotRaw) return nullptr;
        auto slotOpt = safeStoi(*slotRaw);
        if (!slotOpt) return nullptr;
        if (!ch->getEffectChain().getPlugin(static_cast<size_t>(*slotOpt)))
            return CommandRegistry::fail("no effect in slot " + std::string(*slotRaw) +
                                         " of track " + std::string(*trackRaw));
        return std::make_unique<RemoveEffectCommand>(*tm, *ch, static_cast<size_t>(*slotOpt));
    });

    reg.registerCommand("bypass_effect", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt || *trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        auto slotRaw = requireFlag(flags, "slot");
        if (!slotRaw) return nullptr;
        auto slotOpt = safeStoi(*slotRaw);
        if (!slotOpt) return nullptr;
        auto stateRaw = requireFlag(flags, "state");
        if (!stateRaw) return nullptr;
        if (!ch->getEffectChain().getPlugin(static_cast<size_t>(*slotOpt)))
            return CommandRegistry::fail("no effect in slot " + std::string(*slotRaw) +
                                         " of track " + std::string(*trackRaw));
        return std::make_unique<SetEffectBypassCommand>(*tm, *ch, static_cast<size_t>(*slotOpt),
                                                        parseFlagBool(*stateRaw));
    });

    reg.registerCommand("set_effect_param", [tm = trackManager, equalsIgnoreCase](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt || *trackOpt < 0) return nullptr;
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(*trackOpt));
        if (!ch) return CommandRegistry::fail("no such track: " + std::string(*trackRaw));
        auto slotRaw = requireFlag(flags, "slot");
        if (!slotRaw) return nullptr;
        auto slotOpt = safeStoi(*slotRaw);
        if (!slotOpt) return nullptr;
        auto plugin = ch->getEffectChain().getPlugin(static_cast<size_t>(*slotOpt));
        if (!plugin)
            return CommandRegistry::fail("no effect in slot " + std::string(*slotRaw) +
                                         " of track " + std::string(*trackRaw));
        auto paramRaw = requireFlag(flags, "param");
        if (!paramRaw) return nullptr;
        auto valueRaw = requireFlag(flags, "value");
        if (!valueRaw) return nullptr;
        auto valueOpt = safeStof(*valueRaw);
        if (!valueOpt) return nullptr;

        // param is a name (case-insensitive) or a numeric id from get_effects.
        const auto params = plugin->getParameters();
        const PluginParameter* target = nullptr;
        if (auto numeric = safeStoull(*paramRaw);
            numeric && *numeric <= std::numeric_limits<uint32_t>::max()) {
            for (const auto& param : params) {
                if (param.id == static_cast<uint32_t>(*numeric)) {
                    target = &param;
                    break;
                }
            }
        }
        if (!target) {
            for (const auto& param : params) {
                if (equalsIgnoreCase(param.name, *paramRaw) ||
                    equalsIgnoreCase(param.shortName, *paramRaw)) {
                    target = &param;
                    break;
                }
            }
        }
        if (!target)
            return CommandRegistry::fail("no parameter '" + std::string(*paramRaw) + "' on " +
                                         plugin->getInfo().name +
                                         " (get_effects lists parameters)");
        if (target->isReadOnly)
            return CommandRegistry::fail("parameter '" + target->name + "' is read-only");

        return std::make_unique<SetEffectParamCommand>(plugin, target->id, *valueOpt);
    });

    reg.registerCommand("clone_pattern", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto patternRaw = requireFlag(flags, "pattern");
        if (!patternRaw) return nullptr;
        auto patternOpt = safeStoull(*patternRaw);
        if (!patternOpt) return nullptr;
        const PatternID patternId{*patternOpt};
        if (!tm->getPatternManager().getPattern(patternId))
            return CommandRegistry::fail("no such pattern: " + std::string(*patternRaw));
        return std::make_unique<ClonePatternCommand>(tm->getPatternManager(), patternId);
    });

    reg.registerCommand("set_pattern_length", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto patternRaw = requireFlag(flags, "pattern");
        if (!patternRaw) return nullptr;
        auto patternOpt = safeStoull(*patternRaw);
        if (!patternOpt) return nullptr;
        auto beatsRaw = requireFlag(flags, "beats");
        if (!beatsRaw) return nullptr;
        auto beatsOpt = safeStof(*beatsRaw);
        if (!beatsOpt) return nullptr;
        const PatternID patternId{*patternOpt};
        if (!tm->getPatternManager().getPattern(patternId))
            return CommandRegistry::fail("no such pattern: " + std::string(*patternRaw));
        return std::make_unique<SetPatternLengthCommand>(tm->getPatternManager(), patternId,
                                                         static_cast<double>(*beatsOpt));
    });

    reg.registerCommand("set_note", [tm = trackManager, parseNoteKey, findNote](const auto& flags) -> std::unique_ptr<ICommand> {
        auto key = parseNoteKey(flags);
        if (!key) return nullptr;
        auto note = findNote(*tm, key->patternId, key->unitId, key->pitch, key->startBeat);
        if (!note)
            return CommandRegistry::fail("no note at pattern " +
                                         std::to_string(key->patternId.value) + ", unit " +
                                         std::to_string(key->unitId) + ", pitch " +
                                         std::to_string(key->pitch) + ", start " +
                                         std::to_string(key->startBeat));

        float velocity = note->velocity;
        if (auto it = flags.find("velocity"); it != flags.end()) {
            auto v = safeStof(it->second);
            if (!v) return nullptr;
            velocity = *v;
        }
        float pan = note->pan;
        if (auto it = flags.find("pan"); it != flags.end()) {
            auto p = safeStof(it->second);
            if (!p) return nullptr;
            pan = *p;
        }
        return std::make_unique<UpdateNoteCommand>(tm->getPatternManager(), key->patternId, *note,
                                                   velocity, pan);
    });
}

} // namespace Audio
} // namespace Aestra
