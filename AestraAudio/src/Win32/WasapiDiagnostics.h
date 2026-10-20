// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// When Windows reports no usable audio output, "No audio devices found" is all the log said, which cannot tell a
// machine with no audio driver from a stopped Windows Audio service from a bug here. This writes down what Windows
// itself says: the state of the Windows Audio service, and how many playback / recording endpoints exist in each state
// (active, disabled, unplugged, not present). Called once, when enumeration comes back empty.

#ifdef _WIN32

#include "AestraLog.h"

#include <audioclient.h>
#include <mmdeviceapi.h>
#include <windows.h>

#include <cstdio>
#include <string>

namespace Aestra {
namespace Audio {

inline std::string wasapiHex(HRESULT hr) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

inline const char* audioServiceStateText() {
    SC_HANDLE manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
    if (!manager) return "cannot ask (service manager refused)";
    SC_HANDLE service = OpenServiceW(manager, L"Audiosrv", SERVICE_QUERY_STATUS);
    const char* text = "not installed";
    if (service) {
        SERVICE_STATUS status{};
        if (QueryServiceStatus(service, &status)) {
            switch (status.dwCurrentState) {
            case SERVICE_RUNNING: text = "running"; break;
            case SERVICE_STOPPED: text = "STOPPED"; break;
            case SERVICE_START_PENDING: text = "starting"; break;
            case SERVICE_STOP_PENDING: text = "stopping"; break;
            default: text = "in another state"; break;
            }
        } else {
            text = "cannot ask";
        }
        CloseServiceHandle(service);
    }
    CloseServiceHandle(manager);
    return text;
}

inline void logWasapiEndpointReport(IMMDeviceEnumerator* enumerator, const char* who) {
    const std::string tag = std::string("[") + who + "] ";
    Log::warning(tag + "Windows reports no active audio output. What Windows says:");
    Log::warning(tag + "  Windows Audio service (Audiosrv): " + audioServiceStateText());
    if (!enumerator) {
        Log::warning(tag + "  no device enumerator");
        return;
    }
    struct Flow {
        EDataFlow flow;
        const char* name;
    };
    struct State {
        DWORD mask;
        const char* name;
    };
    for (const Flow flow : {Flow{eRender, "playback"}, Flow{eCapture, "recording"}}) {
        std::string line = std::string("  ") + flow.name + " endpoints:";
        for (const State state : {State{DEVICE_STATE_ACTIVE, "active"}, State{DEVICE_STATE_DISABLED, "disabled"},
                                  State{DEVICE_STATE_UNPLUGGED, "unplugged"}, State{DEVICE_STATE_NOTPRESENT, "not present"}}) {
            IMMDeviceCollection* collection = nullptr;
            const HRESULT hr = enumerator->EnumAudioEndpoints(flow.flow, state.mask, &collection);
            UINT count = 0;
            if (SUCCEEDED(hr) && collection) {
                collection->GetCount(&count);
                collection->Release();
                line += std::string(" ") + state.name + "=" + std::to_string(count);
            } else {
                line += std::string(" ") + state.name + "=error " + wasapiHex(hr);
            }
        }
        Log::warning(tag + line);
    }
    IMMDevice* defaultDevice = nullptr;
    const HRESULT hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &defaultDevice);
    Log::warning(tag + "  default playback device: " + (SUCCEEDED(hr) ? "present" : "none (" + wasapiHex(hr) + ")"));
    if (defaultDevice) defaultDevice->Release();
    Log::warning(tag + "  If playback shows active=0 and the others 0, Windows has no audio driver for this machine's sound "
                       "hardware: install the maker's audio driver (Realtek, Intel, the PC maker's support page), then "
                       "restart Aestra.");
}

} // namespace Audio
} // namespace Aestra

#endif
