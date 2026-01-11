# 🔨 Building Nomad DAW

For complete build instructions, see **[docs/BUILDING.md](docs/BUILDING.md)**

---

## Quick Start (Windows)

```powershell
# Clone repository
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD

# Install Git hooks
pwsh -File scripts/install-hooks.ps1

# Configure build
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build --config Release --parallel
```

---

**For detailed instructions including:**
- Linux build steps
- Troubleshooting common issues
- Build options and configurations
- Cross-platform considerations

**See the full guide:** **[docs/BUILDING.md](docs/BUILDING.md)**
