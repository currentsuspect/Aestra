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

### JIT DSP Compilation

- **Innovation**: Compile user-scripted DSP or complex graphs to machine code at runtime (using LLVM or specialized JIT).
- **Benefit**: Native performance for dynamic user scripts.

### GPU Audio Offloading

- **Innovation**: Offload massive convolution reverbs or spectral processing to GPU (CUDA/Vulkan).
- **Benefit**: Free up CPU for low-latency serial processing.

## 2. Performance Boosts

### AVX-512 Everywhere

- **Status**: Partially used in `SampleRateConverter`.
- **Plan**: Add `__attribute__((target("avx512f")))` kernels for all mixing ops (`AudioBuffer::mix`, `Gain`, `Pan`).
- **Dynamic Dispatch**: Ensure `CPUDetection` selects the best kernel at runtime.

### Lock-Free Garbage Collection

- **Status**: Implemented & Integrated (Fixed leak in `AudioEngine`).
- **Plan**: Maintain `GarbageCollector` singleton using the "Zombie Queue" pattern.
- **Benefit**: Eliminates all mutexes from `processBlock` paths (specifically fixing `SamplerPlugin`).

### Zero-Allocation UI

- **Plan**: Use `ImGui` or custom immediate mode renderer that reuses vertex buffers. Eliminate `std::string` allocations in the draw loop (use `fmt::format_to` into fixed buffers).

### SIMD-Accelerated Voice Management

- **Innovation**: Update all synth/sampler voices in a single SIMD pass (SoA layout) instead of iterating Array of Structures (AoS).
- **Benefit**: 4x-8x speedup in polyphonic voice rendering.

## 3. Sound Quality

### 64-bit End-to-End Mixing

- **Plan**: Ensure `AudioBuffer` supports `double` precision.
- **Benefit**: -300dB noise floor, preventing truncation errors in complex chains.

### True-Peak Limiting

- **Plan**: Add 4x oversampling to the Master Limiter to catch inter-sample peaks.
- **Algo**: Use `Sinc32Turbo` for upsampling, apply lookahead limiting, then downsample.

### Phase-Linear EQs

- **Plan**: Implement FIR-based EQs with FFT convolution for zero phase distortion options.

### Oversampled Nonlinearities

- **Innovation**: Automatically oversample any plugin detecting saturation/distortion logic.
- **Benefit**: Eliminates aliasing in saturation plugins without manual user configuration.

## 4. Fixes & Cleanups

### Real-Time Safety

- **Violation**: `SamplerPlugin` used `std::atomic_load` on `std::shared_ptr` (potential lock).
- **Fix**: Replaced with `std::atomic<SampleData*>` (Raw) + `std::shared_ptr<SampleData>` (Holder) pattern. True lock-free access.
- **Violation**: Memory Leak in `AudioEngine` (GarbageCollector never collected).
- **Fix**: Added `GarbageCollector::instance().collect()` to `loudnessWorkerLoop`.

---
*Signed: Bolt*
