// © 2025 Nomad Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <vector>
#include <cmath>
#include <array>
#include <algorithm>
#include "AudioProcessor.h"
#include "../../NomadCore/include/NomadMath.h"

namespace Nomad {
namespace Audio {
namespace DSP {

/**
 * @brief NeuralAmp: A lightweight Neural Network inference engine for real-time audio.
 *
 * Implements a small Multi-Layer Perceptron (MLP) for non-linear saturation/distortion.
 * Design target: < 1% CPU for 16-sample block.
 *
 * Architecture:
 * - Input: [Sample(t), Sample(t-1), Sample(t-2)] (History of 3)
 * - Layer 1: Dense(3 -> 8) + Tanh
 * - Layer 2: Dense(8 -> 4) + Tanh
 * - Layer 3: Dense(4 -> 1) + Linear
 *
 * Weights are flat-packed for cache locality.
 */
class NeuralAmp : public AudioProcessor {
public:
    NeuralAmp() = default;

    struct ModelWeights {
        // L1: 3->8 (24 weights + 8 biases)
        std::array<float, 24> w1;
        std::array<float, 8> b1;

        // L2: 8->4 (32 weights + 4 biases)
        std::array<float, 32> w2;
        std::array<float, 4> b2;

        // L3: 4->1 (4 weights + 1 bias)
        std::array<float, 4> w3;
        float b3;
    };

    void loadWeights(const ModelWeights& weights) {
        m_weights = weights;
    }

    // Load "Tube Screamer" style default weights (randomized/approx for prototype)
    void loadDefaultWeights() {
        // Initialize with identity-like pass-through + soft clipping
        // Ideally these come from a PyTorch export
        std::fill(m_weights.w1.begin(), m_weights.w1.end(), 0.1f);
        std::fill(m_weights.b1.begin(), m_weights.b1.end(), 0.0f);
        std::fill(m_weights.w2.begin(), m_weights.w2.end(), 0.1f);
        std::fill(m_weights.b2.begin(), m_weights.b2.end(), 0.0f);
        std::fill(m_weights.w3.begin(), m_weights.w3.end(), 0.5f);
        m_weights.b3 = 0.0f;
    }

    // AudioProcessor Implementation (Stereo Interleaved)
    // NOTE: Hardcoded to Stereo for Prototype
    void process(float* outputBuffer, const float* inputBuffer, uint32_t numFrames, double streamTime) override {
        // [FIX] Ensure we don't process if buffers are null
        if (!outputBuffer) return;

        // In-place processing if inputBuffer is null (or same as output)
        // If separate, copy first.
        if (inputBuffer && inputBuffer != outputBuffer) {
            std::memcpy(outputBuffer, inputBuffer, numFrames * 2 * sizeof(float));
        }

        // Processing per-channel independently
        // [FIX] Only process if we have stereo data.
        // For mono or >2 channels, we would need to know the channel count.
        // The AudioProcessor interface is generic.
        // We assume 2 channels interleaved for this prototype.

        for (uint32_t c = 0; c < 2; ++c) {
            float x1 = 0.0f, x2 = 0.0f;
            float& s1 = (c == 0) ? m_stateL1 : m_stateR1;
            float& s2 = (c == 0) ? m_stateL2 : m_stateR2;

            x1 = s1;
            x2 = s2;

            const uint32_t stride = 2;

            for (uint32_t i = 0; i < numFrames; ++i) {
                float in = outputBuffer[i * stride + c];

                // --- Inference ---
                // L1 (3->8)
                std::array<float, 8> h1;
                for (int n = 0; n < 8; ++n) {
                    float sum = m_weights.b1[n];
                    sum += in * m_weights.w1[n * 3 + 0];
                    sum += x1 * m_weights.w1[n * 3 + 1];
                    sum += x2 * m_weights.w1[n * 3 + 2];
                    h1[n] = std::tanh(sum); // Activation
                }

                // L2 (8->4)
                std::array<float, 4> h2;
                for (int n = 0; n < 4; ++n) {
                    float sum = m_weights.b2[n];
                    for (int k = 0; k < 8; ++k) {
                        sum += h1[k] * m_weights.w2[n * 8 + k];
                    }
                    h2[n] = std::tanh(sum);
                }

                // L3 (4->1)
                float out = m_weights.b3;
                for (int k = 0; k < 4; ++k) {
                    out += h2[k] * m_weights.w3[k];
                }

                outputBuffer[i * stride + c] = out;

                // Update history
                x2 = x1;
                x1 = in;
            }
            // Save state
            s1 = x1;
            s2 = x2;
        }
    }

    // Process optimized mono block (for microbenchmark)
    void processMono(float* buffer, uint32_t numFrames) {
        float x1 = 0.0f, x2 = 0.0f;
        for (uint32_t i = 0; i < numFrames; ++i) {
            float in = buffer[i];

             // L1 (3->8)
            float h1[8];
            for (int n = 0; n < 8; ++n) {
                float sum = m_weights.b1[n];
                sum += in * m_weights.w1[n * 3 + 0];
                sum += x1 * m_weights.w1[n * 3 + 1];
                sum += x2 * m_weights.w1[n * 3 + 2];
                // Fast Tanh approximation: x * (27 + x^2) / (27 + 9x^2) ? No, that's sigmoid-ish.
                // Standard std::tanh is slow. Use Pade approximation.
                // For prototype, std::tanh is fine.
                h1[n] = std::tanh(sum);
            }

            // L2 (8->4)
            float h2[4];
            for (int n = 0; n < 4; ++n) {
                float sum = m_weights.b2[n];
                for (int k = 0; k < 8; ++k) {
                    sum += h1[k] * m_weights.w2[n * 8 + k];
                }
                h2[n] = std::tanh(sum);
            }

            // L3 (4->1)
            float out = m_weights.b3;
            for (int k = 0; k < 4; ++k) {
                out += h2[k] * m_weights.w3[k];
            }

            buffer[i] = out;
            x2 = x1;
            x1 = in;
        }
    }

private:
    ModelWeights m_weights;
    float m_stateL1{0.0f}, m_stateL2{0.0f};
    float m_stateR1{0.0f}, m_stateR2{0.0f};
};

} // namespace DSP
} // namespace Audio
} // namespace Nomad
