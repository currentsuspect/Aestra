// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// When and how long to wait before re-asking a provider. Pure functions,
// split out so the schedule is testable without a clock or a network.

namespace Aestra {
namespace MuseAgent {

struct RetryPolicy {
    int maxAttempts = 5;         // total tries, including the first
    long baseDelayMs = 1000;     // delay before the first retry
    long maxDelayMs = 30000;     // ceiling for the exponential schedule
    long maxRetryAfterMs = 120000; // ceiling for a server-supplied Retry-After
};

/**
 * @brief Is this failure worth another try? Transport errors, 408, 429 and
 *        5xx are; every other 4xx (bad request, bad key, unknown model) is the
 *        caller's fault and would fail identically again.
 */
inline bool isTransientFailure(bool transportFailure, int httpStatus) {
    if (transportFailure) return true;
    return httpStatus == 408 || httpStatus == 429 || (httpStatus >= 500 && httpStatus <= 599);
}

/**
 * @brief Delay before retry number @p retryIndex (1 = the first retry).
 *
 * Exponential from baseDelayMs, capped at maxDelayMs, plus up to 25% jitter
 * taken from @p jitter01 in [0, 1). A server-supplied Retry-After
 * (@p retryAfterSeconds >= 0) is honoured instead, capped at maxRetryAfterMs.
 */
inline long retryDelayMs(const RetryPolicy& policy, int retryIndex, int retryAfterSeconds,
                         double jitter01) {
    if (retryAfterSeconds >= 0) {
        const long requested = static_cast<long>(retryAfterSeconds) * 1000L;
        return requested < policy.maxRetryAfterMs ? requested : policy.maxRetryAfterMs;
    }
    long delay = policy.baseDelayMs;
    for (int i = 1; i < retryIndex && delay < policy.maxDelayMs; ++i) delay *= 2;
    if (delay > policy.maxDelayMs) delay = policy.maxDelayMs;
    if (jitter01 < 0.0) jitter01 = 0.0;
    if (jitter01 >= 1.0) jitter01 = 0.999999;
    return delay + static_cast<long>(static_cast<double>(delay) * 0.25 * jitter01);
}

} // namespace MuseAgent
} // namespace Aestra
