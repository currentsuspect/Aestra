// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {
void require(bool cond, const char* msg) {
    if (!cond) {
        std::cerr << "[FAIL] " << msg << "\n";
        std::exit(1);
    }
}
} // namespace

int main() {
    // This is a policy contract test only.
    // We intentionally avoid introducing a production bridge enum in Phase 2B.
    constexpr std::array<std::string_view, 6> kBridgeModes = {
        "DraftOnly",
        "PreviewToMaster",
        "LinkedRack",
        "LocalCopy",
        "RenderedAudio",
        "FrozenAudio",
    };

    require(kBridgeModes.size() == 6, "Bridge contract must define exactly six bridge states");
    require(kBridgeModes[0] == "DraftOnly", "Bridge mode[0] must remain DraftOnly");
    require(kBridgeModes[1] == "PreviewToMaster", "Bridge mode[1] must remain PreviewToMaster");
    require(kBridgeModes[2] == "LinkedRack", "Bridge mode[2] must remain LinkedRack");
    require(kBridgeModes[3] == "LocalCopy", "Bridge mode[3] must remain LocalCopy");
    require(kBridgeModes[4] == "RenderedAudio", "Bridge mode[4] must remain RenderedAudio");
    require(kBridgeModes[5] == "FrozenAudio", "Bridge mode[5] must remain FrozenAudio");

    std::cout << "[PASS] ArsenalBridgeContractTest\n";
    return 0;
}
