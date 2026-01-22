# Bolt: Innovations for Aestra

As the performance and quality agent "Bolt", I propose the following innovations and improvements to make Aestra the strongest DAW.

## 1. Innovations

### NeuralFX Suite
Expand the prototype `NeuralAmp` into a full suite of differentiable DSP plugins.
- **NeuralCab**: Convolutional/Recurrent models for cabinet simulation.
- **NeuralComp**: Modeling vintage opto/FET compressors using LSTMs.
- **Architecture**: Use AVX-512 optimized inference (hand-written kernels) instead of generic matrix ops.
- **Training**: Provide a Python script to let users train their own models from hardware.

### TurboSampler (Zero-Copy Streaming)
Current `SamplerPlugin` loads entire samples into RAM.
- **Innovation**: Implement memory-mapped file streaming (using `mmap` on Linux/Windows) with a lock-free ring buffer for the audio thread.
- **Benefit**: Instant load times for multi-GB libraries, near-zero RAM footprint.

### HyperGraph Audio Engine
Move from a linear processing list to a DAG (Directed Acyclic Graph) task scheduler.
- **Innovation**: Analyze dependency graph of tracks/busses. Dispatch independent branches to separate threads using a work-stealing scheduler.
- **Benefit**: Massive multi-core scaling (e.g., 64-core Threadripper support).

### WASM Sandboxed Plugins
- **Innovation**: Run third-party VST3s inside a WebAssembly container (using `wasm2c` or similar).
- **Benefit**: Plugin crashes never crash the DAW. Security against malicious plugins.

### Adaptive Polyphase Resampler
- **Innovation**: Dynamically switch interpolation kernels (Sinc8/16/32/64) based on signal frequency content and CPU load.
- **Benefit**: Optimal balance between CPU usage and aliasing rejection.

### GPU Audio Offloading
- **Innovation**: Offload heavy spectral processing (convolution reverbs, spectral editors) to the GPU using Vulkan Compute or CUDA.
- **Benefit**: Free up CPU for low-latency serial processing while handling massive reverbs on the GPU.

### JIT DSP Compilation
- **Innovation**: Use LLVM to JIT-compile effect chains at runtime, fusing multiple plugins into a single function call to eliminate virtual dispatch overhead.
- **Benefit**: 20-30% performance improvement for long effect chains.

## 2. Performance Boosts

### AVX-512 Everywhere
- **Status**: Partially used in `SampleRateConverter`.
- **Plan**: Add `__attribute__((target("avx512f")))` kernels for all mixing ops (`AudioBuffer::mix`, `Gain`, `Pan`).
- **Dynamic Dispatch**: Ensure `CPUDetection` selects the best kernel at runtime.

### Lock-Free Garbage Collection
- **Status**: Implemented via `GarbageCollector` singleton.
- **Plan**: Ensure `collect()` is called periodically in a background thread to reclaim "zombie" resources (e.g., old sample buffers).
- **Benefit**: Eliminates all mutexes from `processBlock` paths.

### Zero-Allocation UI
- **Plan**: Use `ImGui` or custom immediate mode renderer that reuses vertex buffers. Eliminate `std::string` allocations in the draw loop (use `fmt::format_to` into fixed buffers).

## 3. Sound Quality

### 64-bit End-to-End Mixing
- **Plan**: Ensure `AudioBuffer` supports `double` precision.
- **Benefit**: -300dB noise floor, preventing truncation errors in complex chains.

### True-Peak Limiting
- **Plan**: Add 4x oversampling to the Master Limiter to catch inter-sample peaks.
- **Algo**: Use `Sinc32Turbo` for upsampling, apply lookahead limiting, then downsample.

### Phase-Linear EQs
- **Plan**: Implement FIR-based EQs with FFT convolution for zero phase distortion options.

## 4. Fixes & Cleanups

### Real-Time Safety
- **Violation**: `SamplerPlugin` used `std::unique_lock` or blocking `std::atomic_load(&shared_ptr)` in `process()`.
- **Fix**: Replaced with `std::atomic<SampleData*>` (raw pointer) + `std::shared_ptr` holder pattern. This ensures wait-free access on the hot path.
- **Violation**: `GarbageCollector::collect()` was never called.
- **Fix**: Added `collect()` call to `AudioEngine::loudnessWorkerLoop()` to prevent memory leaks.
- **Violation**: `EffectChain` deleted operators (False Positive in audit, but good to know).

---
*Signed: Bolt*
