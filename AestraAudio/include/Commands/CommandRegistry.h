#pragma once

#include "Commands/ICommand.h"
#include "Commands/MuseGrammar.h"

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

namespace Aestra {
namespace Audio {

class AudioEngine;
class TrackManager;

class CommandRegistry {
public:
    using Factory = std::function<std::unique_ptr<ICommand>(
        const std::unordered_map<std::string, std::string>&)>;

    static CommandRegistry& instance();

    void registerCommand(const std::string& verb, Factory factory);
    std::unique_ptr<ICommand> build(
        const std::string& verb,
        const std::unordered_map<std::string, std::string>& flags);

    static void initialize(TrackManager* trackManager);

    /** Set the AudioEngine for transport commands (play/stop/set_bpm). */
    static void setAudioEngine(AudioEngine* engine);
    static AudioEngine* getAudioEngine();

private:
    CommandRegistry() = default;
    std::unordered_map<std::string, Factory> m_factories;
};

} // namespace Audio
} // namespace Aestra
