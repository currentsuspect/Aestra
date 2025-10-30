# Building and Distributing NOMAD DAW

This guide explains how to build, sign, and package NOMAD DAW for distribution.

## Prerequisites

- Windows 10/11 (64-bit)
- Visual Studio 2022 with C++ workload
- CMake 3.15 or later
- Git
- Inno Setup 6 or later (for creating installers)
- Windows SDK (included with Visual Studio)

## Building from Source

1. Clone the repository:
   ```
   git clone https://github.com/yourusername/nomad-daw.git
   cd nomad-daw
   ```

2. Create a build directory and run CMake:
   ```
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022" -A x64
   ```

3. Build the solution:
   ```
   cmake --build . --config Release
   ```

## Creating a Signed Installer

1. Run the build and sign script as Administrator:
   ```
   build_and_sign.bat
   ```
   This will:
   - Generate a self-signed certificate (if it doesn't exist)
   - Build the project
   - Sign the executable

2. Build the installer using Inno Setup:
   ```
   "C:\Program Files (x86)\Inno Setup 6\ISCC.exe" installer.iss
   ```

## Verifying the Signature

To verify the signature of the built executable:

```
signtool verify /v /pa "build\Release\NomadDAW.exe"
```

## Distribution

The final installer will be created as `Output\NOMAD-DAW-Setup.exe`.

## Code Signing Certificate

For production releases, replace the self-signed certificate with a proper code signing certificate from a trusted Certificate Authority.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.
