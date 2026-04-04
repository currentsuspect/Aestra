# Testing & CI Profiles

This repo uses explicit test profiles so CI can stay stable while still allowing deeper runtime coverage locally. The default public CI path is intentionally smaller than a full local `ctest` run.

## CMake Options

### `AESTRA_ENABLE_RUNTIME_TESTS` (default: `OFF`)
Enables tests that depend on host runtime conditions (audio device availability, integration assumptions):
- `ProjectRoundTripTest`
- `AutosaveRoundTripTest`
- `AestraAudioTest`
- `AestraAudioSoakTest`

### `AESTRA_ENABLE_EXPERIMENTAL_TESTS` (default: `OFF`)
Enables tests currently under investigation for consistency/portability:
- `AestraWaveformLockTest`
- `AestraAudioCallbackTest`
- `AestraFilterTest`
- `AestraSampleRateConverterTest`

These tests are still compiled by default, but they are not registered in CTest unless explicitly enabled.

## Maintained Default CI Path

```bash
cmake -S . -B build -DAESTRA_HEADLESS_ONLY=ON -DAESTRA_ENABLE_UI=OFF
cmake --build build -j2
ctest --test-dir build --output-on-failure -R \
  "CommandHistoryTest|MoveClipCommandTest|MacroCommandTest|AestraOscillatorTest|AestraMixerBusTest|AestraAtomicSaveTest"
```

This is the maintained cross-platform gate used in the main CI workflow. It is intentionally narrower than the broader local confidence suite.

## Current high-value self-contained confidence suite

These are the self-contained tests that currently give the best signal for the internal plugin and headless audio paths without requiring runtime-sensitive registration:

- `RumbleStateTest`
- `RumblePluginFactoryTest`
- `RumbleUsagePathTest`
- `RumbleDiscoveryTest`
- `ArsenalInstrumentAttachmentTest`
- `InternalPluginProjectRoundTripTest`
- `RumbleRenderTest`
- `RumbleArsenalAudibleTest`

Recommended local run:

```bash
cmake -S . -B build-dev
cmake --build build-dev --target \
  RumbleStateTest \
  RumblePluginFactoryTest \
  RumbleUsagePathTest \
  RumbleDiscoveryTest \
  ArsenalInstrumentAttachmentTest \
  InternalPluginProjectRoundTripTest \
  RumbleRenderTest \
  RumbleArsenalAudibleTest -j2

./build-dev/Tests/RumbleStateTest
./build-dev/Tests/RumblePluginFactoryTest
./build-dev/Tests/RumbleUsagePathTest
./build-dev/Tests/RumbleDiscoveryTest
./build-dev/Tests/ArsenalInstrumentAttachmentTest
./build-dev/Tests/InternalPluginProjectRoundTripTest
./build-dev/Tests/Headless/RumbleRenderTest /tmp/rumble_ci
./build-dev/Tests/Headless/RumbleArsenalAudibleTest
```

## Runtime-sensitive coverage

These are built by default but not registered in CTest unless `AESTRA_ENABLE_RUNTIME_TESTS=ON`:

- `ProjectRoundTripTest`
- `AutosaveRoundTripTest`
- `AestraAudioTest`
- `AestraAudioSoakTest`

## Offline export parity coverage

`OfflineRenderRegressionTest` is now a self-contained headless parity test. It does not rely on a checked-in golden WAV; instead it compares `AudioExporter` output against a direct engine reference render in-process.

Typical local run:

```bash
cmake --build build-dev --target OfflineRenderRegressionTest -j2
./build-dev/Tests/Headless/OfflineRenderRegressionTest
```

## Full Local Verification (opt-in)

```bash
cmake -S . -B build \
  -DAESTRA_ENABLE_RUNTIME_TESTS=ON \
  -DAESTRA_ENABLE_EXPERIMENTAL_TESTS=ON
cmake --build build -j2
cd build
ctest --output-on-failure
```

## Labeling

When enabled, runtime/experimental suites are labeled to support filtered runs:
- runtime/device/integration/soak labels for runtime-sensitive tests
- experimental/unstable labels for under-investigation tests

Examples:

```bash
# Run only runtime tests (if enabled)
ctest -L runtime --output-on-failure

# Skip unstable tests
ctest -LE unstable --output-on-failure
```
