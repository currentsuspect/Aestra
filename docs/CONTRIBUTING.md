# 🤝 Contributing to Nomad DAW

![Contributions Welcome](https://img.shields.io/badge/Contributions-Welcome-brightgreen)
![PRs Welcome](https://img.shields.io/badge/PRs-Welcome-blue)

Thank you for your interest in contributing to Nomad DAW! This guide will help you understand our development workflow and contribution process.

## 📋 Table of Contents

- [Code of Conduct](#-code-of-conduct)
- [Getting Started](#-getting-started)
- [Development Workflow](#-development-workflow)
- [Branching Strategy](#-branching-strategy)
- [Pull Request Process](#-pull-request-process)
- [Contribution Guidelines](#-contribution-guidelines)
- [Contributor License Agreement](#-contributor-license-agreement)

## 📜 Code of Conduct

We are committed to providing a welcoming and inclusive environment. Please read our [Code of Conduct](../CODE_OF_CONDUCT.md) before contributing.

## 🚀 Getting Started

### Prerequisites

1. **Set up your development environment**
   - Follow the [Building Guide](BUILDING.md) to set up your system
   - Install Git, CMake, and a C++17 compatible compiler
   - Configure your IDE with the project settings

2. **Fork and clone the repository**
   ```bash
   # Fork the repository on GitHub first, then:
   git clone https://github.com/YOUR_USERNAME/NOMAD.git
   cd NOMAD
   
   # Add upstream remote
   git remote add upstream https://github.com/currentsuspect/NOMAD.git
   ```

3. **Install Git hooks**
   ```powershell
   # Windows PowerShell
   pwsh -File scripts/install-hooks.ps1
   ```
   ```bash
   # Linux
   bash scripts/install-hooks.sh
   ```

4. **Build the project**
   ```bash
   cmake -S . -B build -DNOMAD_CORE_MODE=ON -DCMAKE_BUILD_TYPE=RelWithDebInfo
   cmake --build build --config RelWithDebInfo --parallel
   ```

## 🔄 Development Workflow

### 1. Find an Issue or Feature

- Browse [open issues](https://github.com/currentsuspect/NOMAD/issues)
- Check the [project roadmap](ROADMAP.md) for planned features
- Look for issues labeled `good first issue` or `help wanted`
- Comment on an issue to express interest before starting work

### 2. Create a Feature Branch

```bash
# Update your fork
git checkout main
git pull upstream main

# Create a new branch
git checkout -b feature/your-feature-name
# or
git checkout -b fix/bug-description
```

### 3. Make Your Changes

- Write clean, maintainable code following our [Coding Style Guide](CODING_STYLE.md)
- Add unit tests for new functionality
- Update documentation as needed
- Keep commits small and focused
- Write descriptive commit messages

### 4. Test Your Changes

```bash
# Build and test
cmake --build build --config RelWithDebInfo
./build/NomadDAW  # Test manually

# Run unit tests (if available)
ctest --test-dir build --config RelWithDebInfo
```

### 5. Commit Your Changes

```bash
# Stage your changes
git add .

# Commit with a descriptive message
git commit -m "feat: add sample drag-and-drop functionality"
```

**Commit message format:**
- `feat:` - New feature
- `fix:` - Bug fix
- `docs:` - Documentation changes
- `style:` - Code style changes (formatting, etc.)
- `refactor:` - Code refactoring
- `test:` - Adding or updating tests
- `chore:` - Maintenance tasks

### 6. Push and Create Pull Request

```bash
# Push to your fork
git push origin feature/your-feature-name

# Create a pull request on GitHub
```

## 🌿 Branching Strategy

### Main Branches

- **`main`** - Production-ready code, stable releases
- **`develop`** - Active development, integration branch
- **`feature/*`** - New features (e.g., `feature/sample-editor`)
- **`fix/*`** - Bug fixes (e.g., `fix/audio-glitch`)
- **`docs/*`** - Documentation updates (e.g., `docs/api-reference`)

### Branch Naming Conventions

```bash
feature/short-description    # New features
fix/bug-description          # Bug fixes
docs/documentation-update    # Documentation
refactor/component-name      # Code refactoring
test/test-description        # Test additions
```

### Workflow

```
main
  ↑
  └─ develop
       ├─ feature/new-feature
       ├─ fix/bug-fix
       └─ docs/update-docs
```

## 🔀 Pull Request Process

### Before Creating a PR

✅ **Checklist:**
- [ ] Code follows the [Coding Style Guide](CODING_STYLE.md)
- [ ] All tests pass locally
- [ ] No compiler warnings or errors
- [ ] Documentation is updated
- [ ] Commit messages are clear and descriptive
- [ ] Branch is up-to-date with `develop`

### Creating a Pull Request

1. **Fill out the PR template completely**
   - Describe what your PR does
   - Reference related issues (e.g., "Closes #123")
   - List any breaking changes
   - Add screenshots for UI changes

2. **PR title format:**
   ```
   feat: Add sample drag-and-drop functionality
   fix: Resolve audio timing issue in WASAPI driver
   docs: Update architecture documentation
   ```

3. **Request review from maintainers**

### PR Review Process

1. **Automated checks** run on your PR:
   - Build verification (Windows/Linux)
   - Code style validation (clang-format)
   - Security scanning (Gitleaks)
   - Unit tests (if available)

2. **Manual code review** by maintainers:
   - Code quality and architecture
   - Adherence to guidelines
   - Testing coverage
   - Documentation completeness

3. **Address review feedback:**
   ```bash
   # Make changes based on feedback
   git add .
   git commit -m "fix: address review feedback"
   git push origin feature/your-feature-name
   ```

4. **Approval and merge:**
   - Once approved, maintainers will merge your PR
   - PR is typically merged into `develop` first
   - Regular releases merge `develop` into `main`

## 📝 Contribution Guidelines

### Code Quality

- **Follow the coding style**: Use clang-format (see [CODING_STYLE.md](CODING_STYLE.md))
- **Write clean code**: Clear variable names, proper indentation, minimal complexity
- **Add comments**: Explain complex logic and algorithms
- **Avoid code duplication**: Refactor common patterns into reusable functions
- **Handle errors**: Proper error handling and validation

### Testing

- **Write unit tests**: For new functionality when possible
- **Manual testing**: Test your changes thoroughly before submitting
- **Edge cases**: Consider and test edge cases and error conditions
- **Performance**: Ensure changes don't introduce performance regressions

### Documentation

- **Update documentation**: Keep docs in sync with code changes
- **Comment your code**: Add docstrings for public APIs
- **Add examples**: Include usage examples for new features
- **Update README**: If adding major features, update the main README

### Security

- **No secrets in code**: Never commit API keys, passwords, or certificates
- **Validate inputs**: Always validate user input and external data
- **Follow best practices**: Use secure coding practices
- **Report vulnerabilities**: See [SECURITY.md](../SECURITY.md)

### What NOT to Commit

❌ **Do NOT commit:**
- Large binary files or assets (use Git LFS if needed)
- Private or premium modules (kept in private repositories)
- Compiled binaries or build artifacts
- IDE-specific files (except shared configurations)
- Personal configuration files
- Secrets, keys, or credentials

## 📄 Contributor License Agreement

**By submitting a pull request, you agree that:**

1. All contributed code becomes property of Dylan Makori / Nomad DAW
2. Dylan Makori has full rights to use, modify, and distribute your contributions
3. You waive all ownership claims to your contributions
4. You grant Dylan Makori a perpetual, worldwide, exclusive license
5. Your contributions can be used in the proprietary Nomad DAW product

**Why this matters:**
- Nomad DAW is proprietary commercial software
- We need clear ownership to maintain the product
- Contributors are credited in the project
- This allows us to build a sustainable business while being transparent

## 🏆 Recognition

We value all contributors! Contributors are recognized:

- **GitHub Contributors** - Listed on the repository contributors page
- **Release Notes** - Mentioned in release changelogs
- **Hall of Fame** - Top contributors featured in documentation
- **Community Credits** - Special recognition for significant contributions

## 💡 Scope of Contributions

### Public Contributions (nomad-core)

✅ **You can contribute to:**
- Core audio engine improvements
- UI framework enhancements
- Bug fixes and optimizations
- Documentation improvements
- Unit tests and integration tests
- Build system improvements
- Cross-platform compatibility

### Private Modules (Not in Public Repo)

❌ **These are developed privately:**
- Premium plugins and effects
- AI/ML models (Muse integration internals)
- Proprietary audio algorithms
- Commercial licensing system
- Code signing and distribution

**Note**: Public contributors work on `nomad-core/` with mock assets. Full builds with premium features are only available internally.

## 🛠️ Development Tools

### Required Tools

- **clang-format** - Code formatting (automatic with Git hooks)
- **CMake** - Build system
- **Git** - Version control
- **C++17 compiler** - MSVC (Windows) or GCC/Clang (Linux)

### Recommended Tools

- **Visual Studio 2022** - IDE for Windows development
- **VS Code** - Cross-platform editor with C++ extensions
- **CLion** - JetBrains IDE with CMake support
- **Git GUI** - GitKraken, SourceTree, or GitHub Desktop

## 📚 Additional Resources

- [Building Guide](BUILDING.md) - How to build Nomad
- [Coding Style Guide](CODING_STYLE.md) - Code formatting rules
- [Architecture Overview](ARCHITECTURE.md) - System design
- [FAQ](FAQ.md) - Common questions
- [Glossary](GLOSSARY.md) - Technical terms

## 🆘 Getting Help

**Questions about contributing?**

- 💬 Comment on relevant GitHub issues
- 📧 Email: makoridylan@gmail.com
- 🐛 Report bugs: [GitHub Issues](https://github.com/currentsuspect/NOMAD/issues)

## 🙏 Thank You!

Your contributions help make Nomad DAW better for everyone. We appreciate your time, effort, and passion for building great software!

---

[← Return to Nomad Docs Index](README.md)
