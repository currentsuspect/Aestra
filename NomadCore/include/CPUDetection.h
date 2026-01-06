// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <string>
#include <mutex>

namespace Nomad {
namespace System {

/**
 * @brief Runtime CPU feature detection
 *
 * Used to dispatch optimized code paths (AVX2, AVX-512) at runtime.
 * Thread-safe singleton.
 */
class CPUDetection {
public:
    static CPUDetection& getInstance() {
        static CPUDetection instance;
        return instance;
    }

    bool hasSSE() const noexcept { return m_hasSSE; }
    bool hasAVX() const noexcept { return m_hasAVX; }
    bool hasAVX2() const noexcept { return m_hasAVX2; }
    bool hasFMA() const noexcept { return m_hasFMA; }
    bool hasAVX512F() const noexcept { return m_hasAVX512F; }
    bool hasAVX512DQ() const noexcept { return m_hasAVX512DQ; }

    std::string getCPUBrand() const { return m_cpuBrand; }

private:
    CPUDetection(); // Constructor performs detection

    bool m_hasSSE{false};
    bool m_hasAVX{false};
    bool m_hasAVX2{false};
    bool m_hasFMA{false};
    bool m_hasAVX512F{false};
    bool m_hasAVX512DQ{false};

    std::string m_cpuBrand;
};

} // namespace System
} // namespace Nomad
