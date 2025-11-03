# Contributing to **Nomad DAW**

Welcome to **Nomad DAW** — we’re thrilled you’re interested in helping shape the most lightweight, native audio workstation out there.
This guide explains how to report issues, request features, and contribute code in a way that keeps Nomad stable and fast.

---

## 🌐 Code of Conduct

All contributors are expected to follow our [Code of Conduct](CODE_OF_CONDUCT.md).
Be respectful, patient, and collaborative — Nomad thrives on open creativity.

---

## 🐛 Reporting Bugs

1. **Search first** in the [issue tracker](https://github.com/currentsuspect/NOMAD/issues) to avoid duplicates.
2. If it’s new, create an issue with:

   * A clear, descriptive title.
   * Steps to reproduce the bug.
   * Expected vs. actual behavior.
   * OS / build type / hardware info (CPU, RAM, GPU).
3. Attach screenshots, logs, or crash dumps if available.

---

## 💡 Requesting Features

1. Open an issue and tag it with **`feature-request`**.
2. Describe the problem your feature solves.
3. If possible, include:

   * Design ideas, mockups, or workflow sketches.
   * Example use-cases (e.g., “faster audio preview,” “GPU-optimized waveform”).

---

## 🧩 Contributing Code

1. **Fork** the repository and create a new branch for your changes.

   ```
   git checkout -b feature/my-feature-name
   ```
2. Follow Nomad’s coding conventions (see below).
3. Add tests or sample projects if relevant.
4. Ensure the build passes on Windows (and optionally Linux/macOS).
5. Submit a **Pull Request** with:

   * A short summary of what changed and why.
   * Screenshots or short demo clips if UI-related.

When you submit code, you grant Nomad Studios the right to use, modify, and distribute it under the **Nomad Studios Source-Available License (NSSAL)**.

---

## 🧱 Development Setup

### Prerequisites

* C++17-compatible compiler (MSVC, Clang, or GCC).
* CMake 3.15+
* Git
* Dependencies (fetched automatically during build):

  * RtAudio
  * NanoSVG
  * Vulkan SDK or OpenGL (for graphics backend)

### Build Instructions

```bash
git clone https://github.com/nomad-studios/nomad-daw.git
cd nomad-daw
mkdir build && cd build
cmake ..
cmake --build .
```

Optional: run `NomadDAW.exe` from `build/Release`.

---

## 🧠 Code Style Guidelines

* Follow the existing file and class naming conventions.
* Use **PascalCase** for classes, **camelCase** for functions/variables.
* Keep functions short and focused — one responsibility each.
* Add brief comments for non-obvious logic.
* Avoid unnecessary heap allocations inside real-time audio threads.
* Commit messages: present-tense (“Add”, “Fix”, “Refactor”).
* Always test before pushing.

---

## 🧾 License Notice

By contributing, you acknowledge that:

* Your code becomes part of the Nomad DAW project.
* Contributions are governed by the **Nomad Studios Source-Available License (NSSAL) v1.0**.
* You retain authorship credit in commit history, but ownership transfers to Nomad Studios for unified IP protection.

---

## ❤️ Thank You

Every contribution — big or small — keeps Nomad moving toward its goal of **fluid, native, and free creative software**.
If you build something cool with it, tag us or drop a message in the community — we love seeing what creators make.