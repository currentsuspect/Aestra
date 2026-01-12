// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "ASIODriver.h"
#include "AudioEngine.h" // For Logging if needed, or use generic
#include <iostream>
#include <algorithm>
#include <Windows.h>
#include "../../NomadCore/include/NomadThreading.h"

namespace Nomad {
namespace Audio {

// Static Instance Initialization
ASIODriver* ASIODriver::s_instance = nullptr;

namespace {
    // Static C-style callback wrappers
    void wrapper_bufferSwitch(long doubleBufferIndex, long directProcess) {
        if (ASIODriver::s_instance) {
            ASIODriver::s_instance->asioBufferSwitch(doubleBufferIndex, directProcess);
        }
    }

    void wrapper_sampleRateDidChange(double sRate) {
        if (ASIODriver::s_instance) {
            ASIODriver::s_instance->asioSampleRateDidChange(sRate);
        }
    }

    long wrapper_asioMessage(long selector, long value, void* message, double* opt) {
        if (ASIODriver::s_instance) {
            return ASIODriver::s_instance->asioMessage(selector, value, message, opt);
        }
        return 0;
    }

    ASIO::ASIOTime* wrapper_bufferSwitchTimeInfo(ASIO::ASIOTime* timeInfo, long doubleBufferIndex, long directProcess) {
        if (ASIODriver::s_instance) {
            return ASIODriver::s_instance->asioBufferSwitch(doubleBufferIndex, directProcess);
        }
        return nullptr;
    }
}

ASIO::ASIOCallbacks ASIODriver::s_asioCallbacks = {
    wrapper_bufferSwitch,
    wrapper_sampleRateDidChange,
    wrapper_asioMessage,
    wrapper_bufferSwitchTimeInfo
};

// =============================================================================
// Implementation
// =============================================================================

ASIODriver::ASIODriver() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED); // Ensure COM is ready
}

ASIODriver::~ASIODriver() {
    if (m_asio) {
        if (m_isRunning) stopStream();
        m_asio->disposeBuffers();
        m_asio->Release();
        m_asio = nullptr;
    }
    CoUninitialize();
}

bool ASIODriver::isAvailable() const {
    // Basic check: Are there any keys in the registry?
    // We can do a quick scan or just return true if we're on Windows.
    return true; 
}


std::vector<AudioDeviceInfo> ASIODriver::getDevices() {
    std::vector<AudioDeviceInfo> devices;
    m_knownDrivers.clear();
    
    // Scan HKEY_LOCAL_MACHINE\SOFTWARE\ASIO
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ASIO", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        char keyName[256];
        DWORD keyNameLen = 256;
        DWORD index = 0;
        
        while (RegEnumKeyExA(hKey, index, keyName, &keyNameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            HKEY hSubKey;
            if (RegOpenKeyExA(hKey, keyName, 0, KEY_READ, &hSubKey) == ERROR_SUCCESS) {
                char clsidStr[128];
                char descStr[128];
                DWORD clsidLen = sizeof(clsidStr);
                DWORD descLen = sizeof(descStr);
                DWORD type;
                
                if (RegQueryValueExA(hSubKey, "CLSID", nullptr, &type, (LPBYTE)clsidStr, &clsidLen) == ERROR_SUCCESS) {
                    if (RegQueryValueExA(hSubKey, "Description", nullptr, &type, (LPBYTE)descStr, &descLen) != ERROR_SUCCESS) {
                        strcpy_s(descStr, keyName);
                    }
                    
                    // Add to internal registry
                    m_knownDrivers.push_back({ std::string(descStr), std::string(clsidStr) });
                    uint32_t devId = (uint32_t)(m_knownDrivers.size() - 1);

                    devices.push_back({
                        devId,
                        std::string(descStr),
                        2, 2, // Default capability guess (refined on open)
                        {44100, 48000, 88200, 96000}, // Supported rates guess
                        44100,
                        false, false
                    });
                }
                RegCloseKey(hSubKey);
            }
            index++;
            keyNameLen = 256;
        }
        RegCloseKey(hKey);
    }
    
    return devices;
}


bool ASIODriver::loadDriver(uint32_t deviceIndex) {
    if (deviceIndex >= m_knownDrivers.size()) {
        m_lastError = "Invalid device index";
        return false;
    }

    // Ensure COM is initialized (STA required for ASIO)
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    if (hrInit == RPC_E_CHANGED_MODE) {
        // Already initialized as MTA - This is suboptimal but we proceed.
    }

    if (m_asio) {
        m_asio->Release();
        m_asio = nullptr;
    }

    const std::string& argClsid = m_knownDrivers[deviceIndex].clsid;
    m_driverName = m_knownDrivers[deviceIndex].name;

    // Convert string CLSID to GUID
    CLSID clsid;
    std::wstring wClsid(argClsid.begin(), argClsid.end());
    if (CLSIDFromString(wClsid.c_str(), &clsid) != S_OK) {
        m_lastError = "Invalid CLSID format";
        return false;
    }

    // Load Driver via CoCreateInstance
    IUnknown* unk = nullptr;
    HRESULT hrCreate = CoCreateInstance(clsid, nullptr, CLSCTX_INPROC_SERVER, __uuidof(IUnknown), (void**)&unk);
    if (hrCreate != S_OK) {
         m_lastError = "CoCreateInstance failed: " + std::to_string(hrCreate);
         return false;
    }
    
    // Query for IASIO interface
    // Standard IID_IASIO: {47d6d9d4-e949-11d1-86ba-006008caaa53}
    static const GUID IID_IASIO = { 0x47d6d9d4, 0xe949, 0x11d1, { 0x86, 0xba, 0x00, 0x60, 0x08, 0xca, 0xaa, 0x53 } };
    
    HRESULT hrQI = unk->QueryInterface(IID_IASIO, (void**)&m_asio);
    
    // Fallback: Some legacy drivers use their own CLSID as the Interface ID
    if (hrQI != S_OK) {
        hrQI = unk->QueryInterface(clsid, (void**)&m_asio);
    }
    
    unk->Release(); // Always release the IUnknown wrapper
    
    if (hrQI != S_OK || !m_asio) {
        m_lastError = "QueryInterface(IASIO) failed. HRESULT: " + std::to_string(hrQI);
        return false;
    }
    
    // Initialize Driver
    // ASIO init returns 1 (ASIOTrue) on success, 0 on failure.
    // NOTE: It does NOT return ASE_SUCCESS (0x3f4847a0) or ASE_OK (0).
    // Use GetForegroundWindow() or GetConsoleWindow() as ASIO4ALL and others prefer an owned window handle.
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) hwnd = GetDesktopWindow();

    if (m_asio->init(hwnd) != 0) return true;
    if (m_asio->init(nullptr) != 0) return true;

    m_lastError = "ASIO Init failed";
    m_asio->Release();
    m_asio = nullptr;
    return false;
}

bool ASIODriver::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    if (m_isRunning) return false;

    // Load Driver if not loaded or different
    // Since we rely on index, we can't easily check 'if different' unless we store current index.
    // For now, always reload to be safe.
    if (!loadDriver(config.deviceId)) { 
         // Attempt to rescan?
         getDevices(); // refresh list
         if (!loadDriver(config.deviceId)) return false;
    }
    
    // 2. Query Capabilities
    long inputCh = 0, outputCh = 0;
    m_asio->getChannels(&inputCh, &outputCh);
    
    // 3. Setup Buffers
    long minSize = 0, maxSize = 0, prefSize = 0, gran = 0;
    ASIO::ASIOError errBuffer = (ASIO::ASIOError)m_asio->getBufferSize(&minSize, &maxSize, &prefSize, &gran);
    if (errBuffer != ASIO::ASE_OK) {
        m_lastError = "getBufferSize failed";
        return false;
    }
    
    m_bufferSize = config.bufferSize;
    if (m_bufferSize < minSize) m_bufferSize = minSize; // Clamp to valid range
    if (m_bufferSize > maxSize) m_bufferSize = maxSize;
    
    // 4. Create Buffers
    m_bufferInfos.clear();
    int activeIn = std::min((int)config.numInputChannels, (int)inputCh);
    int activeOut = std::min((int)config.numOutputChannels, (int)outputCh);
    
    if (activeIn + activeOut == 0) {
        m_lastError = "No channels to open";
        return false;
    }
    
    // Input Buffers
    for(int i=0; i<activeIn; ++i) {
        ASIO::ASIOBufferInfo info;
        memset(&info, 0, sizeof(info)); 
        info.isInput = 1;
        info.channelNum = i;
        info.buffers[0] = info.buffers[1] = nullptr;
        m_bufferInfos.push_back(info);
    }
    
    // Output Buffers
    for(int i=0; i<activeOut; ++i) {
        ASIO::ASIOBufferInfo info;
        memset(&info, 0, sizeof(info));
        info.isInput = 0;
        info.channelNum = i;
        info.buffers[0] = info.buffers[1] = nullptr;
        m_bufferInfos.push_back(info);
    }
    
    ASIO::ASIOError err = (ASIO::ASIOError)m_asio->createBuffers(m_bufferInfos.data(), (long)m_bufferInfos.size(), m_bufferSize, &s_asioCallbacks);
    if (err != ASIO::ASE_OK && err != ASIO::ASE_SUCCESS) { 
        m_lastError = "CreateBuffers failed: " + std::to_string(err);
        return false;
    }
    
    // Get Channel Infos
    m_channelInfos.resize(m_bufferInfos.size());
    for(size_t i=0; i<m_bufferInfos.size(); ++i) {
        m_channelInfos[i].channel = m_bufferInfos[i].channelNum;
        m_channelInfos[i].isInput = m_bufferInfos[i].isInput;
        m_asio->getChannelInfo(&m_channelInfos[i]);
    }
    
    m_callback = callback;
    m_callbackUserData = userData;
    m_inputChannels = activeIn;
    m_outputChannels = activeOut;
    
    m_interleavedInput.resize(m_bufferSize * activeIn);
    m_interleavedOutput.resize(m_bufferSize * activeOut);
    
    // Set Sample Rate
    double currentRate = 0;
    m_asio->getSampleRate(&currentRate);
    if (abs(currentRate - (double)config.sampleRate) > 1.0) {
        if (m_asio->canSampleRate((double)config.sampleRate) == ASIO::ASE_OK) {
             m_asio->setSampleRate((double)config.sampleRate);
             m_sampleRate = (double)config.sampleRate;
        } else {
             m_sampleRate = currentRate;
             // Sample rate mismatch but continuing with driver's rate
        }
    } else {
        m_sampleRate = currentRate;
    }
    
    s_instance = this;
    return true;
}

void ASIODriver::closeStream() {
    stopStream();
    if (m_asio) {
        m_asio->disposeBuffers();
    }
    m_bufferInfos.clear();
    s_instance = nullptr;
}

bool ASIODriver::startStream() {
    if (!m_asio) return false;
    ASIO::ASIOError err = (ASIO::ASIOError)m_asio->start();
    if (err == ASIO::ASE_OK || err == ASIO::ASE_SUCCESS) {
        m_isRunning = true;
        return true;
    }
    return false;
}

void ASIODriver::stopStream() {
    if (m_asio && m_isRunning) {
        m_asio->stop();
    }
    m_isRunning = false;
}

double ASIODriver::getStreamLatency() const {
    if (!m_asio) return 0.0;
    long inLat, outLat;
    m_asio->getLatencies(&inLat, &outLat); 
    return (double)(inLat + outLat) / m_sampleRate;
}

uint32_t ASIODriver::getStreamSampleRate() const {
    return (uint32_t)m_sampleRate;
}

uint32_t ASIODriver::getStreamBufferSize() const {
    return (uint32_t)m_bufferSize;
}

// =============================================================================
// Real-Time Callback
// =============================================================================

// Helper: Convert ASIO Input -> Float Interleaved
void ASIODriver::convertInput(long asioChannelIndex, float* destInterleaved, size_t frames) {
    // Implementation intentionally left empty to use inline logic in bufferSwitch for optimization 
    // or if we decide to factor it out. 
    // For this revision, the logic is inside asioBufferSwitch.
}

ASIO::ASIOTime* ASIODriver::asioBufferSwitch(long doubleBufferIndex, long directProcess) {
    if (!m_isRunning || !m_callback) return nullptr;
    
    // "Black Magic": Ensure ASIO Driver thread runs at Real-Time Priority
    // This is safe to call repeatedly but checking a static flag is faster
    static thread_local bool s_threadPrioritySet = false;
    if (!s_threadPrioritySet) {
        MMCSS::setProAudio();
        s_threadPrioritySet = true;
    }

    // 1. De-interleave Input (ASIO -> Interleaved Float)
    for (int i=0; i < m_inputChannels; ++i) {
        ASIO::ASIOChannelInfo& chInfo = m_channelInfos[i];
        void* src = m_bufferInfos[i].buffers[doubleBufferIndex];
        
        switch(chInfo.type) {
            case ASIO::ASIOSTInt32LSB: {
                int32_t* src32 = static_cast<int32_t*>(src);
                for(long f=0; f<m_bufferSize; ++f) {
                    m_interleavedInput[f * m_inputChannels + i] = static_cast<float>(src32[f]) * (1.0f / 2147483648.0f);
                }
                break;
            }
            case ASIO::ASIOSTInt16LSB: {
                int16_t* src16 = static_cast<int16_t*>(src);
                for(long f=0; f<m_bufferSize; ++f) {
                    m_interleavedInput[f * m_inputChannels + i] = static_cast<float>(src16[f]) * (1.0f / 32768.0f);
                }
                break;
            }
            case ASIO::ASIOSTFloat32LSB: {
                float* srcFloat = static_cast<float*>(src);
                for(long f=0; f<m_bufferSize; ++f) {
                    m_interleavedInput[f * m_inputChannels + i] = srcFloat[f];
                }
                break;
            }
            default: 
                for(long f=0; f<m_bufferSize; ++f) m_interleavedInput[f * m_inputChannels + i] = 0.0f; 
                break;
        }
    }
    
    // 2. Call Engine
    m_callback(m_interleavedOutput.data(), m_interleavedInput.data(), m_bufferSize, 0.0, m_callbackUserData);
    m_stats.callbackCount++;
    
    // 3. Interleave Output (Interleaved Float -> ASIO)
    int outStart = m_inputChannels;
    for (int i=0; i < m_outputChannels; ++i) {
        ASIO::ASIOChannelInfo& chInfo = m_channelInfos[outStart + i];
        void* dest = m_bufferInfos[outStart + i].buffers[doubleBufferIndex];
        
        switch(chInfo.type) {
            case ASIO::ASIOSTInt32LSB: {
                int32_t* dest32 = static_cast<int32_t*>(dest);
                for(long f=0; f<m_bufferSize; ++f) {
                    float v = m_interleavedOutput[f * m_outputChannels + i];
                    if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
                    dest32[f] = static_cast<int32_t>(v * 2147483647.0f);
                }
                break;
            }
            case ASIO::ASIOSTInt16LSB: {
                int16_t* dest16 = static_cast<int16_t*>(dest);
                for(long f=0; f<m_bufferSize; ++f) {
                    float v = m_interleavedOutput[f * m_outputChannels + i];
                    if (v > 1.0f) v = 1.0f; else if (v < -1.0f) v = -1.0f;
                    dest16[f] = static_cast<int16_t>(v * 32767.0f);
                }
                break;
            }
            case ASIO::ASIOSTFloat32LSB: {
                float* destFloat = static_cast<float*>(dest);
                for(long f=0; f<m_bufferSize; ++f) {
                    destFloat[f] = m_interleavedOutput[f * m_outputChannels + i];
                }
                break;
            }
             default: 
                memset(dest, 0, m_bufferSize * 4);
                break;
        }
    }
    
    m_asio->outputReady();
    return nullptr;
}

// Ensure no stray definitions follow this block
void ASIODriver::convertOutput(long index, const float* src, size_t frames) {}

void ASIODriver::asioSampleRateDidChange(double sRate) {
    m_sampleRate = sRate;
}

long ASIODriver::asioMessage(long selector, long value, void* message, double* opt) {
    // 1 = kAsioSelectorSupported
    if (selector == 1) {
        switch(value) {
            case 2: // kAsioResetRequest
            case 3: // kAsioBufferSizeChange
                return 1;
        }
    }
    return 0;
}

} // namespace Audio
} // namespace Nomad
