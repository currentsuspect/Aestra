// Â© 2025 Nomad Studios â€” All Rights Reserved. Licensed for personal & educational use only.
#include "WASAPISharedDriver.h"

// Windows-specific includes (only in .cpp file)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <cstring>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "avrt.lib")

namespace Nomad {
namespace Audio {

namespace {
    const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
    const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
    const IID IID_IAudioClient = __uuidof(IAudioClient);
    const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);
    const IID IID_IAudioCaptureClient = __uuidof(IAudioCaptureClient);

    // Helper: Convert HRESULT to string
    std::string HResultToString(HRESULT hr) {
        std::ostringstream oss;
        oss << "0x" << std::hex << hr;
        return oss.str();
    }
}

WASAPISharedDriver::WASAPISharedDriver() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    m_perfFreq = freq.QuadPart;
}

WASAPISharedDriver::~WASAPISharedDriver() {
    shutdown();
}

DriverCapability WASAPISharedDriver::getCapabilities() const {
    return DriverCapability::PLAYBACK |
           DriverCapability::RECORDING |
           DriverCapability::DUPLEX |
           DriverCapability::SAMPLE_RATE_CONVERSION |
           DriverCapability::BIT_DEPTH_CONVERSION |
           DriverCapability::HOT_PLUG_DETECTION |
           DriverCapability::CHANNEL_MIXING;
}

bool WASAPISharedDriver::initialize() {
    if (m_state != DriverState::UNINITIALIZED) {
        return true; // Already initialized
    }

    if (!initializeCOM()) {
        return false;
    }

    m_state = DriverState::INITIALIZED;
    m_lastError = DriverError::NONE;
    m_errorMessage.clear();

    std::cout << "[WASAPI Shared] Driver initialized successfully" << std::endl;
    return true;
}

void WASAPISharedDriver::shutdown() {
    stopStream();
    closeStream();
    shutdownCOM();
    m_state = DriverState::UNINITIALIZED;
}

bool WASAPISharedDriver::isAvailable() const {
    // WASAPI is available on Windows Vista and later
    // For now, assume it's always available on modern Windows
    return true;
}

bool WASAPISharedDriver::initializeCOM() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
        setError(DriverError::INITIALIZATION_FAILED, "COM initialization failed: " + HResultToString(hr));
        return false;
    }

    hr = CoCreateInstance(
        CLSID_MMDeviceEnumerator,
        nullptr,
        CLSCTX_ALL,
        IID_IMMDeviceEnumerator,
        (void**)&m_deviceEnumerator
    );

    if (FAILED(hr)) {
        setError(DriverError::INITIALIZATION_FAILED, "Failed to create device enumerator: " + HResultToString(hr));
        CoUninitialize();
        return false;
    }

    return true;
}

void WASAPISharedDriver::shutdownCOM() {
    if (m_deviceEnumerator) {
        reinterpret_cast<IMMDeviceEnumerator*>(m_deviceEnumerator)->Release();
        m_deviceEnumerator = nullptr;
    }
    CoUninitialize();
}

std::vector<AudioDeviceInfo> WASAPISharedDriver::getDevices() const {
    std::vector<AudioDeviceInfo> devices;

    if (!m_deviceEnumerator) {
        const_cast<WASAPISharedDriver*>(this)->setError(DriverError::INITIALIZATION_FAILED, "Device enumerator not initialized");
        return devices;
    }

    enumerateDevices(devices);
    return devices;
}

bool WASAPISharedDriver::enumerateDevices(std::vector<AudioDeviceInfo>& devices) const {
    IMMDeviceCollection* deviceCollection = nullptr;

    // Enumerate output devices
    HRESULT hr = reinterpret_cast<IMMDeviceEnumerator*>(m_deviceEnumerator)->EnumAudioEndpoints(
        eRender,
        DEVICE_STATE_ACTIVE,
        &deviceCollection
    );

    if (SUCCEEDED(hr)) {
        UINT count = 0;
        deviceCollection->GetCount(&count);

        for (UINT i = 0; i < count; ++i) {
            IMMDevice* device = nullptr;
            if (SUCCEEDED(deviceCollection->Item(i, &device))) {
                AudioDeviceInfo info;
                info.id = i;
                info.maxOutputChannels = 2; // Default to stereo
                info.maxInputChannels = 0;
                info.preferredSampleRate = 48000;
                info.supportedSampleRates = { 44100, 48000, 96000 };
                info.isDefaultOutput = (i == 0); // First device is default
                info.isDefaultInput = false;

                // Get device name
                LPWSTR deviceId = nullptr;
                if (SUCCEEDED(device->GetId(&deviceId))) {
                    IPropertyStore* propertyStore = nullptr;
                    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &propertyStore))) {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        if (SUCCEEDED(propertyStore->GetValue(PKEY_Device_FriendlyName, &varName))) {
                            // Convert wide string to narrow
                            int len = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                            if (len > 0) {
                                std::string name(len - 1, '\0');
                                WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &name[0], len, nullptr, nullptr);
                                info.name = name;
                            }
                            PropVariantClear(&varName);
                        }
                        propertyStore->Release();
                    }
                    CoTaskMemFree(deviceId);
                }

                devices.push_back(info);
                device->Release();
            }
        }
        deviceCollection->Release();
    }

    return !devices.empty();
}

uint32_t WASAPISharedDriver::getDefaultOutputDevice() {
    return 0; // First device is default
}

uint32_t WASAPISharedDriver::getDefaultInputDevice() {
    return 0; // First device is default
}

bool WASAPISharedDriver::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
    if (m_state == DriverState::STREAM_RUNNING) {
        stopStream();
    }
    if (m_state == DriverState::STREAM_OPEN) {
        closeStream();
    }

    m_config = config;
    m_userCallback = callback;
    m_userData = userData;

    if (!openDevice(config.deviceId)) {
        return false;
    }

    if (!initializeAudioClient()) {
        closeDevice();
        return false;
    }

    m_state = DriverState::STREAM_OPEN;
    std::cout << "[WASAPI Shared] Stream opened successfully" << std::endl;
    return true;
}

bool WASAPISharedDriver::openDevice(uint32_t deviceId) {
    if (!m_deviceEnumerator) {
        setError(DriverError::DEVICE_NOT_FOUND, "Device enumerator not initialized");
        return false;
    }

    // Get default device for now (deviceId ignored)
    HRESULT hr = reinterpret_cast<IMMDeviceEnumerator*>(m_deviceEnumerator)->GetDefaultAudioEndpoint(
        eRender,
        eConsole,
        (IMMDevice**)&m_device
    );

    if (FAILED(hr)) {
        setError(DriverError::DEVICE_NOT_FOUND, "Failed to get default audio device: " + HResultToString(hr));
        return false;
    }

    return true;
}

void WASAPISharedDriver::closeDevice() {
    if (m_waveFormat) {
        CoTaskMemFree(m_waveFormat);
        m_waveFormat = nullptr;
    }

    if (m_renderClient) {
        reinterpret_cast<IAudioRenderClient*>(m_renderClient)->Release();
        m_renderClient = nullptr;
    }

    if (m_captureClient) {
        reinterpret_cast<IAudioCaptureClient*>(m_captureClient)->Release();
        m_captureClient = nullptr;
    }

    if (m_audioClient3) {
        reinterpret_cast<IAudioClient3*>(m_audioClient3)->Release();
        m_audioClient3 = nullptr;
    }

    if (m_audioClient) {
        reinterpret_cast<IAudioClient*>(m_audioClient)->Release();
        m_audioClient = nullptr;
    }

    if (m_device) {
        reinterpret_cast<IMMDevice*>(m_device)->Release();
        m_device = nullptr;
    }

    if (m_audioEvent) {
        CloseHandle(m_audioEvent);
        m_audioEvent = nullptr;
    }
}

bool WASAPISharedDriver::initializeAudioClient() {
    HRESULT hr = reinterpret_cast<IMMDevice*>(m_device)->Activate(
        IID_IAudioClient,
        CLSCTX_ALL,
        nullptr,
        (void**)&m_audioClient
    );

    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to activate audio client: " + HResultToString(hr));
        return false;
    }

    // Get the mix format
    hr = reinterpret_cast<IAudioClient*>(m_audioClient)->GetMixFormat(reinterpret_cast<WAVEFORMATEX**>(&m_waveFormat));
    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to get mix format: " + HResultToString(hr));
        return false;
    }

    // Log the format we got
    WAVEFORMATEX* wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
    std::cout << "[WASAPI Shared] Mix format: ";
    if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
        std::cout << "32-bit float";
    } else if (wf->wFormatTag == WAVE_FORMAT_PCM) {
        std::cout << wf->wBitsPerSample << "-bit PCM";
    } else if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        WAVEFORMATEXTENSIBLE* wfex = (WAVEFORMATEXTENSIBLE*)wf;
        if (wfex->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
            std::cout << "32-bit float (extensible)";
        } else if (wfex->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
            std::cout << wf->wBitsPerSample << "-bit PCM (extensible)";
        } else {
            std::cout << "UNKNOWN EXTENSIBLE SUBFORMAT";
        }
    } else {
        std::cout << "UNKNOWN FORMAT TAG: " << wf->wFormatTag;
    }
    std::cout << " @ " << wf->nSamplesPerSec << " Hz, " 
              << wf->nChannels << " channels" << std::endl;

    // Try to use IAudioClient3 for low-latency shared mode (Windows 10+)
    bool usingIAudioClient3 = false;
    hr = reinterpret_cast<IAudioClient*>(m_audioClient)->QueryInterface(__uuidof(IAudioClient3), (void**)&m_audioClient3);
    if (SUCCEEDED(hr) && m_audioClient3) {
        std::cout << "[WASAPI Shared] IAudioClient3 available - attempting low-latency mode" << std::endl;
        
        // Query supported engine periods
        WAVEFORMATEX* wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
        UINT32 defaultPeriodFrames, fundamentalPeriodFrames, minPeriodFrames, maxPeriodFrames;
        hr = reinterpret_cast<IAudioClient3*>(m_audioClient3)->GetSharedModeEnginePeriod(
            wf,
            &defaultPeriodFrames,
            &fundamentalPeriodFrames,
            &minPeriodFrames,
            &maxPeriodFrames
        );
        
        if (SUCCEEDED(hr)) {
            std::cout << "[WASAPI Shared] Engine periods - Min: " << minPeriodFrames 
                      << ", Default: " << defaultPeriodFrames 
                      << ", Max: " << maxPeriodFrames << " frames" << std::endl;
            
            // Choose a safe period: honor requested buffer size when possible.
            // Using absolute minPeriodFrames in shared mode is often too aggressive
            // and can cause underruns on real projects.
            UINT32 targetPeriodFrames = m_config.bufferSize;
            if (targetPeriodFrames < minPeriodFrames) targetPeriodFrames = minPeriodFrames;
            if (targetPeriodFrames > maxPeriodFrames) targetPeriodFrames = maxPeriodFrames;

            // Align to the device's fundamental period if required.
            if (fundamentalPeriodFrames > 0) {
                UINT32 aligned = ((targetPeriodFrames + fundamentalPeriodFrames - 1) / fundamentalPeriodFrames) * fundamentalPeriodFrames;
                if (aligned <= maxPeriodFrames) {
                    targetPeriodFrames = aligned;
                }
            }
            
            // Create event for audio thread synchronization
            m_audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!m_audioEvent) {
                setError(DriverError::STREAM_OPEN_FAILED, "Failed to create audio event");
                return false;
            }
            
            // Initialize with low-latency shared mode
            hr = reinterpret_cast<IAudioClient3*>(m_audioClient3)->InitializeSharedAudioStream(
                AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                targetPeriodFrames,
                wf,
                nullptr
            );
            
            if (SUCCEEDED(hr)) {
                usingIAudioClient3 = true;
                m_bufferFrameCount = targetPeriodFrames;
                std::cout << "[WASAPI Shared] Using IAudioClient3 low-latency mode: " 
                          << targetPeriodFrames << " frames" << std::endl;
                
                // Set event handle
                hr = reinterpret_cast<IAudioClient3*>(m_audioClient3)->SetEventHandle(reinterpret_cast<HANDLE>(m_audioEvent));
                if (FAILED(hr)) {
                    setError(DriverError::STREAM_OPEN_FAILED, "Failed to set event handle (IAudioClient3): " + HResultToString(hr));
                    return false;
                }
            } else {
                std::cout << "[WASAPI Shared] IAudioClient3 initialization failed, falling back to legacy mode" << std::endl;
                CloseHandle(m_audioEvent);
                m_audioEvent = nullptr;
            }
        }
    }
    
    // Fallback to classic IAudioClient if IAudioClient3 failed or unavailable
    if (!usingIAudioClient3) {
        std::cout << "[WASAPI Shared] Using legacy IAudioClient mode" << std::endl;
        
        // Convert buffer size from frames to duration (100ns units)
        REFERENCE_TIME requestedDuration = (REFERENCE_TIME)(
            10000000.0 * m_config.bufferSize / m_config.sampleRate
        );

        // Create event for audio thread synchronization
        if (!m_audioEvent) {
            m_audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            if (!m_audioEvent) {
                setError(DriverError::STREAM_OPEN_FAILED, "Failed to create audio event");
                return false;
            }
        }

        // Initialize audio client in shared mode with event-driven callback
        hr = reinterpret_cast<IAudioClient*>(m_audioClient)->Initialize(
            AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
            requestedDuration,
            0,
            reinterpret_cast<const WAVEFORMATEX*>(m_waveFormat),
            nullptr
        );

        if (FAILED(hr)) {
            setError(DriverError::STREAM_OPEN_FAILED, "Failed to initialize audio client: " + HResultToString(hr));
            return false;
        }

        // Set event handle
        hr = reinterpret_cast<IAudioClient*>(m_audioClient)->SetEventHandle(reinterpret_cast<HANDLE>(m_audioEvent));
        if (FAILED(hr)) {
            setError(DriverError::STREAM_OPEN_FAILED, "Failed to set event handle: " + HResultToString(hr));
            return false;
        }

        // Get buffer size
        hr = reinterpret_cast<IAudioClient*>(m_audioClient)->GetBufferSize(&m_bufferFrameCount);
        if (FAILED(hr)) {
            setError(DriverError::STREAM_OPEN_FAILED, "Failed to get buffer size: " + HResultToString(hr));
            return false;
        }
    }

    // Get render client
    hr = reinterpret_cast<IAudioClient*>(m_audioClient)->GetService(IID_IAudioRenderClient, (void**)&m_renderClient);
    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to get render client: " + HResultToString(hr));
        return false;
    }

    // Calculate and report accurate latency metrics
    wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
    AudioLatencyInfo latencyInfo = AudioLatencyInfo::calculate(
        m_bufferFrameCount,
        wf->nSamplesPerSec,
        3.0  // Shared mode typically has ~3x RTL multiplier
    );
    
    std::cout << "[WASAPI Shared] Initialized - "
              << "Sample Rate: " << wf->nSamplesPerSec << " Hz, "
              << "Buffer: " << m_bufferFrameCount << " frames\n"
              << "  Buffer Period: " << std::fixed << std::setprecision(2) << latencyInfo.bufferPeriodMs << "ms (one-way)\n"
              << "  Estimated RTL: " << latencyInfo.estimatedRTL_Ms << "ms (round-trip)"
              << std::endl;

    return true;
}

void WASAPISharedDriver::closeStream() {
    closeDevice();
    m_state = DriverState::INITIALIZED;
    std::cout << "[WASAPI Shared] Stream closed" << std::endl;
}

bool WASAPISharedDriver::startStream() {
    if (m_state != DriverState::STREAM_OPEN) {
        setError(DriverError::STREAM_START_FAILED, "Stream not open");
        return false;
    }

    m_shouldStop = false;
    m_isRunning = true;

    // Start audio client
    HRESULT hr = reinterpret_cast<IAudioClient*>(m_audioClient)->Start();
    if (FAILED(hr)) {
        setError(DriverError::STREAM_START_FAILED, "Failed to start audio client: " + HResultToString(hr));
        m_isRunning = false;
        return false;
    }

    // Start audio thread
    m_audioThread = std::thread(&WASAPISharedDriver::audioThreadProc, this);

    m_state = DriverState::STREAM_RUNNING;
    std::cout << "[WASAPI Shared] Stream started" << std::endl;
    return true;
}

void WASAPISharedDriver::stopStream() {
    if (!m_isRunning) {
        return;
    }

    std::cout << "[WASAPI Shared] Stopping stream safely..." << std::endl;
    
    m_shouldStop = true;
    
    // Signal event to wake up thread immediately
    if (m_audioEvent) {
        SetEvent(m_audioEvent);
    }

    // Wait for thread to finish with proper shutdown sequence
    if (m_audioThread.joinable()) {
        // Wait a short period for thread to exit gracefully
        if (m_audioThread.joinable()) {
            m_audioThread.join();  // Try normal join first
        }
        
        // If still joinable after a brief wait, it may be hung - detach to prevent std::terminate
        if (m_audioThread.joinable()) {
            std::cerr << "[WASAPI Shared] Warning: Audio thread didn't stop gracefully, detaching" << std::endl;
            m_audioThread.detach();
        } else {
            // Thread stopped successfully - safe to fill buffer
            fillAudioBufferWithSilence();
        }
    } else {
        // Thread was not joinable (already detached or not started)
        fillAudioBufferWithSilence();
    }

    // Stop audio client
    if (m_audioClient) {
        reinterpret_cast<IAudioClient*>(m_audioClient)->Stop();
    }

    m_isRunning = false;
    m_state = DriverState::STREAM_OPEN;
    std::cout << "[WASAPI Shared] Stream stopped safely" << std::endl;
}

void WASAPISharedDriver::fillAudioBufferWithSilence() {
    // Fill buffer with silence before stopping to prevent clicks
    // Only call this after thread has stopped to avoid race conditions
    if (m_audioClient && m_renderClient && m_bufferFrameCount > 0) {
        UINT32 padding = 0;
        HRESULT hr = reinterpret_cast<IAudioClient*>(m_audioClient)->GetCurrentPadding(&padding);
        if (SUCCEEDED(hr)) {
            UINT32 framesAvailable = m_bufferFrameCount - padding;
            if (framesAvailable > 0) {
                BYTE* data = nullptr;
                hr = reinterpret_cast<IAudioRenderClient*>(m_renderClient)->GetBuffer(framesAvailable, &data);
                if (SUCCEEDED(hr)) {
                    std::memset(reinterpret_cast<void*>(data), 0, framesAvailable * reinterpret_cast<WAVEFORMATEX*>(m_waveFormat)->nBlockAlign);
                    reinterpret_cast<IAudioRenderClient*>(m_renderClient)->ReleaseBuffer(framesAvailable, AUDCLNT_BUFFERFLAGS_SILENT);
                }
            }
        }
    }
}

double WASAPISharedDriver::getStreamLatency() const {
    if (!m_audioClient || !m_waveFormat) {
        return 0.0;
    }

    REFERENCE_TIME latency = 0;
    HRESULT hr = reinterpret_cast<IAudioClient*>(m_audioClient)->GetStreamLatency(&latency);
    if (SUCCEEDED(hr)) {
        // Convert from 100ns units to seconds
        return static_cast<double>(latency) / 10000000.0;
    }

    return 0.0;
}

void WASAPISharedDriver::audioThreadProc() {
    if (!setThreadPriority()) {
        std::cerr << "[WASAPI Shared] Warning: Failed to set thread priority" << std::endl;
    }

    // Set MMCSS Pro Audio scheduling for stable performance
    DWORD taskIndex = 0;
    HANDLE avrtHandle = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    if (!avrtHandle) {
        std::cerr << "[WASAPI Shared] Warning: Failed to set MMCSS characteristics" << std::endl;
    } else {
        // Set thread priority (lower than Exclusive mode since shared mode has higher latency)
        BOOL prioritySuccess = AvSetMmThreadPriority(avrtHandle, AVRT_PRIORITY_HIGH);
        if (!prioritySuccess) {
            std::cerr << "[WASAPI Shared] Warning: Failed to set MMCSS priority to HIGH" << std::endl;
        } else {
            std::cout << "[WASAPI Shared] MMCSS enabled: Pro Audio @ HIGH priority" << std::endl;
        }
    }

    // Temporary buffer for user callback
    std::vector<float> userBuffer(m_bufferFrameCount * m_config.numOutputChannels);
    
    WAVEFORMATEX* wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
    std::cout << "[WASAPI Shared] Audio thread running with " 
              << m_bufferFrameCount << " frames at " << wf->nSamplesPerSec << " Hz" << std::endl;

    while (!m_shouldStop) {
        // Wait for buffer event
        DWORD waitResult = WaitForSingleObject(m_audioEvent, 2000);
        
        if (waitResult != WAIT_OBJECT_0) {
            if (!m_shouldStop) {
                std::cerr << "[WASAPI Shared] Audio event timeout" << std::endl;
                m_statistics.underrunCount++;
            }
            continue;
        }

        if (m_shouldStop) {
            break;
        }

        LARGE_INTEGER startTime;
        QueryPerformanceCounter(&startTime);

        // Get padding (number of frames already in buffer)
        UINT32 padding = 0;
        HRESULT hr = reinterpret_cast<IAudioClient*>(m_audioClient)->GetCurrentPadding(&padding);
        if (FAILED(hr)) {
            continue;
        }

        // Calculate available frames
        UINT32 availableFrames = m_bufferFrameCount - padding;
        if (availableFrames == 0) {
            continue;
        }

        // Get buffer from WASAPI
        BYTE* data = nullptr;
        hr = reinterpret_cast<IAudioRenderClient*>(m_renderClient)->GetBuffer(availableFrames, &data);
        if (FAILED(hr)) {
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED || 
                hr == AUDCLNT_E_SERVICE_NOT_RUNNING || 
                hr == AUDCLNT_E_RESOURCES_INVALIDATED) {
                // SECURE-LOG: We avoid I/O logging here in the RT thread.
                // Store error details atomically for the main thread to pick up.
                m_deferredHResult.store(static_cast<uint32_t>(hr), std::memory_order_relaxed);
                m_deferredError.store(DriverError::DEVICE_NOT_FOUND, std::memory_order_relaxed);
                m_hasDeferredError.store(true, std::memory_order_release);
                
                // Signal thread exit
                break;
            }

            m_statistics.underrunCount++;
            continue;
        }

        // Call user callback
        WAVEFORMATEX* wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
        double streamTime = static_cast<double>(m_statistics.callbackCount * m_bufferFrameCount) / wf->nSamplesPerSec;
        
        if (m_userCallback) {
            m_userCallback(
                userBuffer.data(),
                nullptr,
                availableFrames,
                streamTime,
                m_userData
            );
        } else {
            // Silence
            std::fill(userBuffer.begin(), userBuffer.end(), 0.0f);
        }

        // Convert float to WASAPI format
        if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
            (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && 
             ((WAVEFORMATEXTENSIBLE*)wf)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) {
            // 32-bit float - direct copy
            std::memcpy(reinterpret_cast<void*>(data), userBuffer.data(), availableFrames * wf->nBlockAlign);
        }
        else if (wf->wFormatTag == WAVE_FORMAT_PCM ||
                 (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
                  ((WAVEFORMATEXTENSIBLE*)wf)->SubFormat == KSDATAFORMAT_SUBTYPE_PCM)) {
            // PCM format - need to convert floats to integers
            if (wf->wBitsPerSample == 16) {
                // 16-bit PCM
                int16_t* pcmData = reinterpret_cast<int16_t*>(data);
                for (uint32_t i = 0; i < availableFrames * m_config.numOutputChannels; ++i) {
                    float sample = userBuffer[i];
                    if (sample > 1.0f) sample = 1.0f;
                    if (sample < -1.0f) sample = -1.0f;
                    pcmData[i] = static_cast<int16_t>(sample * 32767.0f);
                }
            }
            else if (wf->wBitsPerSample == 24) {
                // 24-bit PCM
                uint8_t* pcmData = data;
                for (uint32_t i = 0; i < availableFrames * m_config.numOutputChannels; ++i) {
                    float sample = userBuffer[i];
                    if (sample > 1.0f) sample = 1.0f;
                    if (sample < -1.0f) sample = -1.0f;
                    int32_t pcmValue = static_cast<int32_t>(sample * 8388607.0f);
                    pcmData[i * 3 + 0] = static_cast<uint8_t>(pcmValue & 0xFF);
                    pcmData[i * 3 + 1] = static_cast<uint8_t>((pcmValue >> 8) & 0xFF);
                    pcmData[i * 3 + 2] = static_cast<uint8_t>((pcmValue >> 16) & 0xFF);
                }
            }
            else {
                // Unknown bit depth - zero the buffer
                std::memset(reinterpret_cast<void*>(data), 0, availableFrames * wf->nBlockAlign);
            }
        }
        else {
            // Unknown format - zero the buffer
            std::memset(reinterpret_cast<void*>(data), 0, availableFrames * reinterpret_cast<WAVEFORMATEX*>(m_waveFormat)->nBlockAlign);
        }

        // Release buffer
        hr = reinterpret_cast<IAudioRenderClient*>(m_renderClient)->ReleaseBuffer(availableFrames, 0);
        if (FAILED(hr)) {
             if (hr == AUDCLNT_E_DEVICE_INVALIDATED || 
                hr == AUDCLNT_E_SERVICE_NOT_RUNNING) {
                // SECURE-LOG: Avoid I/O and allocation in RT thread
                m_deferredHResult.store(static_cast<uint32_t>(hr), std::memory_order_relaxed);
                m_deferredError.store(DriverError::DEVICE_NOT_FOUND, std::memory_order_relaxed);
                m_hasDeferredError.store(true, std::memory_order_release);
                break;
            }
            std::cerr << "[WASAPI Shared] Failed to release buffer" << std::endl;
        }

        // Update statistics
        LARGE_INTEGER endTime;
        QueryPerformanceCounter(&endTime);
        double callbackTimeUs = static_cast<double>(endTime.QuadPart - startTime.QuadPart) * 1000000.0 / static_cast<double>(m_perfFreq);
        updateStatistics(callbackTimeUs);
    }

    if (avrtHandle) {
        AvRevertMmThreadCharacteristics(avrtHandle);
    }
    
    // Ensure properly marked as stopped
    m_isRunning = false;

    std::cout << "[WASAPI Shared] Audio thread exiting" << std::endl;
}

bool WASAPISharedDriver::setThreadPriority() {
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
}

void WASAPISharedDriver::setError(DriverError error, const std::string& message) {
    m_lastError = error;
    m_errorMessage = message;
    m_state = DriverState::DRIVER_ERROR;

    std::cerr << "[WASAPI Shared] Error: " << message << std::endl;

    if (m_errorCallback) {
        m_errorCallback(error, message);
    }
}

void WASAPISharedDriver::updateStatistics(double callbackTimeUs) {
    m_statistics.callbackCount++;
    
    // Update average callback time (exponential moving average)
    const double alpha = 0.1;
    m_statistics.averageCallbackTimeUs = 
        alpha * callbackTimeUs + (1.0 - alpha) * m_statistics.averageCallbackTimeUs;
    
    // Update max callback time
    if (callbackTimeUs > m_statistics.maxCallbackTimeUs) {
        m_statistics.maxCallbackTimeUs = callbackTimeUs;
    }

    // Estimate CPU load
    if (m_waveFormat && m_bufferFrameCount > 0) {
        WAVEFORMATEX* wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
        double bufferDurationUs = static_cast<double>(m_bufferFrameCount) * 1000000.0 / wf->nSamplesPerSec;
        m_statistics.cpuLoadPercent = (callbackTimeUs / bufferDurationUs) * 100.0;
    }

    // Avoid COM calls on the RT thread. Approximate latency from buffer period.
    if (m_waveFormat && m_bufferFrameCount > 0) {
        WAVEFORMATEX* wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
        m_statistics.actualLatencyMs =
            (static_cast<double>(m_bufferFrameCount) * 1000.0) / wf->nSamplesPerSec;
    }
}

uint32_t WASAPISharedDriver::getStreamSampleRate() const {
    if (!m_waveFormat) return 0;
    WAVEFORMATEX* wf = reinterpret_cast<WAVEFORMATEX*>(m_waveFormat);
    return wf->nSamplesPerSec;
}



bool WASAPISharedDriver::pollDeferredError(DriverError& outError, std::string& outMsg) {
    if (m_hasDeferredError.exchange(false, std::memory_order_acquire)) {
        outError = m_deferredError.load(std::memory_order_relaxed);
        uint32_t hr = m_deferredHResult.load(std::memory_order_relaxed);
        
        // Construct detailed error message ON THE MAIN THREAD
        // Convert HRESULT to string manually since HResultToString might be helper
        // Use standard hex formatting if HResultToString not available or expensive
        std::stringstream ss;
        ss << "Audio device disconnected or invalidated (HRESULT: 0x" 
           << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << hr << ")";
           
        outMsg = "Audio device disconnected or invalidated."; // User friendly msg
        
        // Log the detailed one locally
        std::cerr << "[WASAPIShared] Deferred Error: " << ss.str() << std::endl;
        
        // Sync internal state
        m_lastError = outError;
        m_errorMessage = outMsg;
        m_state = DriverState::DRIVER_ERROR;
        
        return true;
    }
    return false;
}

} // namespace Audio
} // namespace Nomad
