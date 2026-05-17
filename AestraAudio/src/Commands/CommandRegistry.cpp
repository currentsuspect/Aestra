#include "Commands/CommandRegistry.h"
#include "Commands/AddChannelCommand.h"
#include "Commands/AddClipCommand.h"
#include "Commands/DuplicateClipCommand.h"
#include "Commands/MoveClipCommand.h"
#include "Commands/RemoveClipCommand.h"
#include "Commands/SetMuteCommand.h"
#include "Commands/SetPanCommand.h"
#include "Commands/SetSoloCommand.h"
#include "Commands/SetVolumeCommand.h"
#include "Commands/TrimClipCommand.h"
#include "Commands/MuseStubs.h"
#include "Models/ClipInstance.h"
#include "Models/TrackManager.h"

#include "AestraUUID.h"

#include <cctype>
#include <cmath>
#include <optional>
#include <string>

namespace Aestra {
namespace Audio {

namespace {
// Mirror of CommandParser bool spellings — keep in sync with
// CommandParser::convertAndValidateValue (FlagType::Bool).
bool parseFlagBool(const std::string& s) {
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
std::optional<int> safeStoi(std::string_view s) {
    if (s.empty()) return std::nullopt;
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

std::optional<unsigned long long> safeStoull(std::string_view s) {
    if (s.empty()) return std::nullopt;
    if (s.front() == '-') return std::nullopt;
    try {
        size_t pos = 0;
        unsigned long long val = std::stoull(std::string(s), &pos);
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
        bool state = parseFlagBool(std::string(*stateRaw));
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
        bool state = parseFlagBool(std::string(*stateRaw));
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
        auto idOpt = safeStoi(*idRaw);
        if (!idOpt) return nullptr;
        ClipInstanceID clipId;
        clipId.low = static_cast<uint64_t>(*idOpt);
        return std::make_unique<RemoveClipCommand>(*pm, clipId);
    });

    reg.registerCommand("move_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto idRaw = requireFlag(flags, "id");
        if (!idRaw) return nullptr;
        auto idOpt = safeStoi(*idRaw);
        if (!idOpt) return nullptr;
        auto trackRaw = requireFlag(flags, "track");
        if (!trackRaw) return nullptr;
        auto trackOpt = safeStoi(*trackRaw);
        if (!trackOpt) return nullptr;
        auto startRaw = requireFlag(flags, "start");
        if (!startRaw) return nullptr;
        auto startOpt = safeStof(*startRaw);
        if (!startOpt) return nullptr;

        ClipInstanceID clipId;
        clipId.low = static_cast<uint64_t>(*idOpt);
        PlaylistLaneID laneId = pm->getLaneId(static_cast<size_t>(*trackOpt));
        return std::make_unique<MoveClipCommand>(*pm, clipId, *startOpt, laneId);
    });

    reg.registerCommand("duplicate_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto idRaw = requireFlag(flags, "id");
        if (!idRaw) return nullptr;
        auto idOpt = safeStoi(*idRaw);
        if (!idOpt) return nullptr;
        auto barRaw = requireFlag(flags, "bar");
        if (!barRaw) return nullptr;
        auto barOpt = safeStoi(*barRaw);
        if (!barOpt) return nullptr;

        ClipInstanceID clipId;
        clipId.low = static_cast<uint64_t>(*idOpt);
        return std::make_unique<DuplicateClipCommand>(*pm, clipId, static_cast<double>(*barOpt));
    });

    reg.registerCommand("trim_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        auto idRaw = requireFlag(flags, "id");
        if (!idRaw) return nullptr;
        auto idOpt = safeStoi(*idRaw);
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
        clipId.low = static_cast<uint64_t>(*idOpt);
        return std::make_unique<TrimClipCommand>(*pm, clipId, *startOpt, *endOpt);
    });
}

} // namespace Audio
} // namespace Aestra
