// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// AudioEngineDiagnosticsTest.cpp — Tests for diagnostic structs and RT guard

#include "Core/AudioEngineDiagnostics.h"
#include "Core/RTGuard.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace Aestra::Audio;

static void testDiagnosticEpochDefaults() {
    DiagnosticEpoch epoch;
    assert(epoch.callbackId == 0);
    assert(epoch.graphGeneration == 0);
    assert(epoch.transportGeneration == 0);
    assert(epoch.lastTransportEvent == TransportEventType::None);
    printf("[PASS] DiagnosticEpoch defaults\n");
}

static void testDenormalEventInfoDefaults() {
    DenormalEventInfo info;
    assert(info.pluginId == 0);
    assert(info.timestamp == 0);
    assert(info.severity == 0);
    printf("[PASS] DenormalEventInfo defaults\n");
}

static void testAudioEngineDiagnosticsDefaults() {
    AudioEngineDiagnostics diag;
    assert(diag.schedulerJitterMs == 0.0);
    assert(diag.dispatchJitterMs == 0.0);
    assert(diag.callbackDurationMs == 0.0);
    assert(diag.xruns == 0);
    assert(diag.rtAllocations == 0);
    assert(diag.cpuLoad == 0.0);
    assert(diag.denormalEvents == 0);
    printf("[PASS] AudioEngineDiagnostics defaults\n");
}

static void testCoherentSnapshot() {
    CoherentDiagnosticsSnapshot snap;
    assert(snap.generation == 0);
    assert(snap.diagnostics.xruns == 0);
    printf("[PASS] CoherentDiagnosticsSnapshot defaults\n");
}

static void testRTViolationEventDefaults() {
    RTViolationEvent event;
    assert(event.timestamp == 0);
    assert(event.returnAddress == nullptr);
    assert(event.allocationSize == 0);
    assert(event.threadId == 0);
    assert(event.type == RTViolationType::Allocation);
    printf("[PASS] RTViolationEvent defaults\n");
}

static void testThreadLocalRTAudit() {
    // Reset audit data
    g_rtAuditData = {};
    assert(g_rtAuditData.violationCount == 0);
    assert(g_rtAuditData.droppedCount == 0);
    assert(g_rtAuditData.eventIndex == 0);
    printf("[PASS] ThreadLocalRTAudit defaults\n");
}

static void testRTViolationRecording() {
    g_rtAuditData = {};

    // Record some violations
    recordRTViolation(RTViolationType::Allocation, (void*)0x1234, 1024);
    recordRTViolation(RTViolationType::MutexAcquire, (void*)0x5678, 0);
    recordRTViolation(RTViolationType::BlockingCall, (void*)0x9ABC, 0);

    assert(g_rtAuditData.violationCount == 3);
    assert(g_rtAuditData.eventIndex == 3);
    assert(g_rtAuditData.lastEvents[0].type == RTViolationType::Allocation);
    assert(g_rtAuditData.lastEvents[0].returnAddress == (void*)0x1234);
    assert(g_rtAuditData.lastEvents[0].allocationSize == 1024);
    assert(g_rtAuditData.lastEvents[1].type == RTViolationType::MutexAcquire);
    assert(g_rtAuditData.lastEvents[2].type == RTViolationType::BlockingCall);
    printf("[PASS] RT violation recording\n");
}

static void testCircularBufferOverflow() {
    g_rtAuditData = {};

    // Fill beyond MAX_LOCAL_VIOLATIONS
    for (size_t i = 0; i < MAX_LOCAL_VIOLATIONS + 10; i++) {
        recordRTViolation(RTViolationType::Allocation, nullptr, i);
    }

    assert(g_rtAuditData.violationCount == MAX_LOCAL_VIOLATIONS + 10);
    assert(g_rtAuditData.droppedCount == 10);
    printf("[PASS] Circular buffer overflow handling\n");
}

static void testRTDepthTracking() {
    g_realtimeAudioThreadDepth = 0;
    assert(!isRealtimeAudioThread());

    {
        ScopedRealtimeAudioThread guard;
        assert(isRealtimeAudioThread());
        assert(g_realtimeAudioThreadDepth == 1);

        {
            ScopedRealtimeAudioThread innerGuard;
            assert(g_realtimeAudioThreadDepth == 2);
        }
        assert(g_realtimeAudioThreadDepth == 1);
    }
    assert(g_realtimeAudioThreadDepth == 0);
    assert(!isRealtimeAudioThread());
    printf("[PASS] RT depth tracking with nested guards\n");
}

static void testThreadLocalIsolation() {
    g_rtAuditData = {};
    g_realtimeAudioThreadDepth = 0;

    // Record violations on main thread
    recordRTViolation(RTViolationType::Allocation, (void*)0x1111, 100);
    assert(g_rtAuditData.violationCount == 1);

    // Check isolation on another thread
    std::thread other([] {
        assert(g_rtAuditData.violationCount == 0);
        assert(g_realtimeAudioThreadDepth == 0);
        recordRTViolation(RTViolationType::Deallocation, (void*)0x2222, 200);
        assert(g_rtAuditData.violationCount == 1);
    });
    other.join();

    // Main thread should still have 1
    assert(g_rtAuditData.violationCount == 1);
    printf("[PASS] Thread-local isolation\n");
}

static void testTransportEventTypeValues() {
    assert(static_cast<uint32_t>(TransportEventType::None) == 0);
    assert(static_cast<uint32_t>(TransportEventType::Seek) == 1);
    assert(static_cast<uint32_t>(TransportEventType::LoopWrap) == 2);
    assert(static_cast<uint32_t>(TransportEventType::StopToPlay) == 3);
    assert(static_cast<uint32_t>(TransportEventType::TempoMapRebuild) == 4);
    assert(static_cast<uint32_t>(TransportEventType::PrerollStart) == 5);
    printf("[PASS] TransportEventType values\n");
}

int main() {
    printf("=== AudioEngineDiagnostics Tests ===\n");
    testDiagnosticEpochDefaults();
    testDenormalEventInfoDefaults();
    testAudioEngineDiagnosticsDefaults();
    testCoherentSnapshot();
    testRTViolationEventDefaults();
    testThreadLocalRTAudit();
    testRTViolationRecording();
    testCircularBufferOverflow();
    testRTDepthTracking();
    testThreadLocalIsolation();
    testTransportEventTypeValues();
    printf("\nAll tests passed.\n");
    return 0;
}
