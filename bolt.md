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

## 2. Performance Boosts

### AVX-512 Everywhere

- **Status**: Partially used in `SampleRateConverter`.
- **Plan**: Add `__attribute__((target("avx512f")))` kernels for all mixing ops (`AudioBuffer::mix`, `Gain`, `Pan`).
- **Dynamic Dispatch**: Ensure `CPUDetection` selects the best kernel at runtime.

### Lock-Free Garbage Collection

- **Status**: Implemented (`GarbageCollector` singleton with "Zombie Queue").
- **Usage**: Used in `SamplerPlugin` to safely reclaim `SampleData`.
- **Benefit**: Eliminates all mutexes from `processBlock` paths.

### Zero-Allocation UI

- **Plan**: Use `ImGui` or custom immediate mode renderer that reuses vertex buffers. Eliminate `std::string` allocations in the draw loop (use `fmt::format_to` into fixed buffers).

### Fast Math Approximation

- **Status**: Implemented in `SamplerPlugin`.
- **Change**: Replaced `std::pow(2, x)` with `std::exp(x * k)` (approx 3x faster) for pitch shifting.
- **Plan**: Apply to all dB/Gain conversions (using Padé approximation).

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

### Real-Time Safety (SamplerPlugin)

- **Violation**: `SamplerPlugin` used `std::atomic_load(&shared_ptr)` which may use internal locks.
- **Fix**: Replemented using the **Context Swap** pattern: `std::atomic<SampleData*>` (Raw) for audio thread + `std::shared_ptr` holder for main thread ownership + `GarbageCollector` for cleanup.
- **Status**: **Fixed**.

### Platform Hygiene

- **Violation**: Platform headers (`<windows.h>`) leaked into public headers `AudioEngine.h` and `AestraThreading.h`.
- **Fix**: Moved platform implementations (`MMCSS`, `ThreadPool`) to `AestraCore/src/AestraThreading.cpp` and used `// ALLOW_PLATFORM_INCLUDE` for unavoidable cases (`ASIO`).
- **Status**: **Fixed** (Validated by `scripts/check_platform_leaks.py`).

---
*Signed: Bolt*
