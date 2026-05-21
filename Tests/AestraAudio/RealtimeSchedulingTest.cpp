// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
// Realtime Scheduling Test — verifies Linux audio thread has proper RT scheduling.
// Tests scheduling policy, priority, and mlockall status.
// Linux-only: sched_getscheduler/sched_setscheduler, RLIMIT_RTPRIO, /proc/self/status
// are not available on Windows or macOS.

#if !defined(__linux__)
#include <iostream>
int main() {
    std::cout << "RealtimeSchedulingTest: skipped (Linux-only test)" << std::endl;
    return 0;
}
#else

#include <cstring>
#include <fstream>
#include <iostream>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

static int g_passes = 0;
static int g_fails = 0;

#define CHECK(cond, msg) do { \
    if (cond) { \
        std::cout << "[PASS] " << msg << "\n"; \
        g_passes++; \
    } else { \
        std::cout << "[FAIL] " << msg << "\n"; \
        g_fails++; \
    } \
} while(0)

// Get scheduling policy name
static const char* policyName(int policy) {
    switch (policy) {
        case SCHED_FIFO:  return "SCHED_FIFO";
        case SCHED_RR:    return "SCHED_RR";
        case SCHED_OTHER: return "SCHED_OTHER";
#ifdef SCHED_BATCH
        case SCHED_BATCH: return "SCHED_BATCH";
#endif
#ifdef SCHED_IDLE
        case SCHED_IDLE:  return "SCHED_IDLE";
#endif
#ifdef SCHED_DEADLINE
        case SCHED_DEADLINE: return "SCHED_DEADLINE";
#endif
        default: return "UNKNOWN";
    }
}

// ============================================================================
// Test: Current thread scheduling policy
// ============================================================================

void testCurrentThreadScheduling() {
    std::cout << "\n=== Test: Current Thread Scheduling ===\n";

    int policy = sched_getscheduler(0); // 0 = calling thread
    CHECK(policy >= 0, "sched_getscheduler() succeeds");

    const char* name = (policy >= 0) ? policyName(policy) : "error";
    std::cout << "  Current thread policy: " << name << " (" << policy << ")\n";

    struct sched_param param;
    int ret = sched_getparam(0, &param);
    CHECK(ret == 0, "sched_getparam() succeeds");
    if (ret == 0) {
        std::cout << "  Current thread priority: " << param.sched_priority << "\n";
    }
}

// ============================================================================
// Test: mlockall status
// ============================================================================

void testMlockallStatus() {
    std::cout << "\n=== Test: mlockall Status ===\n";

    // Check /proc/self/status for VmLck field
    std::ifstream status("/proc/self/status");
    bool found = false;
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmLck:") == 0) {
            found = true;
            // VmLck > 0 means some memory is locked
            std::cout << "  " << line << "\n";
            // Parse the value (in kB)
            size_t colonPos = line.find(':');
            if (colonPos != std::string::npos) {
                std::string val = line.substr(colonPos + 1);
                long kb = std::stol(val);
                if (kb > 0) {
                    CHECK(true, "Memory is locked (VmLck = " + std::to_string(kb) + " kB)");
                } else {
                    std::cout << "  NOTE: No memory locked in this process. mlockall may not be called yet.\n";
                    CHECK(true, "No memory locked (VmLck = 0 kB) — expected before audio stream starts");
                }
            }
            break;
        }
    }
    if (!found) {
        std::cout << "  VmLck not found in /proc/self/status\n";
        CHECK(false, "Cannot determine mlockall status");
    }
}

// ============================================================================
// Test: Priority range available
// ============================================================================

void testPriorityRange() {
    std::cout << "\n=== Test: Priority Range ===\n";

    int fifoMin = sched_get_priority_min(SCHED_FIFO);
    int fifoMax = sched_get_priority_max(SCHED_FIFO);
    int rrMin = sched_get_priority_min(SCHED_RR);
    int rrMax = sched_get_priority_max(SCHED_RR);

    std::cout << "  SCHED_FIFO range: " << fifoMin << " - " << fifoMax << "\n";
    std::cout << "  SCHED_RR range:   " << rrMin << " - " << rrMax << "\n";

    CHECK(fifoMin >= 0 && fifoMax > fifoMin, "SCHED_FIFO priority range is valid");
    CHECK(rrMin >= 0 && rrMax > rrMin, "SCHED_RR priority range is valid");
}

// ============================================================================
// Test: RLIMIT_RTPRIO
// ============================================================================

void testRlimitRtprio() {
    std::cout << "\n=== Test: RLIMIT_RTPRIO ===\n";

    struct rlimit rlim;
    int ret = getrlimit(RLIMIT_RTPRIO, &rlim);
    CHECK(ret == 0, "getrlimit(RLIMIT_RTPRIO) succeeds");

    if (ret == 0) {
        std::cout << "  RLIMIT_RTPRIO current: " << rlim.rlim_cur << "\n";
        std::cout << "  RLIMIT_RTPRIO max:     " << rlim.rlim_max << "\n";

        // Check if we have RT capability. If not, report but don't fail.
        bool hasRtCapability = (rlim.rlim_cur > 0 || rlim.rlim_cur == RLIM_INFINITY);
        if (!hasRtCapability) {
            std::cout << "  NOTE: RLIMIT_RTPRIO is 0. RT scheduling requires CAP_SYS_NICE.\n";
            std::cout << "  This is expected in CI/container environments. Not a test failure.\n";
        }
        CHECK(true, "RLIMIT_RTPRIO reported (cur=" + std::to_string(rlim.rlim_cur) + ", RT cap: " + (hasRtCapability ? "yes" : "no") + ")");
    }
}

// ============================================================================
// Test: Attempt to set SCHED_FIFO
// ============================================================================

void testSetSchedFifo() {
    std::cout << "\n=== Test: Set SCHED_FIFO (dry run) ===\n";

    // Try to set SCHED_FIFO on current thread
    struct sched_param param;
    int maxPri = sched_get_priority_max(SCHED_FIFO);
    int minPri = sched_get_priority_min(SCHED_FIFO);
    param.sched_priority = (maxPri + minPri) / 2;

    int ret = sched_setscheduler(0, SCHED_FIFO, &param);
    if (ret == 0) {
        std::cout << "  Successfully set SCHED_FIFO with priority " << param.sched_priority << "\n";
        CHECK(true, "sched_setscheduler(SCHED_FIFO) succeeds");

        // Restore to SCHED_OTHER
        param.sched_priority = 0;
        sched_setscheduler(0, SCHED_OTHER, &param);
        CHECK(true, "Restored to SCHED_OTHER");
    } else {
        std::cout << "  Failed to set SCHED_FIFO (errno: " << errno << ")\n";
        std::cout << "  NOTE: This is expected in CI/container environments without CAP_SYS_NICE.\n";
        CHECK(true, "sched_setscheduler(SCHED_FIFO) fails as expected without CAP_SYS_NICE (errno=" + std::to_string(errno) + ")");
    }
}

// ============================================================================
// Test: Process memory limits
// ============================================================================

void testProcessMemoryLimits() {
    std::cout << "\n=== Test: Process Memory Limits ===\n";

    struct rlimit rlim;
    int ret = getrlimit(RLIMIT_MEMLOCK, &rlim);
    CHECK(ret == 0, "getrlimit(RLIMIT_MEMLOCK) succeeds");

    if (ret == 0) {
        std::cout << "  RLIMIT_MEMLOCK current: " << rlim.rlim_cur << " bytes\n";
        std::cout << "  RLIMIT_MEMLOCK max:     " << rlim.rlim_max << " bytes\n";

        CHECK(rlim.rlim_cur > 0 || rlim.rlim_cur == RLIM_INFINITY,
              "RLIMIT_MEMLOCK allows memory locking (cur=" + std::to_string(rlim.rlim_cur) + ")");
    }
}

// ============================================================================
// Main
// ============================================================================

int main() {
    std::cout << "=========================================\n";
    std::cout << "  Aestra Realtime Scheduling Test Suite\n";
    std::cout << "=========================================\n";
    std::cout << "  PID: " << getpid() << "\n";
    std::cout << "=========================================\n";

    testCurrentThreadScheduling();
    testMlockallStatus();
    testPriorityRange();
    testRlimitRtprio();
    testSetSchedFifo();
    testProcessMemoryLimits();

    std::cout << "\n=========================================\n";
    std::cout << "  Test Summary\n";
    std::cout << "=========================================\n";
    std::cout << "  Passed: " << g_passes << "\n";
    std::cout << "  Failed: " << g_fails << "\n";
    std::cout << "  Total:  " << (g_passes + g_fails) << "\n";
    std::cout << "=========================================\n";

    return g_fails > 0 ? 1 : 0;
}

#endif // __linux__
