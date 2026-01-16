// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "Plugin/VST3Host.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/base/funknown.h"
#include <iostream>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

// Mock Processor that crashes
class CrasherProcessor : public IAudioProcessor {
public:
    // FUnknown
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    // IAudioProcessor
    tresult PLUGIN_API setBusArrangements(SpeakerArrangement*, int32, SpeakerArrangement*, int32) override { return kResultOk; }
    tresult PLUGIN_API getBusArrangement(BusDirection, int32, SpeakerArrangement&) override { return kResultOk; }
    tresult PLUGIN_API canProcessSampleSize(int32) override { return kResultOk; }
    uint32 PLUGIN_API getLatencySamples() override { return 0; }
    tresult PLUGIN_API setupProcessing(ProcessSetup&) override { return kResultOk; }
    tresult PLUGIN_API setProcessing(TBool) override { return kResultOk; }
    
    // THE KEY METHOD
    tresult PLUGIN_API process(ProcessData& data) override {
        // Trigger Access Violation (Null Pointer Dereference)
        volatile int* badPointer = nullptr;
        *badPointer = 42; 
        
        return kResultOk; 
    }
    
    uint32 PLUGIN_API getTailSamples() override { return 0; }
};

int main() {
    std::cout << "Starting Crash Test..." << std::endl;

#ifndef _WIN32
    std::cout << "Skipping SEH crash test on non-Windows platform." << std::endl;
    return 0;
#endif

    Aestra::Audio::VST3PluginInstance instance;
    CrasherProcessor mockProc;
    
    // Inject Mock
    Aestra::Audio::VST3HostTestAccess::setProcessor(instance, &mockProc);
    
    // Prepare dummy buffers
    std::vector<float> buffer(512, 0.0f);
    float* channelData = buffer.data();
    float* inputs[2] = {channelData, channelData};
    float* outputs[2] = {channelData, channelData};
    
    std::cout << "Invoking process() - expecting crash capture..." << std::endl;
    
    // This call SHOULD NOT crash the process. It should be caught by SEH.
    instance.process(inputs, outputs, 2, 2, 512);
    
    if (instance.isCrashed()) {
        std::cout << "SUCCESS: Crash was caught! Plugin marked as crashed." << std::endl;
        
        // Verify output is silenced logic by checking if subsequent calls are safe
        // (calling process again should return immediately and output silence)
         instance.process(inputs, outputs, 2, 2, 512);
         std::cout << "SUCCESS: Subsequent call was safe." << std::endl;
         
        // Cleanup: set processor to null
        Aestra::Audio::VST3HostTestAccess::setProcessor(instance, nullptr);
        return 0;
    } else {
        std::cout << "FAILURE: Crash was NOT caught or not reported!" << std::endl;
        // Cleanup
        Aestra::Audio::VST3HostTestAccess::setProcessor(instance, nullptr);
        return 1;
    }
}
