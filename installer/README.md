# XXML Compiler Installer

Build a Windows installer for the XXML Compiler with a single command.

## Quick Start

```cmd
cd installer
build.cmd
```

That's it! The script will:
1. Check and install prerequisites (Inno Setup or WiX)
2. Download MinGW-w64 GCC toolchain if needed
3. Build the XXML compiler
4. Create the installer

## Output

The installer will be created in `installer/out/`:
- **Inno Setup**: `xxml-X.Y.Z-windows-x64-setup.exe`
- **WiX**: `xxml-X.Y.Z-windows-x64.msi`

## Options

```cmd
build.cmd                     # Full build with MinGW included
build.cmd --no-mingw          # Build without MinGW (smaller installer)
build.cmd --skip-build        # Skip compiler build (use existing)
build.cmd --version 3.0.1     # Override version number
```

## Prerequisites

The build script will automatically install these if missing:

| Tool | Purpose | Auto-Install |
|------|---------|--------------|
| Inno Setup 6 | Creates .exe installer | Via winget |
| 7-Zip | Extract MinGW archive | Via winget |
| CMake | Build compiler | Must be pre-installed |
| Visual Studio / MSVC | C++ compiler | Must be pre-installed |

### Manual Installation (if auto-install fails)

- **Inno Setup**: https://jrsoftware.org/isinfo.php
- **7-Zip**: https://7-zip.org
- **WiX Toolset** (alternative): `dotnet tool install -g wix`

## What's Included

### Full Installation
- XXML Compiler (`xxml.exe`)
- Language Server (`xxml-lsp.exe`)
- Standard Library (Language/*.XXML)
  - Core types (Integer, String, Bool, Float, Double)
  - Collections (List, HashMap, Set, Array, Queue, Stack)
  - Reflection API (Type, MethodInfo, PropertyInfo, etc.)
  - Annotations (Deprecated, NotNull, Override, etc.)
  - System utilities (Console, Threading, DateTime)
  - Format (JSON)
  - Text processing (Regex, StringUtils)
  - Network (HTTP)
  - Test framework
- Runtime Library
- MinGW-w64 GCC Toolchain
- VS Code Extension (VSIX) - syntax highlighting, IntelliSense, diagnostics
- Documentation

### Compact Installation
- XXML Compiler
- Standard Library
- Runtime Library

## Directory Structure

```
installer/
├── build.cmd              # One-command build script
├── build-installer.ps1    # Main PowerShell script
├── wix/                   # WiX installer sources (MSI)
│   ├── Package.wxs
│   ├── Directories.wxs
│   ├── Components.wxs
│   └── XXML.wixproj
├── scripts/               # Inno Setup sources (EXE)
│   ├── xxml-setup.iss
│   └── build-inno.ps1
├── resources/             # Installer assets
│   ├── license.rtf
│   ├── xxml.ico
│   └── banner.bmp
├── temp/                  # Downloaded files (auto-created)
│   └── mingw64/
└── out/                   # Built installers (auto-created)
```

## Customization

### Change MinGW Version

Edit `build-installer.ps1` and modify the `$MinGWUrl` variable:
```powershell
$MinGWUrl = "https://github.com/niXman/mingw-builds-binaries/releases/..."
```

### Use Existing MinGW

```cmd
build.cmd --mingw-path "C:\path\to\mingw64"
```

### Build MSI Instead of EXE

```powershell
.\build-installer.ps1 -UseWix
```

## Troubleshooting

### "CMake not found"
Install CMake from https://cmake.org/download/ and ensure it's in PATH.

### "MSVC not found"
Install Visual Studio Build Tools or Visual Studio with C++ workload.

### "Inno Setup failed"
1. Check that the compiler built successfully in `build/release/bin/`
2. Run `build.cmd --skip-build` to rebuild just the installer

### Large Installer Size
The full installer with MinGW is ~150-200 MB. Use `--no-mingw` for a smaller installer (~5-10 MB) if users already have GCC/Clang installed.

## Silent Installation

```cmd
:: Full installation
xxml-3.0.0-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES

:: Without MinGW
xxml-3.0.0-windows-x64-setup.exe /VERYSILENT /COMPONENTS="main,stdlib,lsp"
```

## VS Code Extension

After installation, install the VS Code extension:

```cmd
code --install-extension "C:\Program Files\XXML\tools\vscode-xxml\xxml-language-0.1.0.vsix"
```

Or open VS Code, go to Extensions (Ctrl+Shift+X), click "..." menu, select "Install from VSIX...", and browse to the file above.

## Uninstallation

Use "Add or Remove Programs" in Windows Settings, or run:
```cmd
"C:\Program Files\XXML\unins000.exe" /VERYSILENT
```
