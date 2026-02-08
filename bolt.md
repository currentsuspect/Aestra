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

### JIT Graph Compilation

- **Innovation**: Use LLVM to Just-In-Time (JIT) compile the entire audio processing graph into a single optimized machine code function at runtime.
- **Benefit**: Eliminates virtual function call overhead and enables cross-plugin optimizations (e.g., loop unrolling across effects).

### GPU-Accelerated Spectral Processing

- **Innovation**: Offload massive FFT/iFFT operations for spectral editing and plugins to the GPU using Vulkan/CUDA.
- **Benefit**: Real-time spectral denoisers and reverbs with zero CPU impact.

### WASM Sandboxed Plugins

- **Innovation**: Run third-party VST3s inside a WebAssembly container (using `wasm2c` or similar).
- **Benefit**: Plugin crashes never crash the DAW. Security against malicious plugins.

## 2. Performance Boosts

### AVX-512 Everywhere

- **Status**: Partially used in `SampleRateConverter`.
- **Plan**: Add `__attribute__((target("avx512f")))` kernels for all mixing ops (`AudioBuffer::mix`, `Gain`, `Pan`).
- **Dynamic Dispatch**: Ensure `CPUDetection` selects the best kernel at runtime.

### Lock-Free Garbage Collection

- **Status**: Missing global GC for audio thread resources.
- **Plan**: Implement a `GarbageCollector` singleton using the "Zombie Queue" pattern.
- **Benefit**: Eliminates all mutexes from `processBlock` paths (specifically fixing `SamplerPlugin`).

### Zero-Allocation UI

- **Plan**: Use `ImGui` or custom immediate mode renderer that reuses vertex buffers. Eliminate `std::string` allocations in the draw loop (use `fmt::format_to` into fixed buffers).

### Safe Context Swap

- **Plan**: Standardize the "Safe Context Swap" pattern across all plugins.
- **Technique**: Use `std::atomic<T*>` for the read-only pointer in the audio thread, and a `std::shared_ptr<T>` holder in the main thread to manage lifetime.
- **Benefit**: Completely lock-free parameter updates without race conditions.

### Fast Math Approximations

- **Plan**: Replace standard `std::sin`, `std::exp`, `std::log` in modulation sources (LFOs, Envelopes) with high-performance polynomial approximations.
- **Benefit**: 2x-4x speedup in modulation processing with negligible audible difference.

## 3. Sound Quality

### 64-bit End-to-End Mixing

- **Plan**: Ensure `AudioBuffer` supports `double` precision.
- **Benefit**: -300dB noise floor, preventing truncation errors in complex chains.

### True-Peak Limiting

- **Plan**: Add 4x oversampling to the Master Limiter to catch inter-sample peaks.
- **Algo**: Use `Sinc32Turbo` for upsampling, apply lookahead limiting, then downsample.

### Phase-Linear EQs

- **Plan**: Implement FIR-based EQs with FFT convolution for zero phase distortion options.

### Oversampled Distortion

- **Plan**: Automatically oversample (8x-16x) all nonlinear distortion plugins.
- **Benefit**: Eliminates aliasing artifacts ("digital harshness") in high-gain scenarios.

## 4. Fixes & Cleanups

### Real-Time Safety

- **Violation**: `SamplerPlugin` uses `std::unique_lock` in `process()`.
- **Fix**: Replaced with `std::atomic<std::shared_ptr>` + Deferred Reclamation (GC).
- **Violation**: `EffectChain` deleted operators (False Positive in audit).
- **Fix**: Updated `audit_codebase.py` to correctly ignore `= delete` / `= 0`.

### Platform Hygiene

- **Violation**: Header inclusion of `<windows.h>` in `AestraThreading.h` and `AudioEngine.h`.
- **Fix**: Moved platform-specific implementation to `AestraThreading.cpp` and removed platform headers from public interfaces.
- **Fix**: Tagged `ASIOInterface.h` with `// ALLOW_PLATFORM_INCLUDE` for unavoidable driver dependencies.

---
*Signed: Bolt*
