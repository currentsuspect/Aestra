// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <vector>

namespace Aestra {
namespace Audio {

namespace BuiltInPlugins {
const PluginInfo& samplerInfo();
const PluginInfo& eqInfo();
const PluginInfo& compInfo();
const PluginInfo& verbInfo();
const PluginInfo& delayInfo();
const PluginInfo& driftInfo();
const PluginInfo& limiterInfo();
const PluginInfo& satInfo();
const PluginInfo& filterInfo();
const PluginInfo& ottInfo();
void registerCoreBuiltIns();
std::vector<PluginInfo> all();
}

} // namespace Audio
} // namespace Aestra
