#include "WASAPIExclusiveDriver.h"

// ... existing includes ...

std::vector<uint32_t> WASAPIExclusiveDriver::getSupportedExclusiveSampleRates(uint32_t deviceIndex) {
    std::vector<uint32_t> supportedRates;
    std::vector<uint32_t> testRates = { 44100, 48000, 88200, 96000, 176400, 192000 };
    
    IMMDeviceEnumerator* enumerator = nullptr;
    IMMDeviceCollection* collection = nullptr;
    IMMDevice* device = nullptr;
    IAudioClient* client = nullptr;

    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&enumerator);
    if (FAILED(hr)) return supportedRates;

    hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &collection);
    if (SUCCEEDED(hr)) {
        hr = collection->Item(deviceIndex, &device);
        if (SUCCEEDED(hr)) {
             hr = device->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr, (void**)&client);
             if (SUCCEEDED(hr)) {
                 for (uint32_t rate : testRates) {
                     WAVEFORMATEX* match = nullptr;
                     WAVEFORMATEX format = {};
                     format.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
                     format.nChannels = 2;
                     format.nSamplesPerSec = rate;
                     format.wBitsPerSample = 32;
                     format.nBlockAlign = (format.nChannels * format.wBitsPerSample) / 8;
                     format.nAvgBytesPerSec = format.nSamplesPerSec * format.nBlockAlign;
                     format.cbSize = 0;

                     hr = client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &format, &match);
                     if (hr == S_OK) {
                         supportedRates.push_back(rate);
                     }
                     if (match) CoTaskMemFree(match);
                 }
                 client->Release();
             }
             device->Release();
        }
        collection->Release();
    }
    enumerator->Release();

    // If no specific exclusive rates found (rare), fallback to 48k at least to avoid empty list
    if (supportedRates.empty()) {
        supportedRates.push_back(48000);
    }
    
    return supportedRates;
}
