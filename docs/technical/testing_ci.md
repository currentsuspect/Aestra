# Testing & CI Profiles

This repo now uses explicit test profiles so CI can stay stable while still allowing deeper runtime coverage locally.

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

These are still compiled by default, but not registered in CTest unless explicitly enabled.

## Recommended CI Command Set (stable)

```bash
cmake -S . -B build
cmake --build build -j2
cd build
ctest --output-on-failure
```

## Current high-value confidence suite (March 2026)

These are the self-contained tests that currently give the best signal for the internal plugin + project path work:

- `RumbleStateTest`
- `RumblePluginFactoryTest`
- `RumbleUsagePathTest`
- `RumbleDiscoveryTest`
- `ArsenalInstrumentAttachmentTest`
- `InternalPluginProjectRoundTripTest`
- `RumbleRenderTest`
- `RumbleArsenalAudibleTest`
- `ProjectRoundTripTest`

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
  RumbleArsenalAudibleTest \
  ProjectRoundTripTest -j2

./build-dev/Tests/RumbleStateTest
./build-dev/Tests/RumblePluginFactoryTest
./build-dev/Tests/RumbleUsagePathTest
./build-dev/Tests/RumbleDiscoveryTest
./build-dev/Tests/ArsenalInstrumentAttachmentTest
./build-dev/Tests/InternalPluginProjectRoundTripTest
./build-dev/Tests/Headless/RumbleRenderTest /tmp/rumble_ci
./build-dev/Tests/Headless/RumbleArsenalAudibleTest
./build-dev/Tests/ProjectRoundTripTest
```

## Fixture-driven tests

`OfflineRenderRegressionTest` is built and available, but it is currently fixture-driven rather than self-contained.
It requires a project file and a reference WAV:

```bash
./build-dev/Tests/Headless/OfflineRenderRegressionTest <project.aes> <reference.wav>
```

That means a clean nightly/CI workflow still needs canonical fixtures before this test can be treated as a reliable green/red gate.

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
