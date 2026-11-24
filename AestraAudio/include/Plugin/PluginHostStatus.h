// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Whether the out-of-process plugin helper (AestraPluginHost) can be used, and how a failed load is described.
//
// VST3 and CLAP plugins only load through that helper. A build without it used to fail with "Helper failed to load
// plugin: <name>" and nothing else, while the Arsenal drop reported success. These are pure functions so the
// wording and the availability check can be tested without a helper or a plugin.

#include <filesystem>
#include <string>

namespace Aestra {
namespace Audio {

enum class PluginHostStatus { Ok, Missing, NotExecutable };

struct PluginHostCheck {
    PluginHostStatus status = PluginHostStatus::Missing;
    std::string path;
    std::string message; ///< one human line, ready for a log or a tooltip
};

/// Where the helper is looked for: AESTRA_PLUGIN_HOST_PATH, else next to the running executable.
std::string defaultExternalPluginHostPath();

/// Checks the helper file at `path` (empty means defaultExternalPluginHostPath()).
PluginHostCheck externalPluginHostStatus(const std::filesystem::path& path = {});

enum class PluginHostLoadFailure {
    HelperMissing, ///< no helper file at the resolved path
    StartFailed,   ///< the file exists but could not be started
    HelperExited,  ///< the helper started, then went away before replying
    Timeout,       ///< the helper started but did not reply in time
    HelperReplied  ///< the helper answered "ERR <reason>"
};

struct PluginHostLoadError {
    std::string reason; ///< short, for the person: "plugin helper missing"
    std::string log;    ///< everything: plugin, helper path, whether it exists, the helper's own words
};

PluginHostLoadError describePluginHostLoadFailure(PluginHostLoadFailure kind, const std::string& pluginName,
                                                  const std::string& helperPath, bool helperExists,
                                                  const std::string& reply = {});

/// The most recent external-plugin load failure, so a caller that only got a null instance can say why.
/// Empty after a successful load.
void recordPluginHostLoadFailure(const std::string& reason);
std::string lastPluginHostLoadFailure();

} // namespace Audio
} // namespace Aestra
