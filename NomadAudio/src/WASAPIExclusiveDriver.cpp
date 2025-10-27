#include "WASAPIExclusiveDriver.h"
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

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

    // Common exclusive mode sample rates to test
    const uint32_t EXCLUSIVE_SAMPLE_RATES[] = { 44100, 48000, 88200, 96000, 176400, 192000 };

    std::string HResultToString(HRESULT hr) {
        std::ostringstream oss;
        oss << "0x" << std::hex << hr;
        return oss.str();
    }
}

WASAPIExclusiveDriver::WASAPIExclusiveDriver() {
    QueryPerformanceFrequency(&m_perfFreq);
}

WASAPIExclusiveDriver::~WASAPIExclusiveDriver() {
    shutdown();
}

DriverCapability WASAPIExclusiveDriver::getCapabilities() const {
    return DriverCapability::PLAYBACK |
           DriverCapability::RECORDING |
           DriverCapability::DUPLEX |
           DriverCapability::EXCLUSIVE_MODE |
           DriverCapability::EVENT_DRIVEN |
           DriverCapability::HOT_PLUG_DETECTION;
}

bool WASAPIExclusiveDriver::initialize() {
    if (m_state != DriverState::UNINITIALIZED) {
        return true;
    }

    if (!initializeCOM()) {
        return false;
    }

    m_state = DriverState::INITIALIZED;
    m_lastError = DriverError::NONE;
    m_errorMessage.clear();

    std::cout << "[WASAPI Exclusive] Driver initialized successfully" << std::endl;
    return true;
}

void WASAPIExclusiveDriver::shutdown() {
    stopStream();
    closeStream();
    shutdownCOM();
    m_state = DriverState::UNINITIALIZED;
}

bool WASAPIExclusiveDriver::isAvailable() const {
    // Exclusive mode requires Windows Vista or later
    // Check if system supports it by attempting to query capabilities
    return true;
}

bool WASAPIExclusiveDriver::initializeCOM() {
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

void WASAPIExclusiveDriver::shutdownCOM() {
    if (m_deviceEnumerator) {
        m_deviceEnumerator->Release();
        m_deviceEnumerator = nullptr;
    }
    CoUninitialize();
}

std::vector<AudioDeviceInfo> WASAPIExclusiveDriver::getDevices() {
    std::vector<AudioDeviceInfo> devices;

    if (!m_deviceEnumerator) {
        setError(DriverError::INITIALIZATION_FAILED, "Device enumerator not initialized");
        return devices;
    }

    enumerateDevices(devices);
    return devices;
}

bool WASAPIExclusiveDriver::enumerateDevices(std::vector<AudioDeviceInfo>& devices) {
    IMMDeviceCollection* deviceCollection = nullptr;

    HRESULT hr = m_deviceEnumerator->EnumAudioEndpoints(
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
                info.maxOutputChannels = 2;
                info.maxInputChannels = 0;
                info.isDefaultOutput = (i == 0);
                info.isDefaultInput = false;

                // Get supported exclusive sample rates
                info.supportedSampleRates = getSupportedExclusiveSampleRates(i);
                if (!info.supportedSampleRates.empty()) {
                    info.preferredSampleRate = info.supportedSampleRates[0];
                } else {
                    info.preferredSampleRate = 48000;
                }

                // Get device name
                LPWSTR deviceId = nullptr;
                if (SUCCEEDED(device->GetId(&deviceId))) {
                    IPropertyStore* propertyStore = nullptr;
                    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &propertyStore))) {
                        PROPVARIANT varName;
                        PropVariantInit(&varName);
                        if (SUCCEEDED(propertyStore->GetValue(PKEY_Device_FriendlyName, &varName))) {
                            int len = WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, nullptr, 0, nullptr, nullptr);
                            if (len > 0) {
                                std::string name(len - 1, '\0');
                                WideCharToMultiByte(CP_UTF8, 0, varName.pwszVal, -1, &name[0], len, nullptr, nullptr);
                                info.name = name + " (Exclusive)";
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

uint32_t WASAPIExclusiveDriver::getDefaultOutputDevice() {
    return 0;
}

uint32_t WASAPIExclusiveDriver::getDefaultInputDevice() {
    return 0;
}

bool WASAPIExclusiveDriver::isExclusiveModeAvailable(uint32_t deviceId) const {
    // Try to open the device and test exclusive mode
    IMMDevice* testDevice = nullptr;
    
    if (!m_deviceEnumerator) {
        return false;
    }

    HRESULT hr = m_deviceEnumerator->GetDefaultAudioEndpoint(
        eRender,
        eConsole,
        &testDevice
    );

    if (FAILED(hr)) {
        return false;
    }

    IAudioClient* testClient = nullptr;
    hr = testDevice->Activate(
        IID_IAudioClient,
        CLSCTX_ALL,
        nullptr,
        (void**)&testClient
    );

    bool available = false;
    if (SUCCEEDED(hr)) {
        // Test with a common format
        WAVEFORMATEX format = {};
        format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        format.nChannels = 2;
        format.nSamplesPerSec = 48000;
        format.wBitsPerSample = 32;
        format.nBlockAlign = (format.nChannels * format.wBitsPerSample) / 8;
        format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
        format.cbSize = 0;

        WAVEFORMATEX* closestMatch = nullptr;
        hr = testClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &format, &closestMatch);
        
        available = (hr == S_OK);

        if (closestMatch) {
            CoTaskMemFree(closestMatch);
        }

        testClient->Release();
    }

    testDevice->Release();
    return available;
}

std::vector<uint32_t> WASAPIExclusiveDriver::getSupportedExclusiveSampleRates(uint32_t deviceId) const {
    std::vector<uint32_t> supportedRates;

    IMMDevice* testDevice = nullptr;
    if (!m_deviceEnumerator) {
        return supportedRates;
    }

    HRESULT hr = m_deviceEnumerator->GetDefaultAudioEndpoint(
        eRender,
        eConsole,
        &testDevice
    );

    if (FAILED(hr)) {
        return supportedRates;
    }

    IAudioClient* testClient = nullptr;
    hr = testDevice->Activate(
        IID_IAudioClient,
        CLSCTX_ALL,
        nullptr,
        (void**)&testClient
    );

    if (SUCCEEDED(hr)) {
        // Test each sample rate
        for (uint32_t sampleRate : EXCLUSIVE_SAMPLE_RATES) {
            WAVEFORMATEX format = {};
            format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            format.nChannels = 2;
            format.nSamplesPerSec = sampleRate;
            format.wBitsPerSample = 32;
            format.nBlockAlign = (format.nChannels * format.wBitsPerSample) / 8;
            format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
            format.cbSize = 0;

            WAVEFORMATEX* closestMatch = nullptr;
            hr = testClient->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &format, &closestMatch);
            
            if (hr == S_OK) {
                supportedRates.push_back(sampleRate);
            }

            if (closestMatch) {
                CoTaskMemFree(closestMatch);
            }
        }

        testClient->Release();
    }

    testDevice->Release();
    return supportedRates;
}

bool WASAPIExclusiveDriver::openStream(const AudioStreamConfig& config, AudioCallback callback, void* userData) {
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
    std::cout << "[WASAPI Exclusive] Stream opened successfully" << std::endl;
    return true;
}

bool WASAPIExclusiveDriver::openDevice(uint32_t deviceId) {
    if (!m_deviceEnumerator) {
        setError(DriverError::DEVICE_NOT_FOUND, "Device enumerator not initialized");
        return false;
    }

    HRESULT hr = m_deviceEnumerator->GetDefaultAudioEndpoint(
        eRender,
        eConsole,
        &m_device
    );

    if (FAILED(hr)) {
        setError(DriverError::DEVICE_NOT_FOUND, "Failed to get audio device: " + HResultToString(hr));
        return false;
    }

    return true;
}

void WASAPIExclusiveDriver::closeDevice() {
    if (m_waveFormat) {
        CoTaskMemFree(m_waveFormat);
        m_waveFormat = nullptr;
    }

    if (m_renderClient) {
        m_renderClient->Release();
        m_renderClient = nullptr;
    }

    if (m_captureClient) {
        m_captureClient->Release();
        m_captureClient = nullptr;
    }

    if (m_audioClient) {
        m_audioClient->Release();
        m_audioClient = nullptr;
    }

    if (m_device) {
        m_device->Release();
        m_device = nullptr;
    }

    if (m_audioEvent) {
        CloseHandle(m_audioEvent);
        m_audioEvent = nullptr;
    }
}

bool WASAPIExclusiveDriver::initializeAudioClient() {
    HRESULT hr = m_device->Activate(
        IID_IAudioClient,
        CLSCTX_ALL,
        nullptr,
        (void**)&m_audioClient
    );

    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to activate audio client: " + HResultToString(hr));
        return false;
    }

    // Find best exclusive format
    if (!findBestExclusiveFormat(&m_waveFormat)) {
        setError(DriverError::EXCLUSIVE_MODE_UNAVAILABLE, "No compatible exclusive format found");
        return false;
    }

    m_actualSampleRate = m_waveFormat->nSamplesPerSec;

    // Calculate minimum buffer duration (100ns units)
    // For exclusive mode, use smaller buffers for lower latency
    REFERENCE_TIME minDuration = 0;
    hr = m_audioClient->GetDevicePeriod(nullptr, &minDuration);
    if (FAILED(hr)) {
        minDuration = 30000; // Default to 3ms
    }

    // Use requested buffer size, but respect minimum
    REFERENCE_TIME requestedDuration = (REFERENCE_TIME)(
        10000000.0 * m_config.bufferSize / m_actualSampleRate
    );

    if (requestedDuration < minDuration) {
        requestedDuration = minDuration;
        std::cout << "[WASAPI Exclusive] Buffer size adjusted to minimum: " 
                  << (minDuration / 10000.0) << "ms" << std::endl;
    }

    // Create event for audio thread
    m_audioEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_audioEvent) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to create audio event");
        return false;
    }

    // Initialize in exclusive mode
    hr = m_audioClient->Initialize(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
        requestedDuration,
        requestedDuration,  // Exclusive mode requires both durations to match
        m_waveFormat,
        nullptr
    );

    if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
        // Need to align buffer size
        hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
        if (SUCCEEDED(hr)) {
            m_audioClient->Release();
            m_audioClient = nullptr;

            // Recalculate aligned duration
            requestedDuration = (REFERENCE_TIME)(
                10000000.0 * m_bufferFrameCount / m_actualSampleRate + 0.5
            );

            // Try again with aligned buffer
            hr = m_device->Activate(
                IID_IAudioClient,
                CLSCTX_ALL,
                nullptr,
                (void**)&m_audioClient
            );

            if (SUCCEEDED(hr)) {
                hr = m_audioClient->Initialize(
                    AUDCLNT_SHAREMODE_EXCLUSIVE,
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                    requestedDuration,
                    requestedDuration,
                    m_waveFormat,
                    nullptr
                );
            }
        }
    }

    if (hr == AUDCLNT_E_DEVICE_IN_USE) {
        setError(DriverError::DEVICE_IN_USE, "Device is in use by another application");
        return false;
    }

    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to initialize exclusive mode: " + HResultToString(hr));
        return false;
    }

    // Set event handle
    hr = m_audioClient->SetEventHandle(m_audioEvent);
    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to set event handle: " + HResultToString(hr));
        return false;
    }

    // Get actual buffer size
    hr = m_audioClient->GetBufferSize(&m_bufferFrameCount);
    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to get buffer size: " + HResultToString(hr));
        return false;
    }

    // Get render client
    hr = m_audioClient->GetService(IID_IAudioRenderClient, (void**)&m_renderClient);
    if (FAILED(hr)) {
        setError(DriverError::STREAM_OPEN_FAILED, "Failed to get render client: " + HResultToString(hr));
        return false;
    }

    // Calculate accurate latency metrics
    AudioLatencyInfo latencyInfo = AudioLatencyInfo::calculate(
        m_bufferFrameCount, 
        m_actualSampleRate,
        3.0  // Conservative RTL estimate: 3x buffer period
    );
    
    std::cout << "[WASAPI Exclusive] Initialized - "
              << "Sample Rate: " << m_actualSampleRate << " Hz, "
              << "Buffer: " << m_bufferFrameCount << " frames\n"
              << "  Buffer Period: " << std::fixed << std::setprecision(2) << latencyInfo.bufferPeriodMs << "ms (one-way)\n"
              << "  Estimated RTL: " << latencyInfo.estimatedRTL_Ms << "ms (round-trip, device-dependent)"
              << std::endl;

    return true;
}

bool WASAPIExclusiveDriver::findBestExclusiveFormat(WAVEFORMATEX** format) {
    // Try requested sample rate first with different bit depths
    // Most consumer devices support 16-bit, some support 24-bit, fewer support 32-bit float
    
    // Try 16-bit PCM first (most compatible)
    if (testExclusiveFormatPCM(m_config.sampleRate, m_config.numOutputChannels, 16, format)) {
        std::cout << "[WASAPI Exclusive] Using 16-bit PCM at " 
                  << m_config.sampleRate << " Hz" << std::endl;
        return true;
    }
    
    // Try 24-bit PCM
    if (testExclusiveFormatPCM(m_config.sampleRate, m_config.numOutputChannels, 24, format)) {
        std::cout << "[WASAPI Exclusive] Using 24-bit PCM at " 
                  << m_config.sampleRate << " Hz" << std::endl;
        return true;
    }
    
    // Try 32-bit float
    if (testExclusiveFormat(m_config.sampleRate, m_config.numOutputChannels, format)) {
        std::cout << "[WASAPI Exclusive] Using 32-bit float at " 
                  << m_config.sampleRate << " Hz" << std::endl;
        return true;
    }

    // Try common sample rates with 16-bit PCM (most compatible)
    for (uint32_t sampleRate : EXCLUSIVE_SAMPLE_RATES) {
        if (testExclusiveFormatPCM(sampleRate, m_config.numOutputChannels, 16, format)) {
            std::cout << "[WASAPI Exclusive] Using fallback 16-bit PCM at " 
                      << sampleRate << " Hz" << std::endl;
            return true;
        }
    }
    
    // Try common sample rates with 32-bit float
    for (uint32_t sampleRate : EXCLUSIVE_SAMPLE_RATES) {
        if (testExclusiveFormat(sampleRate, m_config.numOutputChannels, format)) {
            std::cout << "[WASAPI Exclusive] Using fallback 32-bit float at " 
                      << sampleRate << " Hz" << std::endl;
            return true;
        }
    }

    return false;
}

bool WASAPIExclusiveDriver::testExclusiveFormat(uint32_t sampleRate, uint32_t channels, WAVEFORMATEX** format) {
    WAVEFORMATEX testFormat = {};
    testFormat.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
    testFormat.nChannels = static_cast<WORD>(channels);
    testFormat.nSamplesPerSec = sampleRate;
    testFormat.wBitsPerSample = 32;
    testFormat.nBlockAlign = (testFormat.nChannels * testFormat.wBitsPerSample) / 8;
    testFormat.nAvgBytesPerSec = testFormat.nSamplesPerSec * testFormat.nBlockAlign;
    testFormat.cbSize = 0;

    WAVEFORMATEX* closestMatch = nullptr;
    HRESULT hr = m_audioClient->IsFormatSupported(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        &testFormat,
        &closestMatch
    );

    if (hr == S_OK) {
        // Exact match - allocate and copy
        *format = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        memcpy(*format, &testFormat, sizeof(WAVEFORMATEX));
        return true;
    }

    if (closestMatch) {
        // Use closest match
        *format = closestMatch;
        return true;
    }

    return false;
}

bool WASAPIExclusiveDriver::testExclusiveFormatPCM(uint32_t sampleRate, uint32_t channels, uint32_t bitsPerSample, WAVEFORMATEX** format) {
    WAVEFORMATEX testFormat = {};
    testFormat.wFormatTag = WAVE_FORMAT_PCM;
    testFormat.nChannels = static_cast<WORD>(channels);
    testFormat.nSamplesPerSec = sampleRate;
    testFormat.wBitsPerSample = static_cast<WORD>(bitsPerSample);
    testFormat.nBlockAlign = (testFormat.nChannels * testFormat.wBitsPerSample) / 8;
    testFormat.nAvgBytesPerSec = testFormat.nSamplesPerSec * testFormat.nBlockAlign;
    testFormat.cbSize = 0;

    WAVEFORMATEX* closestMatch = nullptr;
    HRESULT hr = m_audioClient->IsFormatSupported(
        AUDCLNT_SHAREMODE_EXCLUSIVE,
        &testFormat,
        &closestMatch
    );

    if (hr == S_OK) {
        // Exact match - allocate and copy
        *format = (WAVEFORMATEX*)CoTaskMemAlloc(sizeof(WAVEFORMATEX));
        memcpy(*format, &testFormat, sizeof(WAVEFORMATEX));
        return true;
    }

    if (closestMatch) {
        // Use closest match
        *format = closestMatch;
        return true;
    }

    return false;
}

void WASAPIExclusiveDriver::closeStream() {
    closeDevice();
    m_state = DriverState::INITIALIZED;
    std::cout << "[WASAPI Exclusive] Stream closed" << std::endl;
}

bool WASAPIExclusiveDriver::startStream() {
    if (m_state != DriverState::STREAM_OPEN) {
        setError(DriverError::STREAM_START_FAILED, "Stream not open");
        return false;
    }

    // Pre-fill buffer with silence
    BYTE* data = nullptr;
    HRESULT hr = m_renderClient->GetBuffer(m_bufferFrameCount, &data);
    if (SUCCEEDED(hr)) {
        memset(data, 0, m_bufferFrameCount * m_waveFormat->nBlockAlign);
        m_renderClient->ReleaseBuffer(m_bufferFrameCount, 0);
    }

    m_shouldStop = false;
    m_isRunning = true;

    // Start audio client
    hr = m_audioClient->Start();
    if (FAILED(hr)) {
        setError(DriverError::STREAM_START_FAILED, "Failed to start audio client: " + HResultToString(hr));
        m_isRunning = false;
        return false;
    }

    // Start audio thread
    m_audioThread = std::thread(&WASAPIExclusiveDriver::audioThreadProc, this);

    m_state = DriverState::STREAM_RUNNING;
    std::cout << "[WASAPI Exclusive] Stream started" << std::endl;
    return true;
}

void WASAPIExclusiveDriver::stopStream() {
    if (!m_isRunning) {
        return;
    }

    m_shouldStop = true;
    
    if (m_audioEvent) {
        SetEvent(m_audioEvent);
    }

    if (m_audioThread.joinable()) {
        m_audioThread.join();
    }

    if (m_audioClient) {
        m_audioClient->Stop();
    }

    m_isRunning = false;
    m_state = DriverState::STREAM_OPEN;
    std::cout << "[WASAPI Exclusive] Stream stopped" << std::endl;
}

double WASAPIExclusiveDriver::getStreamLatency() const {
    if (!m_audioClient || !m_waveFormat) {
        return 0.0;
    }

    // In exclusive mode, latency is simply buffer size / sample rate
    return static_cast<double>(m_bufferFrameCount) / m_actualSampleRate;
}

void WASAPIExclusiveDriver::audioThreadProc() {
    if (!setThreadPriority()) {
        std::cerr << "[WASAPI Exclusive] Warning: Failed to set thread priority" << std::endl;
    }

    // Set MMCSS Pro Audio scheduling for real-time performance
    DWORD taskIndex = 0;
    HANDLE avrtHandle = AvSetMmThreadCharacteristicsA("Pro Audio", &taskIndex);
    if (!avrtHandle) {
        std::cerr << "[WASAPI Exclusive] Warning: Failed to set MMCSS" << std::endl;
    } else {
        // Set thread priority to critical for lowest possible latency
        BOOL prioritySuccess = AvSetMmThreadPriority(avrtHandle, AVRT_PRIORITY_CRITICAL);
        if (!prioritySuccess) {
            std::cerr << "[WASAPI Exclusive] Warning: Failed to set MMCSS priority to CRITICAL" << std::endl;
        } else {
            std::cout << "[WASAPI Exclusive] MMCSS enabled: Pro Audio @ CRITICAL priority" << std::endl;
        }
    }

    std::vector<float> userBuffer(m_bufferFrameCount * m_config.numOutputChannels);

    std::cout << "[WASAPI Exclusive] Audio thread running with " 
              << m_bufferFrameCount << " frames at " << m_actualSampleRate << " Hz" << std::endl;

    while (!m_shouldStop) {
        DWORD waitResult = WaitForSingleObject(m_audioEvent, 2000);
        
        if (waitResult != WAIT_OBJECT_0) {
            if (!m_shouldStop) {
                std::cerr << "[WASAPI Exclusive] Event timeout!" << std::endl;
                m_statistics.underrunCount++;
            }
            continue;
        }

        if (m_shouldStop) {
            break;
        }

        LARGE_INTEGER startTime;
        QueryPerformanceCounter(&startTime);

        // Get buffer
        BYTE* data = nullptr;
        HRESULT hr = m_renderClient->GetBuffer(m_bufferFrameCount, &data);
        if (FAILED(hr)) {
            std::cerr << "[WASAPI Exclusive] GetBuffer failed: " << HResultToString(hr) << std::endl;
            m_statistics.underrunCount++;
            continue;
        }

        // Call user callback
        double streamTime = static_cast<double>(m_statistics.callbackCount * m_bufferFrameCount) / m_actualSampleRate;
        
        if (m_userCallback) {
            m_userCallback(
                userBuffer.data(),
                nullptr,
                m_bufferFrameCount,
                streamTime,
                m_userData
            );
        } else {
            std::fill(userBuffer.begin(), userBuffer.end(), 0.0f);
        }

        // Convert and copy to WASAPI buffer based on format
        if (m_waveFormat->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) {
            // 32-bit float - direct copy
            memcpy(data, userBuffer.data(), m_bufferFrameCount * m_waveFormat->nBlockAlign);
        }
        else if (m_waveFormat->wFormatTag == WAVE_FORMAT_PCM) {
            // PCM format - need to convert floats to integers
            if (m_waveFormat->wBitsPerSample == 16) {
                // 16-bit PCM
                int16_t* pcmData = reinterpret_cast<int16_t*>(data);
                for (uint32_t i = 0; i < m_bufferFrameCount * m_config.numOutputChannels; ++i) {
                    // Convert float (-1.0 to 1.0) to int16_t (-32768 to 32767)
                    float sample = userBuffer[i];
                    // Clamp to valid range
                    if (sample > 1.0f) sample = 1.0f;
                    if (sample < -1.0f) sample = -1.0f;
                    pcmData[i] = static_cast<int16_t>(sample * 32767.0f);
                }
            }
            else if (m_waveFormat->wBitsPerSample == 24) {
                // 24-bit PCM (stored in 32-bit containers typically)
                uint8_t* pcmData = data;
                for (uint32_t i = 0; i < m_bufferFrameCount * m_config.numOutputChannels; ++i) {
                    float sample = userBuffer[i];
                    if (sample > 1.0f) sample = 1.0f;
                    if (sample < -1.0f) sample = -1.0f;
                    // Convert to 24-bit (stored in 3 bytes)
                    int32_t pcmValue = static_cast<int32_t>(sample * 8388607.0f); // 2^23 - 1
                    // Write 3 bytes (little-endian)
                    pcmData[i * 3 + 0] = static_cast<uint8_t>(pcmValue & 0xFF);
                    pcmData[i * 3 + 1] = static_cast<uint8_t>((pcmValue >> 8) & 0xFF);
                    pcmData[i * 3 + 2] = static_cast<uint8_t>((pcmValue >> 16) & 0xFF);
                }
            }
            else {
                // Unknown bit depth - zero the buffer
                memset(data, 0, m_bufferFrameCount * m_waveFormat->nBlockAlign);
            }
        }
        else {
            // Unknown format - zero the buffer
            memset(data, 0, m_bufferFrameCount * m_waveFormat->nBlockAlign);
        }

        // Release buffer
        hr = m_renderClient->ReleaseBuffer(m_bufferFrameCount, 0);
        if (FAILED(hr)) {
            std::cerr << "[WASAPI Exclusive] ReleaseBuffer failed" << std::endl;
        }

        // Update statistics
        LARGE_INTEGER endTime;
        QueryPerformanceCounter(&endTime);
        double callbackTimeUs = static_cast<double>(endTime.QuadPart - startTime.QuadPart) * 1000000.0 / m_perfFreq.QuadPart;
        updateStatistics(callbackTimeUs);
    }

    if (avrtHandle) {
        AvRevertMmThreadCharacteristics(avrtHandle);
    }

    std::cout << "[WASAPI Exclusive] Audio thread exiting" << std::endl;
}

bool WASAPIExclusiveDriver::setThreadPriority() {
    return SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
}

void WASAPIExclusiveDriver::setError(DriverError error, const std::string& message) {
    m_lastError = error;
    m_errorMessage = message;
    m_state = DriverState::DRIVER_ERROR;

    std::cerr << "[WASAPI Exclusive] Error: " << message << std::endl;

    if (m_errorCallback) {
        m_errorCallback(error, message);
    }
}

void WASAPIExclusiveDriver::updateStatistics(double callbackTimeUs) {
    m_statistics.callbackCount++;
    
    const double alpha = 0.1;
    m_statistics.averageCallbackTimeUs = 
        alpha * callbackTimeUs + (1.0 - alpha) * m_statistics.averageCallbackTimeUs;
    
    if (callbackTimeUs > m_statistics.maxCallbackTimeUs) {
        m_statistics.maxCallbackTimeUs = callbackTimeUs;
    }

    if (m_waveFormat && m_bufferFrameCount > 0) {
        double bufferDurationUs = static_cast<double>(m_bufferFrameCount) * 1000000.0 / m_actualSampleRate;
        m_statistics.cpuLoadPercent = (callbackTimeUs / bufferDurationUs) * 100.0;
    }

    m_statistics.actualLatencyMs = getStreamLatency() * 1000.0;
}

} // namespace Audio
} // namespace Nomad
