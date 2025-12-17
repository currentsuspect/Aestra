#include "../../include/NomadPlatform.h"
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>
#include <iostream>

namespace Nomad {

// Platform::setCurrentThreadPriority implementation for Linux
bool Platform::setCurrentThreadPriority(ThreadPriority priority) {
    if (priority == ThreadPriority::Normal) {
        // Normal priority: SCHED_OTHER, nice 0
        struct sched_param param;
        param.sched_priority = 0;
        if (pthread_setschedparam(pthread_self(), SCHED_OTHER, &param) == 0) {
            setpriority(PRIO_PROCESS, 0, 0); // Reset nice value
            return true;
        }
        return false;
    }
    
    if (priority == ThreadPriority::Low) {
        // Low: SCHED_OTHER, nice 10
        setpriority(PRIO_PROCESS, 0, 10);
        return true;
    }

    if (priority == ThreadPriority::High) {
        // High: nice -10 (might need CAP_SYS_NICE for < 0, but -10 usually requires it)
        // Try nice first
        if (setpriority(PRIO_PROCESS, 0, -10) == 0) return true;
        
        std::cerr << "Warning: Failed to set High thread priority (EPERM?)." << std::endl;
        return false;
    }

    if (priority == ThreadPriority::RealtimeAudio) {
        // Realtime: SCHED_FIFO or SCHED_RR
        // This usually requires higher privileges or rtlimits setup
        int policy = SCHED_FIFO;
        int min_prio = sched_get_priority_min(policy);
        int max_prio = sched_get_priority_max(policy);
        
        struct sched_param param;
        // Conservative RT priority
        param.sched_priority = min_prio + 10;
        if (param.sched_priority > max_prio) param.sched_priority = max_prio;

        if (pthread_setschedparam(pthread_self(), policy, &param) == 0) {
            return true;
        } else {
             std::cerr << "Warning: Failed to set Realtime thread priority (needs CAP_SYS_NICE or RT limits)." << std::endl;
             // Fallback to high priority nice
             setpriority(PRIO_PROCESS, 0, -15);
             return false;
        }
    }

    return true;
}

// AudioThreadScope implementation
Platform::AudioThreadScope::AudioThreadScope() {
    // Attempt to set realtime priority
    m_valid = Platform::setCurrentThreadPriority(ThreadPriority::RealtimeAudio);
    // m_handle unused on Linux
}

Platform::AudioThreadScope::~AudioThreadScope() {
    // Revert to normal
    Platform::setCurrentThreadPriority(ThreadPriority::Normal);
}

} // namespace Nomad
