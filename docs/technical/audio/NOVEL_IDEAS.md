# Novel DSP & DAW Innovation Proposals

## 1. Adaptive Polyphase Resampler (Feasibility: High)
**Problem:** High-quality resampling (Sinc64) is expensive, even for silence or low-frequency content.
**Idea:** Dynamically adjust filter tap count based on local signal bandwidth.
-   **Silence/Low-freq:** Use cheap linear/cubic interpolation.
-   **Transients/High-freq:** Switch to full Sinc64.
**Implementation:** Analyze input block statistics (zero-crossing, RMS) before resampling. Dispatch to different kernels.
**Status:** Architecture ready (SampleRateConverter already uses runtime dispatch).

## 2. Neural Anti-Aliasing (Feasibility: Medium)
**Problem:** Non-linear saturation adds aliasing. Oversampling is CPU-heavy.
**Idea:** Train a small 1D CNN to "denoise" aliased content, running on NPU/GPU.
**Implementation:** Use ONNX Runtime to load a pre-trained model. Replace the downsampling filter with the neural net.

## 3. Real-Time Collaborative Session (Feasibility: Low/Long-term)
**Problem:** DAW collaboration is file-based and slow.
**Idea:** CRDT (Conflict-free Replicated Data Type) based state engine for tracks/clips.
**Implementation:** Replace `ProjectSerializer` with an operation log (Append-only). Sync operations via WebSocket.

## 4. Differentiable EQ (Feasibility: Medium)
**Problem:** Users struggle to match a "target" tone.
**Idea:** Implement EQ as a differentiable layer. Allow user to provide a "target" sample, and run gradient descent to match parameters.
**Implementation:** Needs a PyTorch/JAX backend or a C++ autodiff library (e.g., Enzyme).

## 5. GPU-Accelerated Convolution Reverb (Feasibility: High)
**Problem:** Long convolution tails consume massive CPU.
**Idea:** Offload FFT/iFFT to Vulkan/Compute Shaders.
**Implementation:** `NomadPlat` already sets up Vulkan. We can add a `ComputeEngine` class to handle async dispatch.
