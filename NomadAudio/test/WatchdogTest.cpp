// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/VST3Host.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/base/funknown.h"
#include <thread>
#include <iostream>
#include <chrono>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

// Mock Processor that sleeps to simulate hangs
class MockProcessor : public IAudioProcessor {
public:
    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; } // No-op, allocated on stack

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32, SpeakerArrangement*, int32) override { return kResultOk; }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement&) override { return kResultOk; }
    tresult PLUGIN_API canProcessSampleSize(int32) override { return kResultOk; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup&) override { return kResultOk; }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    
    // THE KEY METHOD
    tresult PLUGIN_API process(ProcessData& data) override {
        // Sleep for 15ms (budget for 512 frames @ 48kHz is ~10.6ms)
        // This ensures every call is a violation.
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        return kResultOk; 
    }
    
    uint32 PLUGIN_API getTailSamples() override { return 0; }
};

int main() {
    std::cout << "Starting Watchdog Test..." << std::endl;

    Nomad::Audio::VST3PluginInstance instance;
    MockProcessor mockProc;
    
    // Inject Mock
    Nomad::Audio::VST3HostTestAccess::setProcessor(instance, &mockProc);
    
    // Prepare dummy buffers
    std::vector<float> buffer(512, 0.0f);
    float* channelData = buffer.data();
    float* inputs[2] = {channelData, channelData};
    float* outputs[2] = {channelData, channelData};
    
    std::cout << "Running blocks... Expect bypass after 50 violations." << std::endl;
    
    bool bypassed = false;
    for(int i=0; i<60; ++i) { // Run 60 blocks, limit is 50
        auto start = std::chrono::high_resolution_clock::now();
        
        instance.process(inputs, outputs, 2, 2, 512);
        
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        
        // std::cout << "Block " << i << ": " << ms << "ms" << std::endl;

        if (instance.isBypassedByWatchdog()) {
            std::cout << "SUCCESS: Plugin bypassed at block " << i << std::endl;
            bypassed = true;
            break;
        }
    }
    
    // Verify silence functionality (subsequent calls should be fast and return silence)
    if (bypassed) {
         auto start = std::chrono::high_resolution_clock::now();
         instance.process(inputs, outputs, 2, 2, 512);
         auto end = std::chrono::high_resolution_clock::now();
         double ms = std::chrono::duration<double, std::milli>(end - start).count();
         
         if (ms < 1.0) {
             std::cout << "SUCCESS: Bypassed call was fast (" << ms << "ms)" << std::endl;
         } else {
             std::cout << "FAILURE: Bypassed call was slow!" << std::endl;
             return 1;
         }
    } else {
        std::cout << "FAILURE: Plugin was NOT bypassed." << std::endl;
        return 1;
    }
    
    // Cleanup: set processor to null so destructor doesn't try to release our stack object
    Nomad::Audio::VST3HostTestAccess::setProcessor(instance, nullptr);
    
    return 0;
}
