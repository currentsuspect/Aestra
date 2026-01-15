// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
// Performance benchmarking suite for NomadAudio.

#include "AudioEngine.h"
#include "AudioGraph.h"
#include "Filter.h"
#include "SamplePool.h"

#include <atomic>
#include <chrono>
#include <cmath>
#include <vector>
#include <iostream>
#include <thread>
#include <iomanip>
#include <random>

using namespace Nomad::Audio;

#ifdef _WIN32
#include <windows.h>
#include <avrt.h>
#endif

void setHighPriority() {
#ifdef _WIN32
    DWORD taskIndex = 0;
    HANDLE hTask = AvSetMmThreadCharacteristics(TEXT("Pro Audio"), &taskIndex);
    if (!hTask) {
        hTask = AvSetMmThreadCharacteristics(TEXT("Audio"), &taskIndex);
    }
    if (hTask) {
        // Successful MMCSS registration
        // We can also boost the thread priority further if needed, but MMCSS is usually sufficient.
        // For benchmarks, let's be aggressive.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    } else {
        std::cerr << "Warning: Failed to enable MMCSS (Pro Audio). Error: " << GetLastError() << std::endl;
        // Fallback
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);
    }
#endif
}

// =================================================================================================
// Helpers
// =================================================================================================

std::shared_ptr<AudioBuffer> makeSilenceBuffer(uint32_t sampleRate, uint32_t samples) {
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->numFrames = samples;
    buffer->data.resize(static_cast<size_t>(samples) * 2, 0.0f);
    buffer->ready.store(true);
    return buffer;
}

std::shared_ptr<AudioBuffer> makeSineBuffer(uint32_t sampleRate, uint32_t samples) {
    auto buffer = std::make_shared<AudioBuffer>();
    buffer->channels = 2;
    buffer->sampleRate = sampleRate;
    buffer->numFrames = samples;
    buffer->data.resize(static_cast<size_t>(samples) * 2);
    
    constexpr double kSpl = 3.14159 * 2.0 * 440.0 / 48000.0;
    for(size_t i=0; i<samples; ++i) {
        float v = std::sin(static_cast<double>(i) * kSpl) * 0.1f;
        buffer->data[i*2] = v;
        buffer->data[i*2+1] = v;
    }
    buffer->ready.store(true);
    return buffer;
}

// =================================================================================================
// Benchmark 1: Polyphony (Mixing Throughput)
// =================================================================================================
void runPolyphonyBenchmark() {
    std::cout << "\n[Running Polyphony Benchmark]..." << std::endl;
    
    AudioEngine engine;
    setHighPriority();
    engine.setSampleRate(48000);
    const uint32_t kBlockSize = 256;
    engine.setBufferConfig(kBlockSize, 2);
    
    // Create a 10-second source buffer
    auto source = makeSineBuffer(48000, 48000 * 10);
    
    // We will ramp up tracks until we miss deadlines
    int trackCount = 0;
    const int kStep = 32;
    const int kMaxTracks = 4096; // Safety limit
    
    std::vector<float> output(kBlockSize * 2);
    bool failed = false;
    
    // Baseline timing
    auto startParams = std::chrono::high_resolution_clock::now();
    
    std::cout << "  Tracks | Avg Time (us) | Load % | Status" << std::endl;
    std::cout << "  -------|---------------|--------|-------" << std::endl;

    for (trackCount = 32; trackCount <= kMaxTracks; trackCount += kStep) {
        // Build graph
        AudioGraph graph;
        graph.timelineEndSample = source->numFrames;
        
        for(int i=0; i<trackCount; ++i) {
            TrackRenderState tr;
            tr.trackId = i + 1;
            tr.trackIndex = i;
            
            ClipRenderState clip;
            clip.buffer = source;
            clip.audioData = source->data.data();
            clip.totalFrames = source->numFrames;
            clip.sourceSampleRate = 48000;
            clip.endSample = source->numFrames;
            
            tr.clips.push_back(clip);
            graph.tracks.push_back(std::move(tr));
        }
        
        engine.setGraph(graph);
        engine.setTransportPlaying(true);
        
        // Warm up
        engine.processBlock(output.data(), nullptr, kBlockSize, 0.0);
        
        // Measure 100 blocks
        auto t0 = std::chrono::high_resolution_clock::now();
        double checksum = 0.0;
        for(int b=0; b<100; ++b) {
            engine.processBlock(output.data(), nullptr, kBlockSize, 0.0);
            // Defeat optimizer by touching data
            checksum += output[0] + output[kBlockSize/2] + output[kBlockSize-1];
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        
        // Use checksum so it's not discarded
        if (std::abs(checksum) > 1.0e15) std::cout << "!" << std::endl;
        
        double totalUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        double avgUs = totalUs / 100.0;
        
        // Budget for 256 frames @ 48kHz is ~5333us
        double budgetUs = (kBlockSize / 48000.0) * 1000000.0;
        double load = (avgUs / budgetUs) * 100.0;
        
        std::cout << "  " << std::setw(6) << trackCount << " | " 
                  << std::setw(13) << avgUs << " | " 
                  << std::setw(5) << std::fixed << std::setprecision(1) << load << "% | "
                  << (load > 90.0 ? "FAIL" : "OK") << std::endl;
                  
        if (load > 90.0) {
            std::cout << "-> Max Safe Polyphony: ~" << (trackCount - kStep) << " tracks" << std::endl;
            failed = true;
            break;
        }
    }
    
    if (!failed) std::cout << "-> Amazing! Exceeded " << kMaxTracks << " tracks." << std::endl;
}

// =================================================================================================
// Benchmark 2: DSP Stack (Filter Density)
// =================================================================================================
void runDSPBenchmark() {
    std::cout << "\n[Running DSP Density Benchmark]..." << std::endl;
    setHighPriority();
    std::cout << "  Simulating 2x Oversampled Ladder Filters per track" << std::endl;
    
    const int kBlockSize = 128; // Tighter deadline for DSP test
    const int kSampleRate = 48000;
    
    // Create filters
    std::vector<std::unique_ptr<Nomad::Audio::DSP::Filter>> filters;
    
    // Input/Output buffers
    std::vector<float> buffer(kBlockSize); // Mono processing for density
    for(auto& s : buffer) s = 0.5f; // DC offset to avoid denormals
    
    auto addFilters = [&](int count) {
        for(int i=0; i<count; ++i) {
            auto f = std::make_unique<Nomad::Audio::DSP::Filter>(static_cast<float>(kSampleRate));
            f->setType(Nomad::Audio::DSP::FilterType::LowPass);
            f->setOversampling(Nomad::Audio::DSP::OversamplingFactor::TwoX);
            f->setCutoff(1000.0f);
            filters.push_back(std::move(f));
        }
    };
    
    std::cout << "  Filters | Avg Time (us) | Load % | Status" << std::endl;
    std::cout << "  --------|---------------|--------|-------" << std::endl;
    
    double budgetUs = (kBlockSize / 48000.0) * 1000000.0;
    
    for (int count = 16; count <= 1024; count += 16) {
        // Add filters up to current count
        addFilters(count - static_cast<int>(filters.size()));
        
        // Measure 100 blocks
        auto t0 = std::chrono::high_resolution_clock::now();
        for(int b=0; b<100; ++b) {
            for(auto& f : filters) {
                f->processBlock(buffer.data(), kBlockSize);
            }
        }
        auto t1 = std::chrono::high_resolution_clock::now();
        
        double totalUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        double avgUs = totalUs / 100.0;
        double load = (avgUs / budgetUs) * 100.0;
        
        std::cout << "  " << std::setw(7) << count << " | " 
                  << std::setw(13) << avgUs << " | " 
                  << std::setw(5) << std::fixed << std::setprecision(1) << load << "% | "
                  << (load > 90.0 ? "FAIL" : "OK") << std::endl;
                  
        if (load > 90.0) {
            std::cout << "-> Max Real-time Filters: ~" << (count - 16) << " instances" << std::endl;
            break;
        }
    }
}

// =================================================================================================
// Benchmark 3: Buffer Thrashing (Jitter Stability)
// =================================================================================================
void runJitterBenchmark() {
    std::cout << "\n[Running Low-Latency Jitter Benchmark]..." << std::endl;
    setHighPriority(); // Critical for jitter test
    
    AudioEngine engine;
    engine.setSampleRate(48000);
    // Extreme settings: 32 frames buffer
    const uint32_t kBlockSize = 32; 
    engine.setBufferConfig(kBlockSize, 2);
    
    auto source = makeSineBuffer(48000, 48000 * 5);
    AudioGraph graph;
    graph.timelineEndSample = 48000 * 5;
    
    // Add 16 heavy tracks (moderate load)
    for(int i=0; i<16; ++i) {
        TrackRenderState tr;
        tr.trackId = i + 1;
        tr.trackIndex = i;
        ClipRenderState clip;
        clip.buffer = source;
        clip.audioData = source->data.data();
        clip.totalFrames = source->numFrames;
        clip.sourceSampleRate = 48000;
        clip.endSample = source->numFrames;
        tr.clips.push_back(clip);
        graph.tracks.push_back(std::move(tr));
    }
    
    engine.setGraph(graph);
    engine.setTransportPlaying(true);
    
    std::vector<float> output(kBlockSize * 2);
    
    // Run for 5 seconds simulated, check max jitter
    const int kTotalBlocks = (48000 * 5) / kBlockSize;
    
    double maxTimeUs = 0.0;
    double minTimeUs = 999999.0;
    double sumTimeUs = 0.0;
    
    for(int i=0; i<kTotalBlocks; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        engine.processBlock(output.data(), nullptr, kBlockSize, 0.0);
        auto t1 = std::chrono::high_resolution_clock::now();
        
        double us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();
        if (us > maxTimeUs) maxTimeUs = us;
        if (us < minTimeUs) minTimeUs = us;
        sumTimeUs += us;
    }
    
    double avgUs = sumTimeUs / kTotalBlocks;
    double budgetUs = (kBlockSize / 48000.0) * 1000000.0; // ~666us
    
    std::cout << "  Buffer: " << kBlockSize << " frames (" << budgetUs << " us budget)" << std::endl;
    std::cout << "  Avg Time: " << avgUs << " us" << std::endl;
    std::cout << "  Min Time: " << minTimeUs << " us" << std::endl;
    std::cout << "  Max Time: " << maxTimeUs << " us" << std::endl;
    std::cout << "  Jitter Range: " << (maxTimeUs - minTimeUs) << " us" << std::endl;
    
    if (maxTimeUs < budgetUs * 0.8) {
        std::cout << "-> Stability: ROCK SOLID (Max load " << (maxTimeUs/budgetUs)*100 << "%)" << std::endl;
    } else if (maxTimeUs < budgetUs) {
        std::cout << "-> Stability: MARGINAL (Max load " << (maxTimeUs/budgetUs)*100 << "%)" << std::endl;
    } else {
        std::cout << "-> Stability: FAILED (XRun Detected)" << std::endl;
    }
}

int main(int argc, char** argv) {
    std::cout << "========================================" << std::endl;
    std::cout << " NOMAD AUDIO PRO PERFORMANCE SUITE v1.0" << std::endl;
    std::cout << "========================================" << std::endl;
    
    runPolyphonyBenchmark();
    runDSPBenchmark();
    runJitterBenchmark();
    
    return 0;
}
