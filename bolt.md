# Bolt: Innovations for Aestra

As the performance and quality agent "Bolt", I propose the following innovations and improvements to make Aestra the strongest DAW.

## 1. Innovations

### NeuralFX Suite (Expanded)

Expand the prototype `NeuralAmp` into a full suite of differentiable DSP plugins.
- **NeuralCab**: Convolutional/Recurrent models for cabinet simulation.
- **NeuralComp**: Modeling vintage opto/FET compressors using LSTMs.
- **Architecture**: Use AVX-512 optimized inference (hand-written kernels) instead of generic matrix ops.
- **Training**: Provide a Python script to let users train their own models from hardware.
- **Voice Cloning**: Real-time neural voice conversion using RVC-style architecture optimized for low latency.

### TurboSampler (Zero-Copy Streaming)

Current `SamplerPlugin` loads entire samples into RAM.
- **Innovation**: Implement memory-mapped file streaming (using `mmap` on Linux/Windows) with a lock-free ring buffer for the audio thread.
- **Benefit**: Instant load times for multi-GB libraries, near-zero RAM footprint.

### HyperGraph Audio Engine

Move from a linear processing list to a DAG (Directed Acyclic Graph) task scheduler.
- **Innovation**: Analyze dependency graph of tracks/busses. Dispatch independent branches to separate threads using a work-stealing scheduler.
- **Benefit**: Massive multi-core scaling (e.g., 64-core Threadripper support).

### Collaborative Editing (Real-Time)

- **Innovation**: Implement CRDTs (Conflict-free Replicated Data Types) for project state.
- **Benefit**: Allow multiple users to edit the same project simultaneously over the network without locking.

### WASM Sandboxed Plugins

- **Innovation**: Run third-party VST3s inside a WebAssembly container (using `wasm2c` or similar).
- **Benefit**: Plugin crashes never crash the DAW. Security against malicious plugins.

## 2. Performance Boosts

### AVX-512 Everywhere

- **Status**: Partially used in `SampleRateConverter`.
- **Plan**: Add `__attribute__((target("avx512f")))` kernels for all mixing ops (`AudioBuffer::mix`, `Gain`, `Pan`).
- **Dynamic Dispatch**: Ensure `CPUDetection` selects the best kernel at runtime.

### GPU Audio Processing

- **Plan**: Offload heavy spectral processing (FFT convolution reverbs, spectral denoise) to GPU via Vulkan Compute Shaders.
- **Benefit**: Frees up CPU for low-latency serial processing.

### JIT DSP Scripting

- **Plan**: Integrate LuaJIT for user-defined DSP scripts.
- **Benefit**: Near-C++ performance for custom user effects without compilation steps.

### Lock-Free Garbage Collection

- **Status**: Missing global GC for audio thread resources.
- **Plan**: Implement a `GarbageCollector` singleton using the "Zombie Queue" pattern.
- **Benefit**: Eliminates all mutexes from `processBlock` paths (specifically fixing `SamplerPlugin`).

### Zero-Allocation UI

- **Plan**: Use `ImGui` or custom immediate mode renderer that reuses vertex buffers. Eliminate `std::string` allocations in the draw loop (use `fmt::format_to` into fixed buffers).

## 3. Sound Quality

### 64-bit End-to-End Mixing

- **Plan**: Ensure `AudioBuffer` supports `double` precision.
- **Benefit**: -300dB noise floor, preventing truncation errors in complex chains.

### True-Peak Limiting

- **Plan**: Add 4x oversampling to the Master Limiter to catch inter-sample peaks.
- **Algo**: Use `Sinc32Turbo` for upsampling, apply lookahead limiting, then downsample.

### Psychoacoustic Oversampling

- **Innovation**: Adaptive oversampling that only activates when high-frequency content (e.g. >10kHz) is detected and non-linear processing is applied.
- **Benefit**: Saves CPU on bass-heavy tracks while preserving alias-free quality on cymbals/synths.

### Phase-Linear EQs

- **Plan**: Implement FIR-based EQs with FFT convolution for zero phase distortion options.

## 4. Fixes & Cleanups

### Real-Time Safety Audit Results

- **Status**: **PASSING**
- **Fixes Applied**:
    - Refactored `AestraThreading` to move platform-specific code (Windows/MMCSS) out of headers.
    - Added `// ALLOW_PLATFORM_INCLUDE` tags to explicitly allowed platform files (`AestraThreading.cpp`, `ASIOInterface.h`).
    - Updated `AudioEngine` to include platform headers only in implementation (`.cpp`).
    - Updated audit scripts to ignore false positives (`= delete`).

---
*Signed: Bolt*
