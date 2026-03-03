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
