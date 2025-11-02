# 📚 NOMAD DAW - API Reference

This directory contains the automatically generated API documentation using Doxygen.

## 🎯 What's Inside

The API reference provides detailed documentation for:

- **NomadCore** - Foundation utilities (math, threading, logging, file I/O)
- **NomadAudio** - Audio engine and device management
- **NomadPlat** - Platform abstraction layer (window, input, dialogs)
- **NomadUI** - GPU-accelerated UI framework
- **Source** - Main application code

## 🚀 Generating Documentation

### Prerequisites

1. **Install Doxygen**
   - Windows: Download from [doxygen.nl](https://www.doxygen.nl/download.html) or use `choco install doxygen.install`
   - macOS: `brew install doxygen`
   - Linux: `sudo apt-get install doxygen` or `sudo dnf install doxygen`

2. **Install Graphviz** (optional, for diagrams)
   - Windows: Download from [graphviz.org](https://graphviz.org/download/) or use `choco install graphviz`
   - macOS: `brew install graphviz`
   - Linux: `sudo apt-get install graphviz` or `sudo dnf install graphviz`

### Generate Documentation Locally

From the root directory of the project:

```bash
# Generate API documentation
doxygen Doxyfile

# Open the generated documentation
# Windows
start docs/api-reference/html/index.html

# macOS
open docs/api-reference/html/index.html

# Linux
xdg-open docs/api-reference/html/index.html
```

### Configuration

The Doxygen configuration is stored in `Doxyfile` at the project root.

Key settings:
- **Output**: `docs/api-reference/html/`
- **Format**: HTML with SVG diagrams
- **XML Output**: Enabled for tooling integration
- **Source Browser**: Enabled with cross-references
- **Call Graphs**: Enabled (requires Graphviz)

## 📖 Documentation Style

We follow these conventions for API documentation:

### Class/Struct Documentation

```cpp
/**
 * @brief Brief description (one line)
 * 
 * Detailed description explaining the purpose,
 * usage patterns, and important notes.
 * 
 * @note Important implementation details
 * @warning Potential pitfalls or thread-safety concerns
 */
class MyClass {
    // ...
};
```

### Function Documentation

```cpp
/**
 * @brief Brief description of what the function does
 * 
 * @param paramName Description of the parameter
 * @param anotherParam Description of another parameter
 * @return Description of the return value
 * 
 * @note Additional notes about usage
 * @throws std::exception Exception conditions
 * 
 * Example:
 * @code
 * MyClass obj;
 * obj.myFunction(42, "test");
 * @endcode
 */
int myFunction(int paramName, const std::string& anotherParam);
```

### Member Variable Documentation

```cpp
int m_count;  ///< Brief description of the member variable
```

## 🔗 Integration with MkDocs

The generated API documentation can be integrated into the main MkDocs site.

To link API docs in MkDocs pages:

```markdown
See the [AudioDeviceManager API](../api-reference/html/class_nomad_1_1_audio_1_1_audio_device_manager.html) for details.
```

## 🛠️ Maintenance

### Updating Documentation

1. Add/update Doxygen comments in header files
2. Regenerate documentation: `doxygen Doxyfile`
3. Review warnings in `doxygen_warnings.log`
4. Fix any undocumented items

### Quality Checks

The Doxygen configuration is set to:
- ✅ Warn about undocumented members
- ✅ Warn about documentation errors
- ✅ Warn about missing parameter documentation
- ✅ Log all warnings to `doxygen_warnings.log`

Review this log file regularly to ensure documentation quality.

## 📊 Coverage

To check documentation coverage:

```bash
# Generate docs with warnings
doxygen Doxyfile

# Review warnings log
cat doxygen_warnings.log | grep "undocumented" | wc -l
```

## 🎨 Customization

### Styling

To customize the HTML output appearance:

1. Edit `HTML_EXTRA_STYLESHEET` in `Doxyfile`
2. Create custom CSS file: `docs/doxygen-custom.css`
3. Regenerate documentation

### Header/Footer

To add custom header/footer:

1. Generate default templates:
   ```bash
   doxygen -w html header.html footer.html stylesheet.css
   ```
2. Edit the generated files
3. Update `HTML_HEADER` and `HTML_FOOTER` in `Doxyfile`

## 🌐 Online Hosting

The API documentation can be hosted on:

- **GitHub Pages** (via GitHub Actions workflow)
- **ReadTheDocs** (alongside MkDocs)
- **Custom server** (deploy `docs/api-reference/html/` directory)

See `.github/workflows/docs.yml` for automated deployment setup.

## 📝 Best Practices

1. **Document public APIs first** - Focus on user-facing interfaces
2. **Keep it current** - Update docs when changing APIs
3. **Use examples** - Include code snippets for complex APIs
4. **Cross-reference** - Link related classes and functions
5. **Check warnings** - Aim for zero undocumented warnings

## 🔍 Searching

The generated documentation includes:
- **Built-in search** - Client-side JavaScript search
- **Index pages** - Alphabetical class/function index
- **Call graphs** - Visual function relationships
- **Class diagrams** - Inheritance hierarchies

## 🤝 Contributing

When adding new code:

1. ✅ Document all public classes, functions, and members
2. ✅ Use Doxygen-compatible comment syntax
3. ✅ Include usage examples for complex APIs
4. ✅ Run `doxygen Doxyfile` and check for warnings
5. ✅ Review generated docs for clarity

See [CODING_STYLE.md](../docs/developer/coding-style.md) for detailed documentation guidelines.

---

**Generated by Doxygen** | **NOMAD DAW** © 2025 Nomad Studios
