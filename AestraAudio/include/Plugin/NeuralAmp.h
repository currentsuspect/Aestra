// © 2025 Aestra Studios — All Rights Reserved. Licensed for personal & educational use only.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <string>
#include <vector>

namespace Aestra {
namespace Audio {
namespace DSP {

/**
 * @brief Simple Neural Network Inference Engine for Real-Time Audio (Prototype)
 *
 * Implements a small Multi-Layer Perceptron (MLP) for non-linear distortion/saturation.
 * Designed to be "Differentiable DSP" - the weights can be trained offline (PyTorch)
 * and loaded here.
 *
 * Architecture: Input -> Dense(1->8) -> Tanh -> Dense(8->8) -> Tanh -> Dense(8->1) -> Output
 */
class NeuralAmp {
public:
    NeuralAmp() {
        // Initialize with identity-like pass-through weights for safety
        reset();
    }

    void reset() {
        // Identity init for 1->8->8->1 topology (simplified)
        // ... (omitted for prototype, just random/safe values)
        m_layers.clear();
        m_layers.emplace_back(1, 4);
        m_layers.emplace_back(4, 4);
        m_layers.emplace_back(4, 1);
    }

    /**
     * @brief Process a block of audio through the neural network.
     * Real-time safe (no allocation).
     */
    void process(const float* input, float* output, uint32_t numFrames) {
        if (m_bypassed.load(std::memory_order_relaxed)) {
            if (input != output) {
                std::copy(input, input + numFrames, output);
            }
            return;
        }

        // Per-sample processing (SIMD optimization would batch this)
        // Temporary buffers for layer activations
        // Max hidden size is small (e.g. 16), so stack alloc is fine.
        float bufA[16];
        float bufB[16];

        for (uint32_t i = 0; i < numFrames; ++i) {
            float inSample = input[i];

            // Layer 1: Input (1) -> Hidden A (4)
            denseForward(m_layers[0], &inSample, bufA);
            activationTanh(bufA, 4);

            // Layer 2: Hidden A (4) -> Hidden B (4)
            denseForward(m_layers[1], bufA, bufB);
            activationTanh(bufB, 4);

            // Layer 3: Hidden B (4) -> Output (1)
            float outSample = 0.0f;
            denseForward(m_layers[2], bufB, &outSample);
            // No activation on output (linear) or Tanh for limiting?
            // Let's use soft Tanh for amp simulation
            output[i] = std::tanh(outSample);
        }
    }

    void setBypass(bool bypassed) { m_bypassed.store(bypassed); }

private:
    struct Layer {
        int inSize;
        int outSize;
        std::vector<float> weights; // Layout: out * in (row major)
        std::vector<float> biases;  // Layout: out

        Layer(int in, int out) : inSize(in), outSize(out) {
            weights.resize(in * out, 0.1f); // Init with small values
            biases.resize(out, 0.0f);
        }
    };

    std::vector<Layer> m_layers;
    std::atomic<bool> m_bypassed{false};

    // Forward pass for a dense layer
    inline void denseForward(const Layer& layer, const float* in, float* out) {
        for (int r = 0; r < layer.outSize; ++r) {
            float sum = layer.biases[r];
            const float* rowWeights = &layer.weights[r * layer.inSize];

            // Vectorize this loop for AVX
            for (int c = 0; c < layer.inSize; ++c) {
                sum += in[c] * rowWeights[c];
            }
            out[r] = sum;
        }
    }

    inline void activationTanh(float* data, int size) {
        for (int i = 0; i < size; ++i) {
            // Fast tanh approximation could go here
            data[i] = std::tanh(data[i]);
        }
    }
};

} // namespace DSP
} // namespace Audio
} // namespace Aestra
