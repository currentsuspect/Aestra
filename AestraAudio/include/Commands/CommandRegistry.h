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

    /**
     * @brief Record why a factory refused to build, then return nullptr.
     *
     * Factories call `return CommandRegistry::fail("no such pattern: 7");`
     * instead of a bare `return nullptr;` so callers can surface the reason
     * to the agent instead of a generic "failed to build command".
     */
    static std::unique_ptr<ICommand> fail(const std::string& reason);

    /**
     * @brief Take the reason recorded by the most recent failed build.
     *
     * build() clears it before invoking the factory; empty when the factory
     * gave no reason. Thread-local, like command execution itself.
     */
    static std::string consumeLastBuildError();

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
