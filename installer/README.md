# XXML Compiler Installer

Build installers for the XXML Compiler on Windows and macOS.

## Quick Start

### Windows

```cmd
cd installer
build.cmd
```

### macOS

```bash
cd installer
./build-installer-macos.sh
```

## Platform-Specific Instructions

---

## macOS Installer

### Prerequisites

- **Xcode Command Line Tools**: `xcode-select --install`
- **CMake**: `brew install cmake`
- **Ninja** (optional): `brew install ninja`

### Code Signing Setup

For a properly signed and notarized installer:

1. Copy the example configuration:
   ```bash
   cp .env.local.example .env.local
   ```

2. Edit `.env.local` with your Apple Developer credentials:
   ```bash
   # Your Apple Developer Team ID
   APPLE_TEAM_ID=XXXXXXXXXX

   # Your Apple ID for notarization
   APPLE_ID=your-email@example.com

   # App-specific password (create at appleid.apple.com)
   APPLE_APP_PASSWORD=xxxx-xxxx-xxxx-xxxx

   # Code signing identities (from Keychain)
   DEVELOPER_ID_APPLICATION="Developer ID Application: Your Name (TEAMID)"
   DEVELOPER_ID_INSTALLER="Developer ID Installer: Your Name (TEAMID)"
   ```

3. Ensure your certificates are in Keychain. List available identities:
   ```bash
   security find-identity -v -p codesigning
   ```

### Build Options

```bash
./build-installer-macos.sh                    # Full build with signing
./build-installer-macos.sh --skip-build       # Use existing build
./build-installer-macos.sh --skip-sign        # Skip code signing (testing)
./build-installer-macos.sh --skip-notarize    # Skip notarization (local testing)
./build-installer-macos.sh --version 3.0.1    # Override version
```

### Output

The signed PKG installer will be created in `installer/out/`:
- `XXML-Compiler-X.Y.Z.pkg`

### Installation

```bash
# GUI installation
open out/XXML-Compiler-3.0.0.pkg

# Command-line installation
sudo installer -pkg out/XXML-Compiler-3.0.0.pkg -target /
```

### What's Installed (macOS)

| Path | Contents |
|------|----------|
| `/usr/local/bin/xxml` | XXML Compiler |
| `/usr/local/bin/xxml-lsp` | Language Server |
| `/usr/local/lib/xxml/` | Runtime library |
| `/usr/local/share/xxml/Language/` | Standard library |
| `/usr/local/share/xxml/docs/` | Documentation |

### Uninstallation (macOS)

```bash
sudo rm -rf /usr/local/bin/xxml /usr/local/bin/xxml-lsp
sudo rm -rf /usr/local/lib/xxml /usr/local/share/xxml
```

---

## Windows Installer

### Quick Start

```cmd
cd installer
build.cmd
```

The script will:
1. Check and install prerequisites (Inno Setup or WiX)
2. Download MinGW-w64 GCC toolchain if needed
3. Build the XXML compiler
4. Create the installer

### Output

The installer will be created in `installer/out/`:
- **Inno Setup**: `xxml-X.Y.Z-windows-x64-setup.exe`
- **WiX**: `xxml-X.Y.Z-windows-x64.msi`

### Build Options

```cmd
build.cmd                     # Full build with MinGW included
build.cmd --no-mingw          # Build without MinGW (smaller installer)
build.cmd --skip-build        # Skip compiler build (use existing)
build.cmd --version 3.0.1     # Override version number
```

### Prerequisites (Windows)

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

### What's Included (Windows)

#### Full Installation
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
- VS Code Extension (VSIX)
- Documentation

#### Compact Installation
- XXML Compiler
- Standard Library
- Runtime Library

### Silent Installation (Windows)

```cmd
:: Full installation
xxml-3.0.0-windows-x64-setup.exe /VERYSILENT /SUPPRESSMSGBOXES

:: Without MinGW
xxml-3.0.0-windows-x64-setup.exe /VERYSILENT /COMPONENTS="main,stdlib,lsp"
```

### Uninstallation (Windows)

Use "Add or Remove Programs" in Windows Settings, or run:
```cmd
"C:\Program Files\XXML\unins000.exe" /VERYSILENT
```

---

## Directory Structure

```
installer/
├── README.md                  # This file
├── .env.local.example         # macOS code signing config template
├── .env.local                 # Your local config (git-ignored)
│
├── build.cmd                  # Windows: One-command build
├── build-installer.ps1        # Windows: Main PowerShell script
├── build-installer-macos.sh   # macOS: Build script
│
├── wix/                       # Windows: WiX installer sources (MSI)
│   ├── Package.wxs
│   ├── Directories.wxs
│   ├── Components.wxs
│   └── XXML.wixproj
│
├── scripts/                   # Windows: Inno Setup sources (EXE)
│   ├── xxml-setup.iss
│   └── build-inno.ps1
│
├── resources/                 # Installer assets
│   ├── license.rtf            # Windows license
│   ├── xxml.ico               # Windows icon
│   ├── banner.bmp             # Windows banner
│   └── entitlements.plist     # macOS entitlements
│
├── temp/                      # Downloaded files (auto-created)
├── staging/                   # macOS staging (auto-created)
└── out/                       # Built installers (auto-created)
```

---

## VS Code Extension

After installation, install the VS Code extension:

### Windows
```cmd
code --install-extension "C:\Program Files\XXML\tools\vscode-xxml\xxml-language-0.1.0.vsix"
```

### macOS
The VS Code extension can be installed from the VS Code Marketplace, or manually:
```bash
code --install-extension /path/to/xxml-language-0.1.0.vsix
```

---

## Troubleshooting

### macOS

#### "No .env.local found"
Copy `.env.local.example` to `.env.local` and configure your credentials. For testing without signing, use `--skip-sign`.

#### "Certificate not found"
Ensure your Developer ID certificates are installed in Keychain. Check available identities:
```bash
security find-identity -v -p codesigning
```

#### "Notarization failed"
1. Check your Apple ID credentials in `.env.local`
2. Ensure your app-specific password is valid
3. Check the notarization log for specific errors:
   ```bash
   xcrun notarytool log <submission-id> --apple-id YOUR_ID --password YOUR_PASSWORD --team-id YOUR_TEAM
   ```

### Windows

#### "CMake not found"
Install CMake from https://cmake.org/download/ and ensure it's in PATH.

#### "MSVC not found"
Install Visual Studio Build Tools or Visual Studio with C++ workload.

#### "Inno Setup failed"
1. Check that the compiler built successfully in `build/release/bin/`
2. Run `build.cmd --skip-build` to rebuild just the installer

#### Large Installer Size
The full installer with MinGW is ~150-200 MB. Use `--no-mingw` for a smaller installer (~5-10 MB) if users already have GCC/Clang installed.

---

## CI/CD Integration

### GitHub Actions (macOS)

```yaml
- name: Build macOS Installer
  env:
    APPLE_TEAM_ID: ${{ secrets.APPLE_TEAM_ID }}
    APPLE_ID: ${{ secrets.APPLE_ID }}
    APPLE_APP_PASSWORD: ${{ secrets.APPLE_APP_PASSWORD }}
    DEVELOPER_ID_APPLICATION: ${{ secrets.DEVELOPER_ID_APPLICATION }}
    DEVELOPER_ID_INSTALLER: ${{ secrets.DEVELOPER_ID_INSTALLER }}
  run: |
    cd installer
    echo "APPLE_TEAM_ID=$APPLE_TEAM_ID" > .env.local
    echo "APPLE_ID=$APPLE_ID" >> .env.local
    echo "APPLE_APP_PASSWORD=$APPLE_APP_PASSWORD" >> .env.local
    echo "DEVELOPER_ID_APPLICATION=$DEVELOPER_ID_APPLICATION" >> .env.local
    echo "DEVELOPER_ID_INSTALLER=$DEVELOPER_ID_INSTALLER" >> .env.local
    ./build-installer-macos.sh
```

Note: For CI, you'll also need to import certificates into the build agent's Keychain.
