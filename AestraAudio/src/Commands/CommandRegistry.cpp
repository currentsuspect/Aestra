#include "Commands/CommandRegistry.h"
#include "Commands/AddChannelCommand.h"
#include "Commands/AddClipCommand.h"
#include "Commands/AddNoteCommand.h"
#include "Commands/AddUnitCommand.h"
#include "Commands/ArrangePatternCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/LoadSampleCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/MoveNoteCommand.h"
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
} // namespace

CommandRegistry& CommandRegistry::instance() {
    static CommandRegistry s_instance;
    return s_instance;
}

void CommandRegistry::registerCommand(const std::string& verb, Factory factory) {
    m_factories[verb] = std::move(factory);
}

std::unique_ptr<ICommand> CommandRegistry::build(
    const std::string& verb,
    const std::unordered_map<std::string, std::string>& flags)
{
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
        return std::make_unique<DeleteTrackCommand>(*tm, *trackOpt);
    });

    reg.registerCommand("rename_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto nameIt = flags.find("name");
        if (nameIt == flags.end()) return nullptr;
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
        if (!ch) return nullptr;
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
        if (!ch) return nullptr;
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
        if (!ch) return nullptr;
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
        if (!ch) return nullptr;
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
            return nullptr;

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
                return nullptr; // schema comment advertises the accepted values
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

        if (!tm->getUnitManager().getUnit(*unitOpt)) return nullptr;
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::path(std::string(*fileRaw)), ec)) return nullptr;
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
        if (!pattern || !pattern->isMidi()) return nullptr;
        if (!tm->getUnitManager().getUnit(key->unitId)) return nullptr;

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
        if (!note) return nullptr;
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
        if (!note) return nullptr;

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
        if (!pattern || !pattern->isMidi()) return nullptr;
        // Tracks are mixer channels; the command creates matching playlist
        // lanes, but the channel itself must already exist.
        if (static_cast<size_t>(*trackOpt) >= tm->getChannelCount()) return nullptr;

        // A unit has a single timeline route: arranging it onto a different
        // track would silently reroute every earlier clip that uses it.
        // Reject the conflict instead of corrupting existing arrangements.
        for (const MidiNote& note : std::get<MidiPayload>(pattern->payload).notes) {
            if (note.unitId == 0) continue;
            const int route = tm->getUnitManager().getUnitTimelineLane(note.unitId);
            if (route >= 0 && route != *trackOpt) return nullptr;
        }

        return std::make_unique<ArrangePatternCommand>(*tm, patternId,
                                                       static_cast<size_t>(*trackOpt),
                                                       static_cast<double>(*startOpt));
    });

    reg.registerCommand("set_note", [tm = trackManager, parseNoteKey, findNote](const auto& flags) -> std::unique_ptr<ICommand> {
        auto key = parseNoteKey(flags);
        if (!key) return nullptr;
        auto note = findNote(*tm, key->patternId, key->unitId, key->pitch, key->startBeat);
        if (!note) return nullptr;

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
