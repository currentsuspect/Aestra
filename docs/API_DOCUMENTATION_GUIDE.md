# 📘 API Documentation Guide

This guide walks you through generating, customizing, and maintaining the NOMAD DAW API documentation using Doxygen.

---

## 🎯 Quick Start

### Step 1: Install Prerequisites

#### Windows

**Option A: Using Chocolatey (Recommended)**
```powershell
# Install Chocolatey if not already installed
# (Run PowerShell as Administrator)
Set-ExecutionPolicy Bypass -Scope Process -Force
[System.Net.ServicePointManager]::SecurityProtocol = [System.Net.ServicePointManager]::SecurityProtocol -bor 3072
iex ((New-Object System.Net.WebClient).DownloadString('https://community.chocolatey.org/install.ps1'))

# Install Doxygen and Graphviz
choco install doxygen.install graphviz -y

# Verify installation
doxygen --version
dot -V
```

**Option B: Manual Installation**
1. Download Doxygen from: https://www.doxygen.nl/download.html
2. Download Graphviz from: https://graphviz.org/download/
3. Install both and add to PATH
4. Restart terminal and verify:
   ```powershell
   doxygen --version
   dot -V
   ```

#### macOS

```bash
# Using Homebrew
brew install doxygen graphviz

# Verify installation
doxygen --version
dot -V
```

#### Linux (Debian/Ubuntu)

```bash
# Install packages
sudo apt-get update
sudo apt-get install -y doxygen graphviz

# Verify installation
doxygen --version
dot -V
```

#### Linux (Fedora/RHEL)

```bash
# Install packages
sudo dnf install -y doxygen graphviz

# Verify installation
doxygen --version
dot -V
```

---

### Step 2: Generate Documentation

From the NOMAD project root directory:

```bash
# Generate API documentation
doxygen Doxyfile
```

This will:
- Parse all C++ header and source files
- Generate HTML documentation in `docs/api-reference/html/`
- Generate XML documentation in `docs/api-reference/xml/`
- Create call graphs and class diagrams (requires Graphviz)
- Log warnings to `doxygen_warnings.log`

**Expected output:**
```
Searching for include files...
Searching for files in directory c:/Users/Current/Documents/Projects/NOMAD/NomadCore
Searching for files in directory c:/Users/Current/Documents/Projects/NOMAD/NomadAudio
...
Generating docs for class Nomad::Audio::AudioDeviceManager...
...
lookup cache used 1234/65536 hits=5678 misses=90
finished...
```

---

### Step 3: View Documentation

**Windows:**
```powershell
# Open in default browser
start docs/api-reference/html/index.html

# Or use Invoke-Item
Invoke-Item docs\api-reference\html\index.html
```

**macOS:**
```bash
open docs/api-reference/html/index.html
```

**Linux:**
```bash
xdg-open docs/api-reference/html/index.html
```

---

## 📊 Understanding the Output

### Directory Structure

```
docs/api-reference/
├── html/               # HTML documentation (main output)
│   ├── index.html      # Main entry point
│   ├── annotated.html  # Class/struct list
│   ├── classes.html    # Class index
│   ├── files.html      # File list
│   ├── namespaces.html # Namespace list
│   └── ...
├── xml/                # XML output (for tooling)
│   ├── index.xml
│   └── ...
└── README.md           # This guide
```

### Key Documentation Pages

- **Main Page** (`index.html`) - Project overview and module list
- **Classes** (`classes.html`) - Alphabetical class index
- **Files** (`files.html`) - Source file documentation
- **Namespaces** (`namespaces.html`) - Namespace hierarchy
- **Related Pages** - Additional documentation pages

---

## 🔧 Configuration

The Doxygen configuration is stored in `Doxyfile` at the project root.

### Key Configuration Options

| Setting | Value | Description |
|---------|-------|-------------|
| `PROJECT_NAME` | "NOMAD DAW" | Project name in documentation |
| `OUTPUT_DIRECTORY` | `docs/api-reference` | Where docs are generated |
| `INPUT` | Multiple directories | Source directories to document |
| `RECURSIVE` | `YES` | Parse subdirectories |
| `EXTRACT_ALL` | `YES` | Document even undocumented items |
| `SOURCE_BROWSER` | `YES` | Include source code browser |
| `GENERATE_HTML` | `YES` | Generate HTML output |
| `GENERATE_XML` | `YES` | Generate XML for integration |
| `HAVE_DOT` | `YES` | Enable Graphviz diagrams |
| `CALL_GRAPH` | `YES` | Generate call graphs |
| `CLASS_GRAPH` | `YES` | Generate class hierarchy graphs |

### Customizing Configuration

Edit `Doxyfile` to customize:

```bash
# Open in your editor
code Doxyfile   # VS Code
vim Doxyfile    # Vim
notepad Doxyfile # Notepad
```

Common customizations:
- Change colors: `HTML_COLORSTYLE_HUE`, `HTML_COLORSTYLE_SAT`
- Add logo: `PROJECT_LOGO = path/to/logo.png`
- Exclude directories: Add to `EXCLUDE_PATTERNS`
- Disable diagrams: Set `HAVE_DOT = NO` (faster generation)

---

## 📝 Writing Documentation Comments

### Class Documentation

```cpp
/**
 * @brief Brief one-line description of the class
 * 
 * Detailed description explaining:
 * - Purpose of the class
 * - Main responsibilities
 * - Usage patterns
 * - Thread-safety considerations
 * 
 * Example usage:
 * @code
 * AudioDeviceManager manager;
 * manager.initialize();
 * auto devices = manager.getDevices();
 * @endcode
 * 
 * @note Important implementation notes
 * @warning Thread-safety warnings or other cautions
 * @see RelatedClass, relatedFunction()
 */
class AudioDeviceManager {
    // ...
};
```

### Function Documentation

```cpp
/**
 * @brief Brief description of what the function does
 * 
 * Detailed explanation of behavior, edge cases, and important notes.
 * 
 * @param deviceId The ID of the audio device to open
 * @param config Configuration settings for the stream
 * @param callback Audio processing callback function
 * @param userData Optional user data passed to callback
 * 
 * @return true if stream opened successfully, false otherwise
 * 
 * @throws std::invalid_argument if deviceId is invalid
 * @throws std::runtime_error if device fails to open
 * 
 * @note This function must be called after initialize()
 * @warning Callback is invoked from audio thread - avoid blocking operations
 * 
 * @see closeStream(), startStream()
 */
bool openStream(int deviceId, 
                const StreamConfig& config,
                AudioCallback callback,
                void* userData = nullptr);
```

### Member Variable Documentation

```cpp
class MyClass {
private:
    int m_sampleRate;        ///< Sample rate in Hz
    bool m_isRunning;        ///< Whether audio is actively processing
    
    /**
     * @brief Pre-allocated buffer for audio mixing
     * 
     * This buffer is allocated once during initialization to avoid
     * memory allocation in the audio callback thread.
     */
    std::vector<float> m_mixBuffer;
};
```

### Enum Documentation

```cpp
/**
 * @brief Audio driver backend types
 */
enum class DriverType {
    WASAPI_EXCLUSIVE,  ///< Windows WASAPI Exclusive Mode
    WASAPI_SHARED,     ///< Windows WASAPI Shared Mode
    ASIO,              ///< ASIO driver
    CORE_AUDIO,        ///< macOS Core Audio
    ALSA,              ///< Linux ALSA
    JACK               ///< JACK Audio Connection Kit
};
```

---

## ✅ Quality Assurance

### Check for Warnings

After generating documentation, review the warnings log:

```bash
# View all warnings
cat doxygen_warnings.log

# Windows PowerShell
Get-Content doxygen_warnings.log

# Count warnings by type
grep "undocumented" doxygen_warnings.log | wc -l
grep "documented param" doxygen_warnings.log | wc -l
```

### Common Warning Types

| Warning | Meaning | Fix |
|---------|---------|-----|
| `undocumented member` | Missing documentation | Add `/** @brief ... */` comment |
| `documented param 'x' not found` | Parameter name mismatch | Update param name in comment |
| `return type of member is not documented` | Missing `@return` | Add `@return` tag |
| `warning: no uniquely matching class member found` | Wrong function signature in docs | Update signature |

### Documentation Coverage Report

```bash
# Generate coverage statistics
doxygen Doxyfile 2>&1 | grep -E "(documented|undocumented)" | sort | uniq -c

# Or use the built-in statistics
grep "documented" doxygen_warnings.log | wc -l
```

---

## 🚀 Advanced Features

### Generating Call Graphs

Call graphs show function relationships (requires Graphviz):

```cpp
/**
 * @brief Process audio buffer
 * 
 * Call graph will show:
 * - Functions that call this
 * - Functions this calls
 */
void processAudio(float* buffer, int frames);
```

### Custom Pages

Add custom documentation pages by creating `.md` or `.dox` files:

```cpp
/**
 * @page audio_architecture Audio Architecture Overview
 * @brief High-level overview of the audio system
 * 
 * ## Audio Pipeline
 * 
 * The audio pipeline consists of:
 * - Device Manager
 * - Audio Engine
 * - DSP Processors
 * 
 * @see AudioDeviceManager, AudioEngine
 */
```

### Grouping Related Items

```cpp
/**
 * @defgroup audio_core Audio Core System
 * @brief Core audio engine components
 * @{
 */

class AudioEngine { /* ... */ };
class AudioProcessor { /* ... */ };

/** @} */ // end of audio_core group
```

### Code Examples with Syntax Highlighting

```cpp
/**
 * @brief Example function with code highlighting
 * 
 * @code{.cpp}
 * // Initialize audio system
 * AudioDeviceManager manager;
 * if (!manager.initialize()) {
 *     std::cerr << "Failed to initialize audio\n";
 *     return false;
 * }
 * 
 * // List available devices
 * auto devices = manager.getDevices();
 * for (const auto& device : devices) {
 *     std::cout << device.name << std::endl;
 * }
 * @endcode
 */
```

---

## 🌐 Integration & Deployment

### GitHub Pages Deployment

The GitHub Actions workflow (`.github/workflows/api-docs.yml`) automatically:
1. Generates documentation on every push
2. Deploys to GitHub Pages (main branch only)
3. Creates artifacts for download

Documentation will be available at:
```
https://<username>.github.io/NOMAD/api/
```

### Local Preview Server

To preview documentation locally with a web server:

```bash
# Python 3
cd docs/api-reference/html
python -m http.server 8000

# Then open: http://localhost:8000
```

### Integration with MkDocs

Link API docs from MkDocs markdown files:

```markdown
See the [AudioDeviceManager API](../../api-reference/html/class_nomad_1_1_audio_1_1_audio_device_manager.html) for details.
```

Or embed as iframe:

```html
<iframe src="../../api-reference/html/index.html" width="100%" height="600px"></iframe>
```

---

## 🛠️ Troubleshooting

### Doxygen Not Found

**Windows:**
```powershell
# Check if in PATH
where doxygen

# If not found, add to PATH:
$env:Path += ";C:\Program Files\doxygen\bin"

# Or permanently:
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\Program Files\doxygen\bin", "Machine")
```

**macOS/Linux:**
```bash
# Check if installed
which doxygen

# If not found, install or add to PATH
export PATH=$PATH:/usr/local/bin
```

### Graphviz Not Found

If diagrams aren't generated:

1. Verify Graphviz installation:
   ```bash
   dot -V
   ```

2. If not found, install or update `DOT_PATH` in `Doxyfile`:
   ```
   DOT_PATH = C:/Program Files/Graphviz/bin  # Windows
   DOT_PATH = /usr/local/bin                 # macOS/Linux
   ```

### Documentation Not Updating

1. Clean previous output:
   ```bash
   # Windows PowerShell
   Remove-Item -Recurse -Force docs\api-reference\html, docs\api-reference\xml
   
   # macOS/Linux
   rm -rf docs/api-reference/html docs/api-reference/xml
   ```

2. Regenerate:
   ```bash
   doxygen Doxyfile
   ```

### Empty Documentation

If documentation is empty or missing classes:

1. Check `INPUT` paths in `Doxyfile` are correct
2. Verify files have proper extensions (`.h`, `.hpp`, `.cpp`)
3. Check `EXCLUDE_PATTERNS` isn't too aggressive
4. Ensure source files are accessible (not in excluded directories)

### Performance Issues

If generation is slow:

1. Disable call graphs temporarily:
   ```
   CALL_GRAPH = NO
   CALLER_GRAPH = NO
   ```

2. Disable source browser:
   ```
   SOURCE_BROWSER = NO
   ```

3. Reduce `DOT_GRAPH_MAX_NODES`:
   ```
   DOT_GRAPH_MAX_NODES = 25
   ```

---

## 📚 Resources

### Official Documentation
- **Doxygen Manual**: https://www.doxygen.nl/manual/
- **Doxygen Tags Reference**: https://www.doxygen.nl/manual/commands.html
- **Graphviz**: https://graphviz.org/documentation/

### Examples
- **Good Examples**: Look at well-documented open-source projects
  - LLVM: https://llvm.org/doxygen/
  - OpenCV: https://docs.opencv.org/
  - Boost: https://www.boost.org/doc/libs/

### NOMAD Specific
- [Coding Style Guide](developer/coding-style.md)
- [Contributing Guidelines](../CONTRIBUTING.md)
- [Architecture Documentation](architecture/)

---

## 🤝 Contributing to Documentation

When contributing code:

1. ✅ Document all public APIs
2. ✅ Use Doxygen-compatible syntax
3. ✅ Include usage examples for complex APIs
4. ✅ Add `@param` for all parameters
5. ✅ Add `@return` for return values
6. ✅ Note thread-safety considerations
7. ✅ Run `doxygen Doxyfile` and fix warnings
8. ✅ Review generated docs for clarity

### Pre-Commit Checklist

```bash
# 1. Generate docs
doxygen Doxyfile

# 2. Check for warnings
cat doxygen_warnings.log

# 3. Review your documented items
# Open docs/api-reference/html/index.html and navigate to your classes

# 4. Fix any issues and regenerate
doxygen Doxyfile
```

---

## 📊 Documentation Metrics

Track documentation coverage over time:

```bash
# Count total classes
find NomadCore NomadAudio NomadPlat NomadUI -name "*.h" | xargs grep -h "^class\|^struct" | wc -l

# Count documented classes (with Doxygen comments)
find NomadCore NomadAudio NomadPlat NomadUI -name "*.h" | xargs grep -B1 "^class\|^struct" | grep "/\*\*" | wc -l

# Calculate coverage percentage
# (documented / total) * 100
```

---

**Happy Documenting! 📚✨**

For questions or issues, see [SUPPORT.md](../SUPPORT.md) or open a [GitHub Discussion](https://github.com/currentsuspect/NOMAD/discussions).
