// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#include "../include/CPUDetection.h"
#include <array>
#include <vector>
#include <cstring>

#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__GNUC__) || defined(__clang__)
    #include <cpuid.h>
#endif

namespace Nomad {
namespace System {

CPUDetection::CPUDetection() {
    std::vector<std::array<int, 4>> data;
    std::array<int, 4> cpui;

#if defined(_MSC_VER)
    __cpuid(cpui.data(), 0);
    int nIds = cpui[0];
    for (int i = 0; i <= nIds; ++i) {
        __cpuidex(cpui.data(), i, 0);
        data.push_back(cpui);
    }

    // Brand string
    char brand[0x40];
    memset(brand, 0, sizeof(brand));
    for (int i = 0x80000000; i <= 0x80000004; ++i) {
        __cpuid(cpui.data(), i);
        if (i == 0x80000002) memcpy(brand, cpui.data(), sizeof(cpui));
        else if (i == 0x80000003) memcpy(brand + 16, cpui.data(), sizeof(cpui));
        else if (i == 0x80000004) memcpy(brand + 32, cpui.data(), sizeof(cpui));
    }
    m_cpuBrand = std::string(brand);

#elif defined(__GNUC__) || defined(__clang__)
    __cpuid(0, cpui[0], cpui[1], cpui[2], cpui[3]);
    int nIds = cpui[0];
    for (int i = 0; i <= nIds; ++i) {
        __cpuid_count(i, 0, cpui[0], cpui[1], cpui[2], cpui[3]);
        data.push_back(cpui);
    }

    // Brand string (simplified, maybe missing some extensions)
    m_cpuBrand = "Unknown (Linux/macOS)";
#endif

    if (data.size() > 1) {
        m_hasSSE = (data[1][3] & (1 << 25)) != 0;
        m_hasAVX = (data[1][2] & (1 << 28)) != 0;
        m_hasFMA = (data[1][2] & (1 << 12)) != 0;
    }

    if (data.size() > 7) {
        m_hasAVX2 = (data[7][1] & (1 << 5)) != 0;
        m_hasAVX512F = (data[7][1] & (1 << 16)) != 0;
        m_hasAVX512DQ = (data[7][1] & (1 << 17)) != 0;
    }
}

} // namespace System
} // namespace Nomad
