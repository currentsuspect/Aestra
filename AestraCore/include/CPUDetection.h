// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
#include <cpuid.h>
#endif

namespace Aestra {
namespace Core {

/**
 * @brief Runtime CPU feature detection for SIMD dispatch.
 *
 * Uses CPUID to detect AVX2 and FMA support at startup.
 * Results are cached in static booleans for zero-overhead queries.
 */
class CPUDetection {
public:
    // Singleton accessor
    static const CPUDetection& get() {
        static CPUDetection instance;
        return instance;
    }

    bool hasAVX2() const { return m_hasAVX2; }
    bool hasFMA() const { return m_hasFMA; }
    bool hasSSE41() const { return m_hasSSE41; }
    bool hasAVX512F() const { return m_hasAVX512F; }
    bool hasAVX512DQ() const { return m_hasAVX512DQ; }

    // ARM NEON detection (compile-time on ARM, always false on x86)
#if defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
    bool hasNEON() const { return true; }
#else
    bool hasNEON() const { return false; }
#endif

private:
    CPUDetection() { detectFeatures(); }

    // Check OS support for AVX/AVX512 states using XGETBV (x86 only)
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    uint64_t getXCR0() {
#ifdef _MSC_VER
        return _xgetbv(0);
#else
        uint32_t eax, edx;
        __asm__ __volatile__("xgetbv" : "=a"(eax), "=d"(edx) : "c"(0));
        return (static_cast<uint64_t>(edx) << 32) | eax;
#endif
    }
#endif

    void detectFeatures() {
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
        // x86/x64 CPU feature detection
        detectFeaturesX86();
#else
        // Non-x86 platforms: SIMD features remain false (already default-initialized)
        (void)0;
#endif
    }

private:
#if defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86)
    void detectFeaturesX86() {
        int info[4] = {0};

#ifdef _MSC_VER
        __cpuid(info, 0);
        int nIds = info[0];

        if (nIds >= 1) {
            __cpuid(info, 1);
            m_hasSSE41 = (info[2] & (1 << 19)) != 0;
            m_hasFMA = (info[2] & (1 << 12)) != 0;

            bool hasOSXSAVE = (info[2] & (1 << 27)) != 0;

            if (hasOSXSAVE) {
                uint64_t xcr0 = getXCR0();
                bool osAvxSupport = (xcr0 & 0x6) == 0x6;
                bool osAvx512Support = (xcr0 & 0xE6) == 0xE6;

                if (nIds >= 7) {
                    __cpuidex(info, 7, 0);
                    if (osAvxSupport) {
                        m_hasAVX2 = (info[1] & (1 << 5)) != 0;
                    }
                    if (osAvx512Support) {
                        m_hasAVX512F = (info[1] & (1 << 16)) != 0;
                        m_hasAVX512DQ = (info[1] & (1 << 17)) != 0;
                    }
                }
            }
        }
#else
        // GCC/Clang x86
        unsigned int eax, ebx, ecx, edx;
        if (__get_cpuid(0, &eax, &ebx, &ecx, &edx)) {
            unsigned int nIds = eax;

            if (nIds >= 1) {
                __get_cpuid(1, &eax, &ebx, &ecx, &edx);
                m_hasSSE41 = (ecx & (1 << 19)) != 0;
                m_hasFMA = (ecx & (1 << 12)) != 0;

                bool hasOSXSAVE = (ecx & (1 << 27)) != 0;

                if (hasOSXSAVE) {
                    uint64_t xcr0 = getXCR0();
                    bool osAvxSupport = (xcr0 & 0x6) == 0x6;
                    bool osAvx512Support = (xcr0 & 0xE6) == 0xE6;

                    if (nIds >= 7) {
                        __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
                        if (osAvxSupport) {
                            m_hasAVX2 = (ebx & (1 << 5)) != 0;
                        }
                        if (osAvx512Support) {
                            m_hasAVX512F = (ebx & (1 << 16)) != 0;
                            m_hasAVX512DQ = (ebx & (1 << 17)) != 0;
                        }
                    }
                }
            }
        }
#endif
    }
#endif

    bool m_hasAVX2 = false;
    bool m_hasFMA = false;
    bool m_hasSSE41 = false;
    bool m_hasAVX512F = false;
    bool m_hasAVX512DQ = false;
};

} // namespace Core
} // namespace Aestra
