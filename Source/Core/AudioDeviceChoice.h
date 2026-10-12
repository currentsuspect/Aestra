// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Which device a saved selection means.
//
// Device ids are positions in the driver's current list, so they change whenever a device appears or
// goes (a USB mic plugged in, Easy Effects adding virtual devices). A selection saved as an id then names a
// different device on the next start: a saved "129" was the laptop's own mic once the AmpliTank had shifted the
// list. The file therefore also stores the device's NAME, and the name wins while that device is present; the
// id is only the fallback for a file written before names were saved, or a device that has been renamed away.

#include <string>
#include <vector>

namespace Aestra {

struct DeviceChoiceCandidate {
    int id{0};
    std::string name;
    int inputChannels{0};
    int outputChannels{0};
};

/// The id to use for a saved selection. @p wantInput picks which side must have channels.
inline int resolveSavedDeviceId(const std::vector<DeviceChoiceCandidate>& devices, const std::string& savedName,
                                int savedId, bool wantInput) {
    if (!savedName.empty()) {
        for (const auto& d : devices) {
            if (d.name == savedName && (wantInput ? d.inputChannels : d.outputChannels) > 0) return d.id;
        }
    }
    return savedId;
}

/// The name of device @p id, or empty when it is not in the list.
inline std::string deviceNameForId(const std::vector<DeviceChoiceCandidate>& devices, int id) {
    for (const auto& d : devices)
        if (d.id == id) return d.name;
    return {};
}

} // namespace Aestra
