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
        float value = std::stof(flags.at("value"));
        return std::make_unique<SetBpmCommand>(*engine, value);
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
        int trackIndex = std::stoi(flags.at("track"));
        return std::make_unique<DeleteTrackCommand>(*tm, trackIndex);
    });

    reg.registerCommand("rename_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        int trackIndex = std::stoi(flags.at("track"));
        return std::make_unique<RenameTrackCommand>(*tm, trackIndex, flags.at("name"));
    });

    reg.registerCommand("mute_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        int trackIndex = std::stoi(flags.at("track"));
        bool state = parseFlagBool(flags.at("state"));
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(trackIndex));
        if (!ch) return nullptr;
        return std::make_unique<SetMuteCommand>(*ch, state);
    });

    reg.registerCommand("solo_track", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        int trackIndex = std::stoi(flags.at("track"));
        bool state = parseFlagBool(flags.at("state"));
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(trackIndex));
        if (!ch) return nullptr;
        return std::make_unique<SetSoloCommand>(*ch, state);
    });

    reg.registerCommand("set_volume", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        int trackIndex = std::stoi(flags.at("track"));
        float value = std::stof(flags.at("value"));
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(trackIndex));
        if (!ch) return nullptr;
        return std::make_unique<SetVolumeCommand>(*ch, value);
    });

    reg.registerCommand("set_pan", [tm = trackManager](const auto& flags) -> std::unique_ptr<ICommand> {
        int trackIndex = std::stoi(flags.at("track"));
        float value = std::stof(flags.at("value"));
        MixerChannel* ch = tm->getChannel(static_cast<size_t>(trackIndex));
        if (!ch) return nullptr;
        return std::make_unique<SetPanCommand>(*ch, value);
    });

    // ===== Clip (5) =====
    reg.registerCommand("add_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        int trackIndex = std::stoi(flags.at("track"));
        double bar = static_cast<double>(std::stoi(flags.at("bar")));

        PlaylistLaneID laneId = pm->getLaneId(static_cast<size_t>(trackIndex));
        if (!laneId.isValid())
            return nullptr;

        ClipInstance clip;
        clip.startBeat = (bar - 1) * 4.0;
        clip.durationBeats = 4.0;
        clip.name = flags.at("file");

        auto it = flags.find("source");
        if (it != flags.end()) {
            clip.sourceId = std::stoull(it->second);
        }

        return std::make_unique<AddClipCommand>(*pm, laneId, clip);
    });

    reg.registerCommand("delete_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        int id = std::stoi(flags.at("id"));
        ClipInstanceID clipId;
        clipId.low = static_cast<uint64_t>(id);
        return std::make_unique<RemoveClipCommand>(*pm, clipId);
    });

    reg.registerCommand("move_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        int id = std::stoi(flags.at("id"));
        int trackIndex = std::stoi(flags.at("track"));
        double start = static_cast<double>(std::stof(flags.at("start")));

        ClipInstanceID clipId;
        clipId.low = static_cast<uint64_t>(id);
        PlaylistLaneID laneId = pm->getLaneId(static_cast<size_t>(trackIndex));
        return std::make_unique<MoveClipCommand>(*pm, clipId, start, laneId);
    });

    reg.registerCommand("duplicate_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        int id = std::stoi(flags.at("id"));
        int bar = std::stoi(flags.at("bar"));

        ClipInstanceID clipId;
        clipId.low = static_cast<uint64_t>(id);
        return std::make_unique<DuplicateClipCommand>(*pm, clipId, static_cast<double>(bar));
    });

    reg.registerCommand("trim_clip", [pm](const auto& flags) -> std::unique_ptr<ICommand> {
        if (!pm) return nullptr;
        int id = std::stoi(flags.at("id"));
        double start = static_cast<double>(std::stof(flags.at("start")));
        double end = static_cast<double>(std::stof(flags.at("end")));

        ClipInstanceID clipId;
        clipId.low = static_cast<uint64_t>(id);
        return std::make_unique<TrimClipCommand>(*pm, clipId, start, end);
    });
}

} // namespace Audio
} // namespace Aestra
