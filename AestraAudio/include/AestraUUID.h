#pragma once
#include <cstdint>

namespace Aestra {
namespace Audio {

struct AestraUUID {
    uint64_t low = 0;
    uint64_t high = 0;

    AestraUUID() = default;
    AestraUUID(uint64_t v) : low(v) {}

    static AestraUUID generate() {
        return AestraUUID(1);
    }

    bool operator==(const AestraUUID& other) const {
        return low == other.low && high == other.high;
    }
};

}
}

using AestraUUID = Aestra::Audio::AestraUUID;
