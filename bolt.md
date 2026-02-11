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

- **Innovation**: Compile the entire static portion of the DSP graph into a single optimized function pointer using LLVM or a custom JIT assembler.
- **Benefit**: Removes virtual function overhead (indirect calls) for static chains, enabling compiler optimizations (inlining, constant folding) across plugin boundaries.

### GPU-Accelerated Spectral Processing

- **Innovation**: Offload heavy spectral tasks (FFT/iFFT) to the GPU using Vulkan Compute/CUDA for plugins with large window sizes (Reverbs, Spectral Restoration).
- **Benefit**: Frees up CPU for serial processing, handling thousands of bands in real-time.

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

### Fast Math Approximations

- **Plan**: Use fast approximations for `sin`, `cos`, `exp`, `log` in modulation paths where precision is less critical.
- **Benefit**: Significant CPU savings in complex synth patches.

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

- **Violation**: `SamplerPlugin` uses `std::unique_lock` in `process()`.
- **Fix**: Replaced with `std::atomic<std::shared_ptr>` + Deferred Reclamation (GC).
- **Audit Script Fix**: `audit_codebase.py` was flagging false positives in Doxygen comments and deleted functions. Updated script to strip comments and correctly handle declarations.

### Platform Isolation (Threading)

- **Violation**: `AestraThreading.h` leaked `<windows.h>` into public headers.
- **Fix**: Moved `MMCSS` and `ThreadPool` implementations to `AestraCore/src/AestraThreading.cpp`, isolating platform headers.
- **Violation**: `AudioEngine.h` included `<windows.h>` unnecessarily.
- **Fix**: Removed the include.
- **Violation**: `ASIOInterface.h` missing platform exemption tag.
- **Fix**: Added `// ALLOW_PLATFORM_INCLUDE` to legitimize the inclusion.

---
*Signed: Bolt*
