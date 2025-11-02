# 🎯 Steps to Generate NOMAD API Documentation

## ✅ Current Status
- ✅ Pulled latest changes from origin/develop
- ✅ Created Doxygen configuration (`Doxyfile`)
- ✅ Created documentation guides and READMEs
- ✅ Created automation scripts (PowerShell & Batch)
- ✅ Set up GitHub Actions workflow for CI/CD
- ✅ Updated .gitignore for Doxygen output
- ✅ Updated main README with API docs section

---

## 📋 Prerequisites Installation

### Step 1: Install Doxygen

#### Windows (Choose One Method)

**Method A: Using Chocolatey (Recommended)**
```powershell
# Run PowerShell as Administrator
choco install doxygen.install -y

# Verify installation
doxygen --version
```

**Method B: Manual Installation**
1. Download from: https://www.doxygen.nl/download.html
2. Run installer (choose "Add to PATH" option)
3. Restart terminal
4. Verify: `doxygen --version`

### Step 2: Install Graphviz (Optional - For Diagrams)

#### Windows
```powershell
# Using Chocolatey
choco install graphviz -y

# Verify installation
dot -V
```

#### Alternative: Manual Installation
- Download from: https://graphviz.org/download/
- Install and add to PATH
- Restart terminal
- Verify: `dot -V`

---

## 🚀 Generate API Documentation

### Option 1: Easy Way (Batch File)

Simply double-click or run:
```cmd
generate-api-docs.bat
```

### Option 2: PowerShell (Recommended)

```powershell
# Generate and automatically open in browser
.\generate-api-docs.ps1 generate -Open

# Or just generate
.\generate-api-docs.ps1 generate

# View statistics about documentation
.\generate-api-docs.ps1 stats

# Clean previous documentation
.\generate-api-docs.ps1 clean

# Get help
.\generate-api-docs.ps1 help
```

### Option 3: Direct Doxygen Command

```powershell
# From project root directory
doxygen Doxyfile
```

---

## 📂 Where to Find Generated Documentation

After running the generation command, your documentation will be at:

```
docs/api-reference/html/index.html
```

### Open in Browser

**Windows:**
```powershell
# Method 1: PowerShell
start docs\api-reference\html\index.html

# Method 2: Using the script
.\generate-api-docs.ps1 view

# Method 3: Double-click the file in File Explorer
```

---

## 📊 Understanding the Output

### Main Documentation Pages

1. **Main Page** (`index.html`)
   - Project overview
   - Module list
   - Quick navigation

2. **Classes** (`classes.html`)
   - Alphabetical index of all classes
   - Quick access to class documentation

3. **Namespaces** (`namespaces.html`)
   - Namespace hierarchy
   - Module organization

4. **Files** (`files.html`)
   - Source file browser
   - File-level documentation

### Interactive Features

- 🔍 **Search Bar** - Full-text search across all documentation
- 🔗 **Cross-References** - Click to navigate between related items
- 📊 **Call Graphs** - Visual function call relationships
- 🌳 **Class Diagrams** - Inheritance hierarchies
- 💻 **Source Browser** - Syntax-highlighted source code

---

## 🔍 Reviewing Documentation Quality

### Step 1: Check Warnings Log

```powershell
# View all warnings
Get-Content doxygen_warnings.log

# Count warnings
(Get-Content doxygen_warnings.log).Count

# Or use the script
.\generate-api-docs.ps1 stats
```

### Step 2: Understand Warning Types

| Warning Type | Meaning | Action |
|--------------|---------|--------|
| `undocumented member` | Missing documentation | Add `/** @brief ... */` comment |
| `documented param 'x' not found` | Parameter name mismatch | Fix parameter name in docs |
| `return type ... not documented` | Missing `@return` tag | Add return documentation |

### Step 3: Quality Assessment

**Excellent Quality (Target):**
- ✅ < 50 undocumented items
- ✅ All public APIs documented
- ✅ Examples provided for complex APIs

**Good Quality:**
- ⚠️ 50-100 undocumented items
- ⚠️ Most public APIs documented
- ⚠️ Some examples provided

**Needs Improvement:**
- ❌ > 100 undocumented items
- ❌ Many public APIs undocumented
- ❌ Few or no examples

---

## 📝 Improving Documentation

### For Classes

Add documentation above class definition:

```cpp
/**
 * @brief Brief one-line description
 * 
 * Detailed description explaining purpose,
 * usage patterns, and important notes.
 * 
 * Example usage:
 * @code
 * MyClass obj;
 * obj.initialize();
 * @endcode
 * 
 * @note Thread-safety: Not thread-safe
 * @see RelatedClass
 */
class MyClass {
    // ...
};
```

### For Functions

Add documentation above function:

```cpp
/**
 * @brief Process audio buffer
 * 
 * @param buffer Pointer to audio buffer
 * @param frames Number of frames to process
 * @return true if successful, false otherwise
 * 
 * @note Called from audio thread - must be real-time safe
 */
bool processAudio(float* buffer, int frames);
```

### For Member Variables

```cpp
int m_sampleRate;  ///< Sample rate in Hz (e.g., 44100, 48000)
```

After adding documentation:
1. Save files
2. Re-run: `.\generate-api-docs.ps1 generate`
3. Check improvements: `.\generate-api-docs.ps1 stats`

---

## 🌐 Publishing Documentation

### Automatic GitHub Pages Deployment

Documentation is automatically published when you push to the `main` branch:

1. **Commit your changes:**
   ```powershell
   git add .
   git commit -m "docs: set up Doxygen API documentation"
   ```

2. **Push to develop first:**
   ```powershell
   git push origin develop
   ```

3. **Merge to main** (via PR or direct push)
   ```powershell
   git checkout main
   git merge develop
   git push origin main
   ```

4. **Wait for GitHub Actions** (takes ~5 minutes)
   - Check: https://github.com/currentsuspect/NOMAD/actions

5. **Access published docs at:**
   ```
   https://currentsuspect.github.io/NOMAD/api/
   ```

### Manual Deployment Trigger

From GitHub:
1. Go to repository
2. Click **Actions** tab
3. Select **Generate API Documentation** workflow
4. Click **Run workflow** dropdown
5. Select `main` branch
6. Click **Run workflow** button

---

## 🔄 Regenerating Documentation

### When to Regenerate

Regenerate documentation whenever you:
- ✅ Add new classes or functions
- ✅ Modify function signatures
- ✅ Update documentation comments
- ✅ Want to check current quality

### How to Regenerate

```powershell
# Clean and regenerate
.\generate-api-docs.ps1 clean
.\generate-api-docs.ps1 generate -Open

# Or in one step
Remove-Item -Recurse -Force docs\api-reference\html, docs\api-reference\xml; doxygen Doxyfile
```

---

## 🛠️ Troubleshooting

### Problem: "Doxygen not found"

**Solution:**
```powershell
# Check if installed
doxygen --version

# If not found, install
choco install doxygen.install -y

# Verify PATH
$env:Path -split ';' | Select-String "doxygen"

# Add to PATH if needed (restart terminal after)
[Environment]::SetEnvironmentVariable("Path", $env:Path + ";C:\Program Files\doxygen\bin", "User")
```

### Problem: No diagrams in documentation

**Solution:**
```powershell
# Check Graphviz
dot -V

# If not found, install
choco install graphviz -y

# Regenerate docs
.\generate-api-docs.ps1 clean
.\generate-api-docs.ps1 generate
```

### Problem: Documentation looks empty

**Check:**
1. Are you in the project root directory?
2. Does `Doxyfile` exist?
3. Check input paths in Doxyfile are correct
4. Run with verbose: `.\generate-api-docs.ps1 generate -Verbose`

### Problem: Too many warnings

**Action:**
1. View warnings: `Get-Content doxygen_warnings.log | more`
2. Identify most common issues
3. Fix highest-priority items first (public APIs)
4. Regenerate and check progress

---

## 📋 Complete Checklist

### Initial Setup (One-Time)
- [ ] Install Doxygen
- [ ] Install Graphviz (optional)
- [ ] Verify installations with `doxygen --version` and `dot -V`

### First Generation
- [ ] Open terminal in project root
- [ ] Run `.\generate-api-docs.bat` or `.\generate-api-docs.ps1 generate`
- [ ] Wait for completion (may take 1-2 minutes)
- [ ] Open generated docs in browser
- [ ] Review documentation quality

### Quality Check
- [ ] Open `doxygen_warnings.log`
- [ ] Count undocumented items
- [ ] Identify most common warning types
- [ ] Plan documentation improvements

### Improvement Cycle
- [ ] Add documentation to priority items
- [ ] Save files
- [ ] Regenerate: `.\generate-api-docs.ps1 generate`
- [ ] Check improvements: `.\generate-api-docs.ps1 stats`
- [ ] Repeat until satisfied

### Publishing
- [ ] Commit changes: `git add . && git commit -m "docs: improve API documentation"`
- [ ] Push to develop: `git push origin develop`
- [ ] Test in PR
- [ ] Merge to main for automatic deployment
- [ ] Verify published docs online

---

## 📚 Additional Resources

### Documentation Files Created

1. **`DOXYGEN_SETUP_COMPLETE.md`** - What was set up
2. **`docs/API_DOCUMENTATION_GUIDE.md`** - Comprehensive guide
3. **`docs/api-reference/README.md`** - Quick reference
4. **`Doxyfile`** - Configuration file
5. **`generate-api-docs.ps1`** - PowerShell script
6. **`generate-api-docs.bat`** - Batch wrapper
7. **`.github/workflows/api-docs.yml`** - CI/CD workflow

### Useful Commands

```powershell
# Generate docs
.\generate-api-docs.ps1 generate

# Generate and open
.\generate-api-docs.ps1 generate -Open

# View existing docs
.\generate-api-docs.ps1 view

# Show statistics
.\generate-api-docs.ps1 stats

# Clean output
.\generate-api-docs.ps1 clean

# Get help
.\generate-api-docs.ps1 help

# Direct Doxygen
doxygen Doxyfile
```

---

## 🎯 Next Steps

1. **Install Doxygen** (see Prerequisites above)
2. **Run generation**: `.\generate-api-docs.bat`
3. **Review output**: Open `docs/api-reference/html/index.html`
4. **Check quality**: `.\generate-api-docs.ps1 stats`
5. **Improve documentation** where needed
6. **Commit and push** to publish

---

## 💡 Tips for Success

### Documentation Best Practices

1. **Start with public APIs** - Document user-facing code first
2. **Use examples** - Show how to use complex features
3. **Be concise** - Brief descriptions are better than novels
4. **Stay current** - Update docs when changing code
5. **Review regularly** - Check warnings log periodically

### Performance Tips

1. **Disable diagrams temporarily** - Set `HAVE_DOT = NO` in Doxyfile for faster generation
2. **Reduce graph size** - Set `DOT_GRAPH_MAX_NODES = 25` for quicker processing
3. **Exclude test files** - Add to `EXCLUDE_PATTERNS` if not needed

### Integration Tips

1. **Link from MkDocs** - Add links in markdown files
2. **Use consistent style** - Follow existing comment patterns
3. **Automate checks** - Let GitHub Actions catch issues
4. **Review PRs** - Check documentation quality in code reviews

---

## ✅ Summary

You now have:
- ✅ Complete Doxygen configuration
- ✅ Easy-to-use generation scripts
- ✅ Comprehensive documentation guides
- ✅ Automated CI/CD pipeline
- ✅ Publishing to GitHub Pages

**Just run `.\generate-api-docs.bat` to get started!**

---

**Need Help?**
- 📖 Read: `docs/API_DOCUMENTATION_GUIDE.md`
- 🐛 Report: [GitHub Issues](https://github.com/currentsuspect/NOMAD/issues)
- 💬 Discuss: [GitHub Discussions](https://github.com/currentsuspect/NOMAD/discussions)

---

*Generated on: November 2, 2025*  
*NOMAD DAW © 2025 Nomad Studios*
