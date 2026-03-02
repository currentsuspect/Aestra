#pragma once
#include <vector>
#include <memory>
namespace Aestra { namespace Audio {
    struct UnitState {
        int id;
        bool enabled;
        std::shared_ptr<void> plugin;
        int routeId;
    };
    struct AudioArsenalSnapshot {
        std::vector<UnitState> units;
    };
    class UnitManager {
    public:
        std::shared_ptr<const AudioArsenalSnapshot> getAudioSnapshot() const { return nullptr; }
    };
}}
