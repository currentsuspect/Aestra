// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.

#include "DSP/ReverbSIMD.h"

#include <array>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

constexpr size_t kLines = Aestra::Audio::DSP::ReverbSIMD::kFDNLineCount;

struct FDNState {
    std::array<float, kLines> damping{};
    std::array<float, kLines> lowDamp{};
    std::array<int, kLines> pos{};
    std::array<int, kLines> masks{};
    std::array<std::vector<float>, kLines> buffers{};
    std::array<float*, kLines> ptrs{};

    FDNState() {
        for (size_t line = 0; line < kLines; ++line) {
            buffers[line].assign(32, 0.0f);
            masks[line] = 31;
            pos[line] = static_cast<int>((line * 3) & 31);
            damping[line] = 0.01f * static_cast<float>(line + 1);
            lowDamp[line] = -0.004f * static_cast<float>(line + 1);
            ptrs[line] = buffers[line].data();
        }
    }
};

bool nearlyEqual(float a, float b, float tolerance = 1.0e-6f) {
    return std::abs(a - b) <= tolerance;
}

bool compareStates(const FDNState& a, const FDNState& b) {
    for (size_t line = 0; line < kLines; ++line) {
        if (!nearlyEqual(a.damping[line], b.damping[line]) ||
            !nearlyEqual(a.lowDamp[line], b.lowDamp[line]) ||
            a.pos[line] != b.pos[line]) {
            return false;
        }
        for (size_t i = 0; i < a.buffers[line].size(); ++i) {
            if (!nearlyEqual(a.buffers[line][i], b.buffers[line][i])) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main() {
    using namespace Aestra::Audio::DSP;

#if defined(AESTRA_REVERB_HAS_SSE)
    std::array<float, kLines> lineOut{{0.31f, -0.19f, 0.07f, 0.41f, -0.23f, 0.17f, -0.11f, 0.29f}};
    std::array<float, kLines> feedback{{0.73f, 0.71f, 0.69f, 0.67f, 0.65f, 0.63f, 0.61f, 0.59f}};
    std::array<float, kLines> injectL{{0.93f, -0.37f, 0.61f, -0.79f, 0.23f, 0.84f, -0.51f, 0.42f}};
    std::array<float, kLines> injectR{{0.29f, 0.88f, -0.73f, -0.19f, 0.96f, -0.44f, 0.57f, -0.82f}};
    std::array<float, kLines> outputL{{0.42f, -0.31f, 0.37f, 0.23f, -0.36f, 0.29f, 0.33f, -0.27f}};
    std::array<float, kLines> outputR{{0.24f, 0.39f, -0.28f, 0.35f, 0.31f, -0.34f, 0.26f, 0.41f}};

    FDNState scalar;
    FDNState sse;
    float scalarWetL = 0.0f;
    float scalarWetR = 0.0f;
    float sseWetL = 0.0f;
    float sseWetR = 0.0f;

    const bool previousForceScalar = ReverbSIMD::g_forceScalarFallback;
    ReverbSIMD::g_forceScalarFallback = true;
    ReverbSIMD::processFDNSample(lineOut.data(), 0.2f, -0.13f,
                                 scalar.damping.data(), scalar.lowDamp.data(),
                                 scalar.ptrs.data(), scalar.pos.data(), scalar.masks.data(),
                                 feedback.data(), injectL.data(), injectR.data(),
                                 outputL.data(), outputR.data(),
                                 0.62f, 0.014f, 0.45f, scalarWetL, scalarWetR);
    ReverbSIMD::g_forceScalarFallback = previousForceScalar;

    ReverbSIMD::processFDNSampleSSE(lineOut.data(), 0.2f, -0.13f,
                                    sse.damping.data(), sse.lowDamp.data(),
                                    sse.ptrs.data(), sse.pos.data(), sse.masks.data(),
                                    feedback.data(), injectL.data(), injectR.data(),
                                    outputL.data(), outputR.data(),
                                    0.62f, 0.014f, 0.45f, sseWetL, sseWetR);

    if (!nearlyEqual(scalarWetL, sseWetL) || !nearlyEqual(scalarWetR, sseWetR) ||
        !compareStates(scalar, sse)) {
        std::cerr << "Reverb SSE path diverges from scalar FDN reference\n";
        return 1;
    }
#else
    std::cout << "SSE reverb path not available on this platform; parity check skipped.\n";
#endif

    return 0;
}
