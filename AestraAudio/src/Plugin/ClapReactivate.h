// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Takes an active CLAP plugin through stop, deactivate, activate, start again at the same rate and block size.
//
// Loading state into a plugin that is already processing can leave its engine out of step with what the state says
// (an oversampling or latency setting the engine sized its buffers for). The Riverflux plugins then crash inside their
// own DSP on the next block, which kills the sandbox helper and, with it, the editor window. Preparing the plugin
// again after the load makes the same sequence survive. No plugin module here, so a test can drive it with a fake.

#include "ClapAbiMirror.h"

#include <cstdint>
#include <functional>

namespace AestraHelper {
namespace ClapAbi {

/// @p processingScope(true/false) brackets the calls CLAP requires on the processing thread (stop and start), so the
/// caller can mark that thread. May be empty. Returns false when the plugin could not be brought back.
inline bool reactivateClapPlugin(const ClapPlugin* plugin, double sampleRate, uint32_t blockSize,
                                 const std::function<void(bool)>& processingScope) {
    if (!plugin || !plugin->activate || !plugin->deactivate || !plugin->startProcessing) {
        return false;
    }
    const auto scoped = [&](auto&& call) {
        if (processingScope) processingScope(true);
        const auto result = call();
        if (processingScope) processingScope(false);
        return result;
    };
    if (plugin->stopProcessing) {
        scoped([&] { plugin->stopProcessing(plugin); return true; });
    }
    plugin->deactivate(plugin);
    if (!plugin->activate(plugin, sampleRate, 1, blockSize)) {
        return false;
    }
    return scoped([&] { return plugin->startProcessing(plugin); });
}

} // namespace ClapAbi
} // namespace AestraHelper
