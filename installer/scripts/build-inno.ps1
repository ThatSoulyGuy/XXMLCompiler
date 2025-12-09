<#
.SYNOPSIS
    Builds the XXML Compiler installer using Inno Setup.

.DESCRIPTION
    Alternative installer builder using Inno Setup instead of WiX.
    Inno Setup is simpler and creates .exe installers.

.PARAMETER Version
    Version number for the installer

.PARAMETER SkipCompilerBuild
    Skip building the compiler

.PARAMETER SkipMinGW
    Don't include MinGW

.EXAMPLE
    .\build-inno.ps1

.EXAMPLE
    .\build-inno.ps1 -Version "3.0.0" -SkipCompilerBuild
#>

[CmdletBinding()]
param(
    [string]$Version = "",
    [switch]$SkipCompilerBuild,
    [switch]$SkipMinGW
)

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$InstallerDir = Split-Path -Parent $ScriptDir
$ProjectRoot = (Resolve-Path "$InstallerDir\..").Path
$BuildDir = "$ProjectRoot\build\release"
$TempDir = "$InstallerDir\temp"
$OutputDir = "$InstallerDir\out"

# MinGW download URL
$MinGWUrl = "https://github.com/niXman/mingw-builds-binaries/releases/download/13.2.0-rt_v11-rev1/x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev1.7z"

function Write-Header { param([string]$Text); Write-Host "`n$("=" * 60)`n  $Text`n$("=" * 60)`n" -ForegroundColor Cyan }
function Write-Step { param([string]$Text); Write-Host "[*] $Text" -ForegroundColor Yellow }
function Write-Success { param([string]$Text); Write-Host "[+] $Text" -ForegroundColor Green }
function Write-Error { param([string]$Text); Write-Host "[!] $Text" -ForegroundColor Red }

function Get-ProjectVersion {
    $cmakeLists = Get-Content "$ProjectRoot\CMakeLists.txt" -Raw
    if ($cmakeLists -match 'project\s*\(\s*XXMLCompiler\s+VERSION\s+(\d+\.\d+\.\d+)') {
        return $Matches[1]
    }
    return "3.0.0"
}

function Find-InnoSetup {
    $paths = @(
        "C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
        "C:\Program Files\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )

    foreach ($path in $paths) {
        if (Test-Path $path) { return $path }
    }

    # Try winget install
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Write-Step "Installing Inno Setup via winget..."
        winget install JRSoftware.InnoSetup --silent --accept-package-agreements --accept-source-agreements 2>$null

        foreach ($path in $paths) {
            if (Test-Path $path) { return $path }
        }
    }

    return $null
}

function Find-7Zip {
    $paths = @(
        "C:\Program Files\7-Zip\7z.exe",
        "C:\Program Files (x86)\7-Zip\7z.exe"
    )

    foreach ($path in $paths) {
        if (Test-Path $path) { return $path }
    }

    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Write-Step "Installing 7-Zip via winget..."
        winget install 7zip.7zip --silent --accept-package-agreements --accept-source-agreements 2>$null

        foreach ($path in $paths) {
            if (Test-Path $path) { return $path }
        }
    }

    return $null
}

function Get-MinGW {
    param([string]$7zPath)

    $mingwDir = "$TempDir\mingw64"

    if (Test-Path "$mingwDir\bin\gcc.exe") {
        Write-Success "MinGW already extracted"
        return $mingwDir
    }

    # Check common locations
    $commonPaths = @("C:\mingw64", "C:\msys64\mingw64", "C:\tools\mingw64")
    foreach ($path in $commonPaths) {
        if (Test-Path "$path\bin\gcc.exe") {
            Write-Success "Found existing MinGW: $path"
            return $path
        }
    }

    if (-not $7zPath) {
        Write-Error "7-Zip required to extract MinGW"
        return $null
    }

    # Download
    Write-Step "Downloading MinGW-w64..."
    New-Item -ItemType Directory -Force -Path $TempDir | Out-Null
    $archive = "$TempDir\mingw64.7z"

    if (-not (Test-Path $archive)) {
        Invoke-WebRequest -Uri $MinGWUrl -OutFile $archive
    }

    Write-Step "Extracting MinGW-w64..."
    & $7zPath x $archive -o"$TempDir" -y | Out-Null

    if (Test-Path "$mingwDir\bin\gcc.exe") {
        Write-Success "MinGW extracted"
        return $mingwDir
    }

    return $null
}

function Build-Compiler {
    Write-Header "Building XXML Compiler"

    Push-Location $ProjectRoot
    try {
        cmake --preset release
        if ($LASTEXITCODE -ne 0) { return $false }

        cmake --build --preset release
        if ($LASTEXITCODE -ne 0) { return $false }

        Write-Success "Compiler built"
        return $true
    }
    finally { Pop-Location }
}

function New-IconFile {
    # Create a simple placeholder icon
    $iconPath = "$InstallerDir\resources\xxml.ico"
    if (Test-Path $iconPath) { return }

    New-Item -ItemType Directory -Force -Path "$InstallerDir\resources" | Out-Null

    # Copy system icon as placeholder
    Copy-Item "$env:SystemRoot\System32\cmd.exe" "$TempDir\temp.exe" -Force -ErrorAction SilentlyContinue
    if (Test-Path "$TempDir\temp.exe") {
        # Extract icon using PowerShell
        Add-Type -AssemblyName System.Drawing
        try {
            $icon = [System.Drawing.Icon]::ExtractAssociatedIcon("$TempDir\temp.exe")
            $bitmap = $icon.ToBitmap()
            $bitmap.Save("$InstallerDir\resources\xxml.png", [System.Drawing.Imaging.ImageFormat]::Png)
            $icon.Dispose()
            $bitmap.Dispose()
        } catch {}
        Remove-Item "$TempDir\temp.exe" -Force -ErrorAction SilentlyContinue
    }

    # Create minimal ICO file if extraction failed
    if (-not (Test-Path $iconPath)) {
        # Minimal valid ICO header
        [byte[]]$ico = @(
            0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x10, 0x10,
            0x00, 0x00, 0x01, 0x00, 0x20, 0x00, 0x68, 0x04,
            0x00, 0x00, 0x16, 0x00, 0x00, 0x00
        )
        # Add 16x16 RGBA data (solid blue)
        $pixelData = New-Object byte[] 1128
        for ($i = 0; $i -lt 256; $i++) {
            $pixelData[$i * 4] = 0x80      # Blue
            $pixelData[$i * 4 + 1] = 0x40  # Green
            $pixelData[$i * 4 + 2] = 0x20  # Red
            $pixelData[$i * 4 + 3] = 0xFF  # Alpha
        }
        $fullIco = $ico + $pixelData
        [System.IO.File]::WriteAllBytes($iconPath, $fullIco)
    }
}

# =============================================================================
# Main
# =============================================================================

Write-Header "XXML Inno Setup Installer Builder"

if ([string]::IsNullOrEmpty($Version)) {
    $Version = Get-ProjectVersion
}
Write-Host "Version: $Version"

# Find Inno Setup
$iscc = Find-InnoSetup
if (-not $iscc) {
    Write-Error "Inno Setup not found. Please install from https://jrsoftware.org/isinfo.php"
    exit 1
}
Write-Success "Inno Setup: $iscc"

# Get MinGW
$mingwPath = ""
if (-not $SkipMinGW) {
    $7z = Find-7Zip
    if ($7z) {
        $mingwPath = Get-MinGW -7zPath $7z
    }
}

# Build compiler
if (-not $SkipCompilerBuild) {
    if (-not (Build-Compiler)) {
        Write-Error "Compiler build failed"
        exit 1
    }
} else {
    if (-not (Test-Path "$BuildDir\bin\xxml.exe")) {
        Write-Error "Compiler not found. Build first without -SkipCompilerBuild"
        exit 1
    }
}

# Create icon
New-IconFile

# Create output dir
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

# Set environment variables for Inno Setup
$env:XXML_VERSION = $Version
$env:XXML_PROJECT_ROOT = $ProjectRoot
$env:MINGW_PATH = if ($mingwPath) { $mingwPath } else { "" }

# Run Inno Setup
Write-Header "Building Installer"
Write-Step "Running Inno Setup Compiler..."

& $iscc "$ScriptDir\xxml-setup.iss"

if ($LASTEXITCODE -ne 0) {
    Write-Error "Inno Setup failed"
    exit 1
}

$installer = Get-ChildItem "$OutputDir\*.exe" | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if ($installer) {
    Write-Header "Build Complete!"
    Write-Host "Installer: $($installer.FullName)" -ForegroundColor Green
    Write-Host "`nFile size: $([math]::Round($installer.Length / 1MB, 2)) MB"
} else {
    Write-Error "Installer not created"
    exit 1
}
