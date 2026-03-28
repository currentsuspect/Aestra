#include "../../include/AestraPlatform.h"

#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <sys/resource.h>

namespace Aestra {

// Platform::setCurrentThreadPriority implementation for macOS
bool Platform::setCurrentThreadPriority(ThreadPriority priority) {
    if (priority == ThreadPriority::Normal) {
        // Normal priority
        struct sched_param param;
        param.sched_priority = 0;
        if (pthread_setschedparam(pthread_self(), SCHED_OTHER, &param) == 0) {
            setpriority(PRIO_PROCESS, 0, 0);
            return true;
        }
        return false;
    }

    if (priority == ThreadPriority::Low) {
        // Low priority: nice value 10
        setpriority(PRIO_PROCESS, 0, 10);
        return true;
    }

    if (priority == ThreadPriority::High) {
        // High priority: nice value -10
        if (setpriority(PRIO_PROCESS, 0, -10) == 0)
            return true;

        std::cerr << "Warning: Failed to set High thread priority (requires elevated privileges)." << std::endl;
        return false;
    }

    if (priority == ThreadPriority::RealtimeAudio) {
        // Realtime audio priority on macOS
        // Use SCHED_FIFO with high priority
        int policy = SCHED_FIFO;
        int min_prio = sched_get_priority_min(policy);
        int max_prio = sched_get_priority_max(policy);

        struct sched_param param;
        // Conservative RT priority
        param.sched_priority = min_prio + 10;
        if (param.sched_priority > max_prio)
            param.sched_priority = max_prio;

        if (pthread_setschedparam(pthread_self(), policy, &param) == 0) {
            return true;
        } else {
            std::cerr << "Warning: Failed to set Realtime thread priority (needs elevated privileges)." << std::endl;
            // Fallback to high priority nice
            setpriority(PRIO_PROCESS, 0, -15);
            return false;
        }
    }

    return true;
}

// AudioThreadScope implementation
Platform::AudioThreadScope::AudioThreadScope() {
    // Attempt to set realtime priority for audio thread
    m_valid = Platform::setCurrentThreadPriority(ThreadPriority::RealtimeAudio);
    // m_handle unused on macOS
}

Platform::AudioThreadScope::~AudioThreadScope() {
    // Revert to normal
    Platform::setCurrentThreadPriority(ThreadPriority::Normal);
}

} // namespace Aestra
