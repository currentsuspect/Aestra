# NOMAD Hybrid Audio Engine Design (Proposal)

**Status**: Draft / Research
**Target**: NOMAD v4.0

## Problem Statement
Users want to hear "render quality" effects (e.g., non-realtime oversampling, expensive reverbs, mastering chains) while mixing, but CPU limits prevents running these in the real-time (RT) path.

## The Solution: Hybrid Dual-Graph Architecture
Detach the `AudioGraph` processing from the ASIO callback and run **two concurrent instances** of the rendering pipeline.

### 1. The Monitor Graph (RT Path)
-   **Context**: Runs on the ASIO Thread (Real-Time Priority).
-   **Goal**: Lowest possible latency (< 3ms).
-   **Configuration**: 
    -   Draft quality interpolation (Cubic/Sinc8).
    -   Heavy plugins run in "Eco Mode" or are bypassed.
    -   Oversampling disabled.

### 2. The Render Graph (Background Path)
-   **Context**: Runs on a low-priority Worker Thread.
-   **Goal**: Maximum Audio Quality (Offline Rendering output).
-   **Configuration**:
    -   Mastering quality interpolation (Sinc64Turbo).
    -   Plugins running at max quality / high oversampling.
    -   Renders chunks 2-5 seconds *ahead* of the playhead.

### 3. The Crossfader (The "Magic")
The `AudioEngine` mixes the output of the Monitor Graph with the Render Graph.
-   When the users changes a parameter, the engine immediately plays the **Monitor Graph** (instant feedback).
-   The **Render Graph** invalidates its buffer and starts re-rendering the affected section in the background.
-   Once the background chunk is ready, the engine **seamlessly crossfades** from the Monitor stream to the Render stream.
-   **Result**: The user hears the "studio quality" version a few seconds after they stop tweaking a knob.

## Implementation Steps

### Phase 1: Decoupling (Refactor)
-   [ ] Abstract `AudioEngine::renderGraph` into a stateless `AudioRenderer` class.
-   [ ] `AudioRenderer` must take `AudioGraph` state as a const input, not depending on global `AudioEngine` state.
-   [ ] Ensure all Plugins support state cloning (snapshotting).

### Phase 2: Double Buffering
-   [ ] Create a secondary `AudioRenderer` instance ("Shadow Renderer").
-   [ ] Implement a Lookahead Ring Buffer for the Shadow Renderer output.

### Phase 3: Selection Logic
-   [ ] Implement the `CrossfadeEngine` to switch between RT and Shadow streams based on buffer availability.
