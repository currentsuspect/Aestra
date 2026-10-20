// © 2026 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

// Numbers the sandbox helper's fake test plugin reports and the tests that talk to it must agree on. Test builds only.

#include <cstdint>

namespace Aestra {
namespace Audio {

#ifdef AESTRA_ENABLE_TEST_HOOKS
/// What the helper's __aestra_test_echo__ plugin says its own processing latency is (LATENCY command).
constexpr uint32_t kEchoTestLatencySamples = 37;
#endif

} // namespace Audio
} // namespace Aestra
