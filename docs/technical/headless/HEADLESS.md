# 🧪 Headless Runner (AestraHeadless)

AestraHeadless runs offline DSP scenarios without OpenGL/UI. Use it for CI, containers, and regression checks.

## Build + Run

```powershell
cmake --preset headless
cmake --build --preset headless-release

# List scenarios
./build/headless/bin/Release/AestraHeadless --list-scenarios

# Run all scenarios (writes JSON to stdout)
./build/headless/bin/Release/AestraHeadless --scenario-file Tests/headless_scenarios.json

# Run a single scenario with a human-readable summary
./build/headless/bin/Release/AestraHeadless --scenario roundtrip_autosave --human
```

## Gate Thresholds (CI/Headless)

Use environment variables to enforce performance gates in CI:

- `AESTRA_MIN_PEAK`: fail if max peak is below this (already supported)
- `AESTRA_MAX_CPU_RATIO`: fail if `renderMs / audioMs` exceeds this
- `AESTRA_MAX_RENDER_MS`: fail if total render time exceeds this (milliseconds)

Example:

```powershell
$env:AESTRA_MAX_CPU_RATIO = "0.25"
$env:AESTRA_MAX_RENDER_MS = "150"
./build/headless/bin/Release/AestraHeadless --scenario roundtrip_autosave --human
```

## Failure Codes

AestraHeadless returns **exit code 1** when any scenario fails. Reports include a `failure` string for the specific reason.

Common failure strings:

- `missing_project` – scenario requires a project path but none was provided
- `project_load_failed` – failed to load the requested project
- `project_save_failed` – failed to save during round-trip
- `roundtrip_load_failed` – failed to reload the saved project
- `roundtrip_mismatch` – tempo/playhead mismatch after round-trip
- `nan_or_inf` – NaN/Inf samples detected
- `invalid_peak` – peak sample computed as NaN/Inf
- `near_silence` – max peak below `minPeak`
- `cpu_ratio_exceeded` – exceeded `AESTRA_MAX_CPU_RATIO`
- `render_time_exceeded` – exceeded `AESTRA_MAX_RENDER_MS`
- `unknown_type` – scenario type not recognized

**Exit codes:**
- `0` – all scenarios passed
- `1` – scenario failure (see `failure`)
- `2` – invalid CLI/scenario file or missing scenario
