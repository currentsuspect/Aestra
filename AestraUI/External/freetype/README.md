# FreeType Integration

This directory will contain FreeType library files.

## Option 1: Use vcpkg (Recommended for Windows)

```bash
# Install vcpkg if not already installed
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# Install FreeType
.\vcpkg install freetype:x64-windows

# Integrate with Visual Studio
.\vcpkg integrate install
```

## Option 2: Manual Download

Download FreeType from: https://freetype.org/download.html

Or use pre-built binaries for Windows from:
https://github.com/ubawurinna/freetype-windows-binaries

Extract and place in this directory.

## Option 3: Use FetchContent in CMake (Current approach)

The CMakeLists.txt will automatically download and build FreeType.
No manual installation needed!
