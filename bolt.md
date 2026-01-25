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

### GPU Audio Offloading

- **Innovation**: Utilize Vulkan Compute Shaders for heavy spectral processing (Convolution Reverb, Spectral Cleaning).
- **Benefit**: Offload generic DSP from CPU, enabling massive track counts with heavy effects.

### JIT DSP Compilation

- **Innovation**: Compile effect chains into optimized machine code at runtime using LLVM ORC JIT, removing virtual function overhead and enabling cross-plugin optimization.
- **Benefit**: 20-30% reduction in CPU usage for long effect chains.

## 2. Performance Boosts

### AVX-512 Everywhere

- **Status**: Partially used in `SampleRateConverter`.
- **Plan**: Add `__attribute__((target("avx512f")))` kernels for all mixing ops (`AudioBuffer::mix`, `Gain`, `Pan`).
- **Dynamic Dispatch**: Ensure `CPUDetection` selects the best kernel at runtime.

### Lock-Free Garbage Collection

- **Status**: Missing global GC usage in `AudioEngine`.
- **Plan**: Implement a `GarbageCollector` singleton using the "Zombie Queue" pattern with timestamp-based reclamation.
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

### Phase-Linear EQs

- **Plan**: Implement FIR-based EQs with FFT convolution for zero phase distortion options.

### Psychoacoustic Noise Shaping

- **Innovation**: Apply ATH (Absolute Threshold of Hearing) based noise shaping during dithering.
- **Benefit**: Perceptually silent quantization noise at 16-bit.

## 4. Fixes & Cleanups

### Real-Time Safety Violations

- **Violation**: `SamplerPlugin` used `std::atomic_load` on `std::shared_ptr` (potential lock).
- **Fix**: Replaced with `std::atomic<SampleData*>` (raw pointer) + GC-managed Holder pattern.
- **Violation**: `AudioEngine` used `std::dynamic_pointer_cast` in `processBlock`.
- **Fix**: Added `requestHardResetVoices` virtual method to `IPluginInstance` to avoid RTTI/allocations.

### Memory Leaks

- **Violation**: `GarbageCollector::collect()` was never called.
- **Fix**: Added `collect()` call to `AudioEngine::loudnessWorkerLoop`.

---
*Signed: Bolt*
