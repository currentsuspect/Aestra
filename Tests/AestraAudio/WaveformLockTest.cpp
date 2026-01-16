// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "WaveformCache.h"
#include "AestraLog.h"
#include <iostream>
#include <thread>
#include <chrono>
#include <vector>
#include <atomic>
#include <numeric>
#include <cmath>

using namespace Aestra::Audio;

// Benchmark for Reader Latency during concurrent Writes
int main() {
    std::cout << "Starting Waveform Lock Test..." << std::endl;

    WaveformCache cache;
    
    // Create large dummy audio buffer (10 seconds @ 48kHz stereo)
    SampleIndex numFrames = 48000 * 10;
    uint32_t numChannels = 2;
    std::vector<float> audioData(numFrames * numChannels);
    
    // Fill with sine wave
    for (size_t i = 0; i < audioData.size(); ++i) {
        audioData[i] = std::sin(i * 0.01f);
    }
    
    std::atomic<bool> running{true};
    std::atomic<bool> writerActive{false};
    
    // 1. Writer Thread: Continuously rebuilds cache
    std::thread writer([&]() {
        writerActive = true;
        int count = 0;
        while (running) {
            // Build cache (this takes time)
            cache.buildFromRaw(audioData.data(), numFrames, numChannels);
            count++;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "Writer completed " << count << " builds." << std::endl;
        writerActive = false;
    });

    // Wait for at least one build
    while (!cache.isReady()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // 2. Reader Thread: Measures latency of getPeaksForRange
    std::vector<double> latencies;
    latencies.reserve(10000);
    
    auto startBench = std::chrono::high_resolution_clock::now();
    
    int readCount = 0;
    std::vector<WaveformPeak> peaks;
    
    while (readCount < 1000) { // Perform 1000 reads
        auto start = std::chrono::high_resolution_clock::now();
        
        // Simulate UI draw call
        cache.getPeaksForRange(0, 0, numFrames, 1000, peaks);
        
        auto end = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(end - start).count();
        latencies.push_back(ms);
        
        readCount++;
        std::this_thread::sleep_for(std::chrono::milliseconds(1)); // Simulate frame interval
    }
    
    running = false; // Stop writer
    writer.join();
    
    // Analyze results
    double maxLatency = 0.0;
    double totalLatency = 0.0;
    
    for (double lat : latencies) {
        if (lat > maxLatency) maxLatency = lat;
        totalLatency += lat;
    }
    double avgLatency = totalLatency / latencies.size();
    
    std::cout << "Reader Stats:" << std::endl;
    std::cout << "  Avg Latency: " << avgLatency << " ms" << std::endl;
    std::cout << "  Max Latency: " << maxLatency << " ms" << std::endl;
    
    if (maxLatency > 5.0) { // Threshold: 5ms (relaxed for OS jitter)
        std::cout << "FAILURE: Max latency exceeded 5ms! (UI would stutter)" << std::endl;
        return 1;
    }
    
    std::cout << "SUCCESS: Reader latency is low (Lock-free-ish behavior confirmed)." << std::endl;
    return 0;
}
