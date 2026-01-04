# Validate nomad-core build (quick smoke test)

This document explains how to confirm the `nomad-core` subset builds locally without private assets.

Steps:

1) Ensure `assets_mock/` exists in the repo root (it was added for this purpose).

2) Ensure `cmake/NomadPremiumFallback.cmake` is included by the top-level `CMakeLists.txt`.
   If not already present, add a line near the top of `CMakeLists.txt`:

   include(${CMAKE_SOURCE_DIR}/cmake/NomadPremiumFallback.cmake)

3) Configure CMake for a Release build (Windows example):

   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release

4) Build the core targets:

   cmake --build build --config Release --target NOMAD_DAW --parallel

5) Run a quick smoke test (if executable produced):

   if (Test-Path build\bin\Release\NOMAD_DAW.exe) {
       echo "Executable exists — launch to confirm basic UI loads (no premium features)."
   }

6) If you encounter missing files during compile/link, inspect error message for references to `NomadMuse` or `NomadAssets` and ensure the build uses `assets_mock` (see `NomadPremiumFallback.cmake`).

Notes:
- This validation ensures the public `nomad-core` code is buildable; it does not exercise premium features.
- For CI, the public workflow `public-ci.yml` should run these steps on push/PR.
