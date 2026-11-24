# 📘 Aestra — Documentation Portal

![Aestra Version](https://img.shields.io/badge/Aestra-0.x%20pre--beta-blue)
![License](https://img.shields.io/badge/License-ASSAL%20v1.1-blue)
![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux-lightgrey)
![C++](https://img.shields.io/badge/C%2B%2B-17-orange)

Welcome to the **Aestra Documentation Portal**! This is your guide to understanding, building, validating, and contributing to Aestra.

The public repository is currently on a **0.x pre-beta line**. The working roadmap target is **v1 Beta in December 2026**.

## 📌 Current docs posture

Some documents in this repo are active engineering references, while others are preserved historical notes.
The documents below describe the code as it stands today:

- [Changelog](../CHANGELOG.md) — current through v0.8.2-alpha
- [Testing & CI](technical/testing_ci.md) — test profiles, and what the public CI gate runs
- [Engineering health](technical/engineering-health.md) — the properties a change must not break
- [Audio engine ownership](technical/audio/AUDIO_ENGINE_OWNERSHIP.md) — who owns an `AudioEngine`, and who only borrows one
- [Pan law policy](technical/audio/PAN_LAW_POLICY.md) — FD-64, as the code applies it

Dated, and not a description of current state: the [Roadmap](technical/roadmap.md) is an internal memo
from March 2026 and says of itself that its claims are based on repo state as of January 2026. Read it
for direction. For what exists today, read the code, the tests, and the [changelog](../CHANGELOG.md).

## 🎯 What is Aestra?

Aestra is a source-available digital audio workstation under active development. The public tree includes the native audio engine, custom UI framework, platform layer, tests, and contributor docs.

## 📚 Documentation Index

### 🚀 Getting Started

- **[Building Guide](getting-started/building.md)** — Complete build instructions for Windows and Linux
- **[Validate Public Core Build](getting-started/validate-core-build.md)** — Smoke-test the public/core configuration
- **[Contributing Guide](developer/contributing.md)** — How to contribute to Aestra (GitHub workflow, PR rules)
- **[FAQ](technical/faq.md)** — Frequently asked questions for contributors and users

### 🏗️ Architecture & Design

- **[Architecture Overview](architecture/overview.md)** — Aestra's modular architecture (Core, UI, Audio, Platform)
- **[Engineering Health](technical/engineering-health.md)** — The properties a change must not break
- **[Audio Engine Ownership](technical/audio/AUDIO_ENGINE_OWNERSHIP.md)** — Engine ownership contract, with known residuals
- **[Pan Law Policy](technical/audio/PAN_LAW_POLICY.md)** — FD-64 as the signal path applies it
- **[Coding Style Guide](developer/coding-style.md)** — Code conventions, formatting rules, and best practices
- **[Glossary](technical/glossary.md)** — Technical terms and definitions

### 🐛 Development & Debugging

- **[Bug Reports Guide](developer/bug-reports.md)** — How to report bugs effectively with reproduction steps
- **[Debugging Guide](developer/debugging.md)** — Using Aestra profiler, Visual Studio debugger, and logging
- **[Performance Tuning](developer/performance-tuning.md)** — FPS optimization, latency reduction, and profiling
- **[Style Guide](developer/style-guide.md)** — Documentation and comment standards

### 📋 Project Management

- **[Roadmap](technical/roadmap.md)** — Direction memo (March 2026); not a description of current state
- **[Testing & CI](technical/testing_ci.md)** — Current confidence suite and verification workflow
- **[CI Workflows Map](technical/ci_workflows.md)** — What runs in GitHub Actions, what is blocking vs preview
- **[Changelog](../CHANGELOG.md)** — Current milestone history, through v0.8.2-alpha
- **[License Reference](about/license-reference.md)** — Licensing information and ASSAL v1.1 details

### 📝 Templates

- **[Issue Template](TEMPLATE/ISSUE_TEMPLATE.md)** — Template for documentation-related issues
- **[PR Template](TEMPLATE/PR_TEMPLATE.md)** — Template for documentation pull requests
- **[Documentation Section Template](TEMPLATE/DOC_SECTION.md)** — Base structure for new documentation

## 🎓 Quick Links

### For New Contributors
1. Start with [FAQ](technical/faq.md) to understand common questions
2. Read [Contributing Guide](developer/contributing.md) to learn our workflow
3. Check [Building Guide](getting-started/building.md) to set up your development environment
4. Review [Coding Style Guide](developer/coding-style.md) before writing code

### For Developers
1. Understand [Architecture Overview](architecture/overview.md) to grasp system design
2. Reference [Glossary](technical/glossary.md) for technical terminology
3. Follow [Coding Style Guide](developer/coding-style.md) for consistent code quality
4. Use [Debugging Guide](developer/debugging.md) and [Performance Tuning](developer/performance-tuning.md) for optimization

### For Bug Reporters
1. Read [Bug Reports Guide](developer/bug-reports.md) for effective bug reporting
2. Follow the bug report template with clear reproduction steps
3. Include logs, screenshots, and system information

### For Maintainers
1. Review [Roadmap](technical/roadmap.md) for project direction (March 2026 memo)
2. Check the [Changelog](../CHANGELOG.md) for recent milestone history
3. Check [License Reference](about/license-reference.md) for ASSAL v1.1 details
4. Use [Issue Template](TEMPLATE/ISSUE_TEMPLATE.md) for documentation issues

## 🌟 Key Features

### Audio Engine
- **Native backend work** - WASAPI, ASIO, RtAudio, playback, recording, and export live in one codebase
- **Headless test paths** - core audio workflows can be exercised without the full UI
- **Sample-accurate timing work** - timing-sensitive paths are under active engineering and regression coverage

### User Interface
- **AestraUI Framework** - Custom renderer, widgets, and theming system
- **Timeline and piano roll surfaces** - actively maintained as part of the public repo
- **Contributor-visible UI stack** - no separate proprietary front-end layer is required to work on the app

### Development
- **Modern C++ (C++17)** - Clean, maintainable codebase
- **CMake Build System** - Cross-platform build configuration
- **Git Hooks** - Pre-commit validation for code quality
- **GitHub Actions** - maintained build, test, docs, and public-tree validation workflows

## 🔧 Development Tools

- **CMake** - Build system configuration
- **clang-format** - Automated code formatting
- **Git hooks** - Pre-commit validation
- **Gitleaks** - Security scanning for secrets
- **Visual Studio 2022** - Primary IDE for Windows development

## 📖 External Resources

- **[GitHub Repository](https://github.com/currentsuspect/Aestra)** - Main source code repository
- **[Issues Tracker](https://github.com/currentsuspect/Aestra/issues)** - Bug reports and feature requests
- **[Project Board](https://github.com/currentsuspect/Aestra/projects)** - Development roadmap and tasks

## 💡 Contributing

We welcome contributions from the community! Before contributing:

1. Read our [Contributing Guide](developer/contributing.md)
2. Check the [changelog](../CHANGELOG.md) for what has landed, and the [roadmap](technical/roadmap.md) for direction
3. Review existing issues and discussions
4. Follow our [Coding Style Guide](developer/coding-style.md)

**Note**: By contributing to Aestra, you agree your contribution is handled under the repository licensing terms described in the contributing guide.

## 📧 Contact & Support

- **Email**: support@aestra.studio
- **GitHub Issues**: [Report bugs or request features](https://github.com/currentsuspect/Aestra/issues)
- **Documentation Issues**: Use our [Issue Template](TEMPLATE/ISSUE_TEMPLATE.md)

## ⚖️ License

**Aestra** is licensed under the **Aestra Studios Source-Available License (ASSAL) v1.1**. See [License Reference](about/license-reference.md) for full details.

**Key Points:**
- ✅ Source code is publicly visible for educational purposes
- ✅ You can study, report bugs, and suggest improvements
- ✅ You can submit pull requests (contributors grant all rights to Aestra Studios)
- ❌ You cannot use, copy, modify, or redistribute the code without permission

The source code is publicly visible for transparency but is **NOT open-source**. All rights reserved.
