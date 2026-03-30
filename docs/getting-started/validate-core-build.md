# Validate the public build (quick smoke test)

This document explains how to confirm the public repository build configures and builds locally without premium modules.

Steps:

1) Ensure `assets_mock/` exists in the repo root (it was added for this purpose).

2) Confirm the top-level `CMakeLists.txt` includes the public core-mode helper from `aestra-core/cmake/AestraCoreMode.cmake`. This is what forces `Aestra_CORE_MODE=ON` when premium modules are not present.

3) Configure CMake for a Release build (Windows example):

   cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DAestra_CORE_MODE=ON -DAESTRA_ENABLE_TESTS=ON -DCMAKE_BUILD_TYPE=Release

4) Build the public targets:

   cmake --build build --config Release --parallel

5) Run a quick smoke test (if executable produced):

   if (Test-Path build\bin\Release\Aestra.exe) {
       echo "Executable exists — launch to confirm the public app surface loads."
   }

6) If configuration or linking fails because premium modules are missing, re-check that `Aestra_CORE_MODE=ON` is being applied and that the build is using `assets_mock/`.

Notes:
- This validation ensures the public repo shape is buildable; it does not exercise premium/commercial-only modules.
- Prefer the canonical build instructions in `docs/getting-started/building.md` for day-to-day work.
