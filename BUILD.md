# 🔨 Building Aestra

For complete build instructions, see **[docs/technical/ui/BUILD_AND_TEST.md](docs/technical/ui/BUILD_AND_TEST.md)**

---

## Quick Start (Windows)

```powershell
# Clone repository
git clone https://github.com/currentsuspect/Aestra.git
cd Aestra

# Install Git hooks
pwsh -File scripts/install-hooks.ps1

# Configure + build (Full app)
cmake --preset full
cmake --build --preset full-release

# (Optional) Watch + rebuild on changes
pwsh -File scripts/watch_build.ps1 -Preset full -Config Release
```

---

## Headless (Optional)

Use this for CI/containers or DSP-only regression runs (no OpenGL/UI dependencies at configure time).

```powershell
cmake --preset headless
cmake --build --preset headless-release
pwsh -File scripts/watch_build.ps1 -Preset headless -Config Release
```

Important note: CMake preset build directories are generator-specific. If you previously configured `build/headless` (or `build/full`) with a different generator (e.g. Ninja vs Visual Studio), delete that preset folder and re-run `cmake --preset ...`.

---

**For detailed instructions including:**
- Linux build steps
- Troubleshooting common issues
- Build options and configurations
- Cross-platform considerations

**See the full guide:** **[docs/technical/ui/BUILD_AND_TEST.md](docs/technical/ui/BUILD_AND_TEST.md)**
