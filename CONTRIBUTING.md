# 🤝 Contributing to Nomad DAW

Thank you for your interest in contributing to Nomad DAW! We welcome contributions from the community.

---

## 📚 Before You Start

Please read our comprehensive [Contributing Guide](docs/CONTRIBUTING.md) for detailed information about:
- Development workflow
- Code style guidelines
- Pull request process
- Testing requirements
- Communication channels

---

## 🚀 Quick Start

### 1. Setup Development Environment

**Prerequisites:**
- Windows 10/11 (64-bit)
- Visual Studio 2022 with C++ workload
- CMake 3.15+
- Git
- PowerShell 7

**Clone and Build:**
```powershell
# Clone the repository
git clone https://github.com/currentsuspect/NOMAD.git
cd NOMAD

# Install Git hooks for code quality
pwsh -File scripts/install-hooks.ps1

# Configure build (core-only mode)
cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=Release

# Build project
cmake --build build --config Release --parallel
```

See [Building Guide](docs/BUILDING.md) for detailed instructions.

### 2. Find Something to Work On

- Browse [open issues](https://github.com/currentsuspect/NOMAD/issues)
- Check [good first issue](https://github.com/currentsuspect/NOMAD/labels/good%20first%20issue) label
- Review [Roadmap](docs/ROADMAP.md) for planned features

### 3. Make Your Changes

- Create a new branch: `git checkout -b feature/my-feature`
- Follow [Coding Style Guide](docs/CODING_STYLE.md)
- Write clear commit messages (see [Style Guide](docs/STYLE_GUIDE.md))
- Add tests if applicable
- Ensure code compiles and passes existing tests

### 4. Submit Pull Request

- Push your branch: `git push origin feature/my-feature`
- Open a pull request on GitHub
- Describe what your changes do and why
- Link related issues

---

## 📋 Contribution Guidelines

### Code Quality
- ✅ Follow [Coding Style Guide](docs/CODING_STYLE.md)
- ✅ Use clang-format for consistent formatting
- ✅ Add comments for complex logic
- ✅ Write self-documenting code with clear names
- ✅ Include documentation updates

### Pull Requests
- ✅ One feature/fix per PR
- ✅ Clear, descriptive title
- ✅ Detailed description of changes
- ✅ Link to related issues
- ✅ Pass all CI checks

### Commit Messages
Follow this format:
```
type(scope): subject

body (optional)

footer (optional)
```

**Example:**
```
feat(audio): add WASAPI exclusive mode support

Implements exclusive mode with fallback to shared mode.
Reduces latency from 20ms to 5ms.

Closes #42
```

See [Style Guide](docs/STYLE_GUIDE.md#-commit-messages) for details.

---

## ⚖️ Contributor License Agreement

By contributing to Nomad DAW, you agree that:

- ✅ All contributed code becomes property of Dylan Makori / Nomad Studios
- ✅ You grant Nomad Studios full rights to use, modify, and distribute your contributions
- ✅ You waive all ownership claims to your contributions
- ✅ Contributions are made under NSSAL v1.0 license terms

This ensures Nomad Studios can maintain the project sustainably while keeping the code source-available.

---

## 🐛 Reporting Bugs

Found a bug? Please report it!

1. Search [existing issues](https://github.com/currentsuspect/NOMAD/issues) first
2. Create a new issue with:
   - Clear title describing the bug
   - Steps to reproduce
   - Expected vs actual behavior
   - System information (OS, audio driver, etc.)
   - Logs or screenshots if applicable

See [Bug Reports Guide](docs/BUG_REPORTS.md) for detailed instructions.

---

## 💡 Suggesting Features

Have an idea for a new feature?

1. Check [Roadmap](docs/ROADMAP.md) to see if it's already planned
2. Search [existing issues](https://github.com/currentsuspect/NOMAD/issues) for similar requests
3. Open a new issue with:
   - Clear description of the feature
   - Use case and motivation
   - Potential implementation approach (if known)
   - Examples from other DAWs (if applicable)

---

## 🎓 Learning Resources

### For New Contributors
- **[Building Guide](docs/BUILDING.md)** — Set up your development environment
- **[Architecture Overview](docs/ARCHITECTURE.md)** — Understand system design
- **[Glossary](docs/GLOSSARY.md)** — Learn technical terminology
- **[FAQ](docs/FAQ.md)** — Common questions answered

### For Experienced Developers
- **[Debugging Guide](docs/developer/debugging.md)** — Use Tracy profiler and debugging tools
- **[Performance Tuning](docs/PERFORMANCE_TUNING.md)** — Optimize code for speed
- **[Style Guide](docs/STYLE_GUIDE.md)** — Write good documentation

---

## 📧 Communication

### GitHub
- **Issues** — Bug reports and feature requests
- **Pull Requests** — Code contributions
- **Discussions** — Questions and general discussion

### Email
For private inquiries: [makoridylan@gmail.com](mailto:makoridylan@gmail.com)

---

## 🔒 Security

Found a security vulnerability?

**DO NOT** open a public issue. Instead:
- Email: [makoridylan@gmail.com](mailto:makoridylan@gmail.com)
- Subject: "NOMAD Security Vulnerability"
- Include detailed description and reproduction steps

See [SECURITY.md](SECURITY.md) for our security policy.

---

## 📜 Scope & Licensing

### What's Public (Source-Available)
- ✅ Nomad Core audio engine
- ✅ NomadUI framework
- ✅ Platform abstraction layer
- ✅ Build system and tools

### What's Private
- ❌ Premium plugins and features
- ❌ Muse AI integration (future commercial)
- ❌ Build signing and distribution

### License
All contributions are licensed under **NSSAL v1.0** (Nomad Studios Source-Available License).

See [LICENSING.md](LICENSING.md) for full details.

---

## 🙏 Thank You!

Every contribution helps make Nomad better. Whether you're fixing typos, reporting bugs, or implementing features — thank you for being part of the Nomad community!

**Built by musicians, for musicians. Crafted with intention.** 🎵

---

**For detailed contribution guidelines, see [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md)**

*Last updated: January 2025*
