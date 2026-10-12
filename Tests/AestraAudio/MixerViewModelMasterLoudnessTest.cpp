// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
//
// V81-M2: the master channel of the mixer view model carries integrated, short-term and true-peak readings
// from the meter snapshot, sanitised (a NaN or an absurd value never reaches the readout).

#include "MixerViewModel.h"

#include "Core/ChannelSlotMap.h"
#include "Models/MeterSnapshot.h"
#include "Models/TrackManager.h"

#include <cmath>
#include <iostream>
#include <limits>
#include <string>

using namespace Aestra;

namespace {

int g_failures = 0;
void check(bool ok, const std::string& what) {
    std::cout << "  " << (ok ? "PASS " : "FAIL ") << what << "\n";
    if (!ok)
        ++g_failures;
}

} // namespace

int main() {
    Audio::TrackManager tm;
    tm.addChannel("A");
    tm.buildAndShareSlotMap();
    MixerViewModel vm;
    vm.syncFromEngine(tm, *tm.getChannelSlotMapShared());
    auto* master = vm.getMaster();
    if (!master) {
        std::cerr << "[FAIL] no master\n";
        return 1;
    }

    Audio::MeterSnapshotBuffer snaps;
    const int slot = static_cast<int>(master->slotIndex);

    std::cout << "[1] the readings arrive\n";
    snaps.writeLevels(slot, 0.5f, 0.5f, 0.3f, 0.3f, 0.0f, 0.0f, 0.8f, -14.2f);
    snaps.writeLoudness(slot, -12.0f, -13.1f, 0.891f, 0.708f);
    vm.updateMeters(snaps, 0.016);
    check(std::fabs(master->integratedLufs - (-14.2f)) < 1e-4f, "integrated LUFS");
    check(std::fabs(master->momentaryLufs - (-12.0f)) < 1e-4f, "momentary LUFS");
    check(std::fabs(master->shortTermLufs - (-13.1f)) < 1e-4f, "short-term LUFS");
    check(std::fabs(master->truePeakDb - 20.0f * std::log10(0.891f)) < 1e-3f, "true peak in dBTP, the larger of L and R (-1.0 dBTP)");

    std::cout << "[2] no signal reads as nothing yet\n";
    snaps.writeLoudness(slot, -144.0f, -144.0f, 0.0f, 0.0f);
    vm.updateMeters(snaps, 0.016);
    check(master->shortTermLufs <= -100.0f && master->truePeakDb <= -100.0f, "silence floor, not a number");

    std::cout << "[3] garbage never reaches the readout\n";
    const float nan = std::numeric_limits<float>::quiet_NaN();
    snaps.writeLoudness(slot, nan, std::numeric_limits<float>::infinity(), nan, -3.0f);
    vm.updateMeters(snaps, 0.016);
    check(std::isfinite(master->momentaryLufs) && std::isfinite(master->shortTermLufs) && std::isfinite(master->truePeakDb),
          "NaN and infinity are replaced");

    std::cout << (g_failures == 0 ? "ALL PASS\n" : "FAILURES\n");
    return g_failures == 0 ? 0 : 1;
}
