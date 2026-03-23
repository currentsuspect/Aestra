// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "PluginHost.h"

#include <vector>

namespace Aestra {
namespace Audio {

namespace BuiltInPlugins {
const PluginInfo& rumbleInfo();
const PluginInfo& samplerInfo();
std::vector<PluginInfo> all();
}

} // namespace Audio
} // namespace Aestra
