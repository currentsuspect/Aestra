// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// What the user is told when the audio interface drops out and comes back (#650, FD-64).
// Pure, so the wording and the "say it once" rule are testable without a window. Main thread only.
#pragma once

#include <string>

namespace Aestra {

struct AudioDeviceNotice {
    bool show = false;
    std::string text;
    double seconds = 0.0;
};

class AudioDeviceNoticeState {
public:
    /// The interface was lost and Aestra is running on the silent safety driver. Said once per loss.
    AudioDeviceNotice onLost() {
        if (m_substituted) return {};
        m_substituted = true;
        return {true, "Your audio interface disconnected. Aestra keeps running without sound until it's back.", 12.0};
    }

    /// Output is on a real device again. Only said if we told the user it had gone.
    AudioDeviceNotice onRestored(const std::string& deviceName) {
        if (!m_substituted) return {};
        m_substituted = false;
        const std::string who = deviceName.empty() ? "Your audio interface" : "Your audio interface (" + deviceName + ")";
        return {true, who + " is back.", 6.0};
    }

    bool substituted() const { return m_substituted; }

private:
    bool m_substituted = false;
};

} // namespace Aestra
