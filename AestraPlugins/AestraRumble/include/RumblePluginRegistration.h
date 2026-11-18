// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include "Plugin/PluginHost.h"

namespace Aestra {
namespace Plugins {

const Aestra::Audio::PluginInfo& rumblePluginInfo();
void registerRumblePlugin();
/// Ten of Rumble's presets in the shared preset system (FD-72). Called by registerRumblePlugin().
void registerRumbleFactoryPresets();

} // namespace Plugins
} // namespace Aestra
