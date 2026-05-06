// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

namespace Aestra {
namespace Audio {

enum class GraphDirtyReason {
    Unknown,
    TimelineChanged,
    EffectChainChanged,
    RoutingChanged,
    TrackStructureChanged,
    TrackProcessingChanged,
    MixerStateChanged,
    ProjectLoaded,
    DeviceConfigChanged
};

} // namespace Audio
} // namespace Aestra