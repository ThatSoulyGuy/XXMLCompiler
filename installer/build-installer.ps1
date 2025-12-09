<#
.SYNOPSIS
    Builds the XXML Compiler Windows installer with a single command.

.DESCRIPTION
    This script automates the entire installer build process:
    1. Detects/installs prerequisites (Inno Setup preferred, WiX as fallback)
    2. Downloads MinGW-w64 GCC toolchain if not present
    3. Builds the XXML compiler
    4. Creates the installer package (.exe or .msi)

.PARAMETER SkipCompilerBuild
    Skip building the compiler (use existing build in build/release)

.PARAMETER SkipMinGW
    Don't include MinGW in the installer (users must have GCC/Clang)

.PARAMETER MinGWPath
    Path to existing MinGW installation to bundle

.PARAMETER Version
    Version number for the installer (default: read from CMakeLists.txt)

.PARAMETER OutputDir
    Output directory for the installer (default: installer/out)

.PARAMETER UseWix
    Force use of WiX Toolset instead of Inno Setup

.EXAMPLE
    .\build-installer.ps1
    # Full build with MinGW, auto-detects installer tool

.EXAMPLE
    .\build-installer.ps1 -SkipMinGW
    # Build without MinGW (smaller installer)

.EXAMPLE
    .\build-installer.ps1 -SkipCompilerBuild -Version "3.0.0"
    # Use existing build, specify version

.NOTES
    Run from the installer directory: cd installer && .\build-installer.ps1
#>

[CmdletBinding()]
param(
    [switch]$SkipCompilerBuild,
    [switch]$SkipMinGW,
    [Alias("no-mingw")][switch]$NoMinGW,
    [Alias("skip-build")][switch]$SkipBuild,
    [string]$MinGWPath = "",
    [string]$Version = "",
    [string]$OutputDir = "",
    [switch]$UseWix
)

# Handle aliases
if ($NoMinGW) { $SkipMinGW = $true }
if ($SkipBuild) { $SkipCompilerBuild = $true }

$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

# =============================================================================
# Configuration
# =============================================================================

$ScriptDir = $PSScriptRoot
if (-not $ScriptDir) { $ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path }
$ProjectRoot = (Resolve-Path "$ScriptDir\..").Path
$InstallerDir = $ScriptDir
$WixDir = "$InstallerDir\wix"
$ScriptsDir = "$InstallerDir\scripts"
$ResourcesDir = "$InstallerDir\resources"
$BuildDir = "$ProjectRoot\build\release"
$TempDir = "$InstallerDir\temp"

if ([string]::IsNullOrEmpty($OutputDir)) {
    $OutputDir = "$InstallerDir\out"
}

# MinGW-w64 download (GCC 13.2, UCRT runtime, POSIX threads)
$MinGWUrl = "https://github.com/niXman/mingw-builds-binaries/releases/download/13.2.0-rt_v11-rev1/x86_64-13.2.0-release-posix-seh-ucrt-rt_v11-rev1.7z"
$MinGWArchive = "$TempDir\mingw64.7z"
$MinGWExtracted = "$TempDir\mingw64"

# =============================================================================
# Helper Functions
# =============================================================================

function Write-Banner {
    $banner = @"

  ██╗  ██╗██╗  ██╗███╗   ███╗██╗
  ╚██╗██╔╝╚██╗██╔╝████╗ ████║██║
   ╚███╔╝  ╚███╔╝ ██╔████╔██║██║
   ██╔██╗  ██╔██╗ ██║╚██╔╝██║██║
  ██╔╝ ██╗██╔╝ ██╗██║ ╚═╝ ██║███████╗
  ╚═╝  ╚═╝╚═╝  ╚═╝╚═╝     ╚═╝╚══════╝
           Installer Builder

"@
    Write-Host $banner -ForegroundColor Cyan
}

function Write-Header {
    param([string]$Text)
    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Cyan
    Write-Host "  $Text" -ForegroundColor Cyan
    Write-Host ("=" * 60) -ForegroundColor Cyan
}

function Write-Step {
    param([string]$Text)
    Write-Host "[*] $Text" -ForegroundColor Yellow
}

function Write-Success {
    param([string]$Text)
    Write-Host "[+] $Text" -ForegroundColor Green
}

function Write-Warn {
    param([string]$Text)
    Write-Host "[!] $Text" -ForegroundColor DarkYellow
}

function Write-Err {
    param([string]$Text)
    Write-Host "[X] $Text" -ForegroundColor Red
}

function Test-CommandExists {
    param([string]$Command)
    return $null -ne (Get-Command $Command -ErrorAction SilentlyContinue)
}

function Get-ProjectVersion {
    $cmakeLists = Get-Content "$ProjectRoot\CMakeLists.txt" -Raw -ErrorAction SilentlyContinue
    if ($cmakeLists -match 'project\s*\(\s*XXMLCompiler\s+VERSION\s+(\d+\.\d+\.\d+)') {
        return $Matches[1]
    }
    return "3.0.0"
}

# =============================================================================
# Tool Detection and Installation
# =============================================================================

function Find-InnoSetup {
    $paths = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles}\Inno Setup 6\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Find-7Zip {
    $paths = @(
        "${env:ProgramFiles}\7-Zip\7z.exe",
        "${env:ProgramFiles(x86)}\7-Zip\7z.exe"
    )
    foreach ($p in $paths) {
        if (Test-Path $p) { return $p }
    }
    return $null
}

function Install-WithWinget {
    param([string]$PackageId, [string]$Name)
    if (-not (Test-CommandExists "winget")) {
        return $false
    }
    Write-Step "Installing $Name via winget..."
    $result = winget install $PackageId --silent --accept-package-agreements --accept-source-agreements 2>&1
    return $LASTEXITCODE -eq 0
}

function Ensure-InnoSetup {
    Write-Step "Checking for Inno Setup..."

    $iscc = Find-InnoSetup
    if ($iscc) {
        Write-Success "Found Inno Setup: $iscc"
        return $iscc
    }

    if (Install-WithWinget "JRSoftware.InnoSetup" "Inno Setup") {
        $iscc = Find-InnoSetup
        if ($iscc) {
            Write-Success "Installed Inno Setup: $iscc"
            return $iscc
        }
    }

    Write-Warn "Inno Setup not available"
    return $null
}

function Ensure-7Zip {
    Write-Step "Checking for 7-Zip..."

    $7z = Find-7Zip
    if ($7z) {
        Write-Success "Found 7-Zip: $7z"
        return $7z
    }

    if (Install-WithWinget "7zip.7zip" "7-Zip") {
        $7z = Find-7Zip
        if ($7z) {
            Write-Success "Installed 7-Zip: $7z"
            return $7z
        }
    }

    Write-Warn "7-Zip not available (required for MinGW)"
    return $null
}

function Ensure-WixToolset {
    Write-Step "Checking for WiX Toolset..."

    if (Test-CommandExists "wix") {
        Write-Success "Found WiX Toolset"
        return $true
    }

    if (Test-CommandExists "dotnet") {
        Write-Step "Installing WiX Toolset as .NET tool..."
        dotnet tool install -g wix 2>&1 | Out-Null
        $env:PATH = "$env:USERPROFILE\.dotnet\tools;$env:PATH"

        if (Test-CommandExists "wix") {
            Write-Success "Installed WiX Toolset"
            return $true
        }
    }

    Write-Warn "WiX Toolset not available"
    return $false
}

# =============================================================================
# MinGW Handling
# =============================================================================

function Get-MinGW {
    param([string]$SevenZipPath)

    Write-Header "Preparing MinGW-w64 Toolchain"

    # Check if already extracted
    if (Test-Path "$MinGWExtracted\bin\gcc.exe") {
        Write-Success "MinGW already available at: $MinGWExtracted"
        return $MinGWExtracted
    }

    # Check provided path
    if ($MinGWPath -and (Test-Path "$MinGWPath\bin\gcc.exe")) {
        Write-Success "Using provided MinGW: $MinGWPath"
        return $MinGWPath
    }

    # Check common installation locations
    $commonPaths = @(
        "C:\mingw64",
        "C:\msys64\mingw64",
        "C:\tools\mingw64",
        "$env:LOCALAPPDATA\mingw64"
    )

    foreach ($path in $commonPaths) {
        if (Test-Path "$path\bin\gcc.exe") {
            Write-Success "Found system MinGW: $path"
            return $path
        }
    }

    # Need to download
    if (-not $SevenZipPath) {
        Write-Err "7-Zip required to extract MinGW"
        return $null
    }

    New-Item -ItemType Directory -Force -Path $TempDir | Out-Null

    # Download if needed
    if (-not (Test-Path $MinGWArchive)) {
        Write-Step "Downloading MinGW-w64 GCC 13.2 (~60 MB)..."
        Write-Host "    URL: $MinGWUrl" -ForegroundColor DarkGray

        try {
            Invoke-WebRequest -Uri $MinGWUrl -OutFile $MinGWArchive -UseBasicParsing
            Write-Success "Download complete"
        }
        catch {
            Write-Err "Download failed: $_"
            return $null
        }
    }
    else {
        Write-Success "Using cached MinGW archive"
    }

    # Extract
    Write-Step "Extracting MinGW-w64..."
    & $SevenZipPath x $MinGWArchive -o"$TempDir" -y 2>&1 | Out-Null

    if (Test-Path "$MinGWExtracted\bin\gcc.exe") {
        Write-Success "MinGW extracted to: $MinGWExtracted"
        return $MinGWExtracted
    }

    Write-Err "Failed to extract MinGW"
    return $null
}

# =============================================================================
# Compiler Build
# =============================================================================

function Build-Compiler {
    Write-Header "Building XXML Compiler"

    if (-not (Test-CommandExists "cmake")) {
        Write-Err "CMake not found. Please install CMake and add to PATH."
        return $false
    }

    Push-Location $ProjectRoot
    try {
        Write-Step "Configuring CMake..."
        $configResult = cmake --preset release 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Err "CMake configuration failed"
            Write-Host $configResult -ForegroundColor DarkGray
            return $false
        }

        Write-Step "Building..."
        $buildResult = cmake --build --preset release 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Err "Build failed"
            Write-Host ($buildResult | Select-Object -Last 20) -ForegroundColor DarkGray
            return $false
        }

        Write-Success "Compiler built successfully"
        return $true
    }
    finally {
        Pop-Location
    }
}

# =============================================================================
# Resource Creation
# =============================================================================

function New-InstallerResources {
    Write-Step "Creating installer resources..."

    New-Item -ItemType Directory -Force -Path $ResourcesDir | Out-Null

    # License RTF
    if (-not (Test-Path "$ResourcesDir\license.rtf")) {
        $licenseRtf = @"
{\rtf1\ansi\deff0{\fonttbl{\f0\fnil Arial;}}
\viewkind4\uc1\pard\f0\fs20
\b XXML Compiler License\b0\par\par
Copyright (c) 2025 XXML Project\par\par
Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:\par\par
The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.\par\par
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\par\par
\b MinGW-w64\b0\par\par
If included, MinGW-w64 is licensed under various open source licenses. See https://www.mingw-w64.org/ for details.\par
}
"@
        Set-Content -Path "$ResourcesDir\license.rtf" -Value $licenseRtf
    }

    # Simple icon placeholder (minimal valid ICO)
    if (-not (Test-Path "$ResourcesDir\xxml.ico")) {
        # Create a minimal 16x16 32-bit ICO file
        $icoHeader = [byte[]]@(
            0x00, 0x00,       # Reserved
            0x01, 0x00,       # Type (1 = ICO)
            0x01, 0x00        # Count (1 image)
        )
        $icoEntry = [byte[]]@(
            0x10,             # Width (16)
            0x10,             # Height (16)
            0x00,             # Color palette
            0x00,             # Reserved
            0x01, 0x00,       # Color planes
            0x20, 0x00,       # Bits per pixel (32)
            0x68, 0x04, 0x00, 0x00,  # Size of image data
            0x16, 0x00, 0x00, 0x00   # Offset to image data
        )

        # BMP header for 16x16 32-bit image
        $bmpHeader = [byte[]]@(
            0x28, 0x00, 0x00, 0x00,  # Header size (40)
            0x10, 0x00, 0x00, 0x00,  # Width (16)
            0x20, 0x00, 0x00, 0x00,  # Height (32 for ICO = 2x actual)
            0x01, 0x00,              # Planes
            0x20, 0x00,              # Bits per pixel (32)
            0x00, 0x00, 0x00, 0x00,  # Compression
            0x00, 0x04, 0x00, 0x00,  # Image size
            0x00, 0x00, 0x00, 0x00,  # X pixels/meter
            0x00, 0x00, 0x00, 0x00,  # Y pixels/meter
            0x00, 0x00, 0x00, 0x00,  # Colors used
            0x00, 0x00, 0x00, 0x00   # Important colors
        )

        # Create 16x16 pixel data (BGRA) - blue gradient
        $pixels = New-Object byte[] 1024
        for ($y = 0; $y -lt 16; $y++) {
            for ($x = 0; $x -lt 16; $x++) {
                $i = ($y * 16 + $x) * 4
                $pixels[$i] = [byte](100 + $y * 8)   # Blue
                $pixels[$i+1] = [byte](60 + $x * 4)  # Green
                $pixels[$i+2] = [byte](40)          # Red
                $pixels[$i+3] = [byte]255           # Alpha
            }
        }

        # AND mask (all zeros = fully opaque)
        $andMask = New-Object byte[] 64

        $icoData = $icoHeader + $icoEntry + $bmpHeader + $pixels + $andMask
        [System.IO.File]::WriteAllBytes("$ResourcesDir\xxml.ico", $icoData)
    }

    Write-Success "Resources ready"
}

# =============================================================================
# Installer Building
# =============================================================================

function Build-InnoSetupInstaller {
    param(
        [string]$InnoSetupPath,
        [string]$ProductVersion,
        [string]$MinGWFinalPath
    )

    Write-Header "Building Installer (Inno Setup)"

    New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

    # Set environment variables for the ISS script
    $env:XXML_VERSION = $ProductVersion
    $env:XXML_PROJECT_ROOT = $ProjectRoot
    $env:MINGW_PATH = if ($MinGWFinalPath) { $MinGWFinalPath } else { "$TempDir\nonexistent" }

    Write-Step "Running Inno Setup Compiler..."

    $issFile = "$ScriptsDir\xxml-setup.iss"
    if (-not (Test-Path $issFile)) {
        Write-Err "Inno Setup script not found: $issFile"
        return $null
    }

    & $InnoSetupPath $issFile 2>&1 | ForEach-Object { Write-Host "    $_" -ForegroundColor DarkGray }

    if ($LASTEXITCODE -ne 0) {
        Write-Err "Inno Setup compilation failed"
        return $null
    }

    $installer = Get-ChildItem "$OutputDir\*.exe" -ErrorAction SilentlyContinue |
                 Sort-Object LastWriteTime -Descending |
                 Select-Object -First 1

    if ($installer) {
        return $installer.FullName
    }

    Write-Err "Installer not created"
    return $null
}

# =============================================================================
# Main Script
# =============================================================================

Write-Banner

Write-Host "Project Root: $ProjectRoot" -ForegroundColor DarkGray
Write-Host "Build Dir:    $BuildDir" -ForegroundColor DarkGray
Write-Host "Output Dir:   $OutputDir" -ForegroundColor DarkGray

# Determine version
if ([string]::IsNullOrEmpty($Version)) {
    $Version = Get-ProjectVersion
}
Write-Host "Version:      $Version" -ForegroundColor Cyan
Write-Host ""

# =============================================================================
# Step 1: Check Prerequisites
# =============================================================================

Write-Header "Checking Prerequisites"

# Find installer tool
$isccPath = $null
$useInnoSetup = $true

if (-not $UseWix) {
    $isccPath = Ensure-InnoSetup
}

if (-not $isccPath) {
    Write-Step "Falling back to WiX Toolset..."
    if (Ensure-WixToolset) {
        $useInnoSetup = $false
    }
    else {
        Write-Err "No installer tool available!"
        Write-Host ""
        Write-Host "Please install one of:" -ForegroundColor Yellow
        Write-Host "  - Inno Setup 6: https://jrsoftware.org/isinfo.php" -ForegroundColor Yellow
        Write-Host "  - WiX Toolset:  dotnet tool install -g wix" -ForegroundColor Yellow
        exit 1
    }
}

# Check for 7-Zip (needed for MinGW)
$sevenZipPath = $null
if (-not $SkipMinGW) {
    $sevenZipPath = Ensure-7Zip
}

# =============================================================================
# Step 2: Prepare MinGW
# =============================================================================

$mingwFinalPath = $null
if (-not $SkipMinGW) {
    if ($sevenZipPath) {
        $mingwFinalPath = Get-MinGW -SevenZipPath $sevenZipPath
    }

    if (-not $mingwFinalPath) {
        Write-Host ""
        Write-Warn "MinGW not available - installer will not include GCC"
        Write-Host "    Users will need to install GCC/Clang separately" -ForegroundColor DarkGray
        Write-Host "    Or re-run with a valid MinGW path: -MinGWPath C:\mingw64" -ForegroundColor DarkGray
        Write-Host ""
    }
}
else {
    Write-Step "Skipping MinGW (--no-mingw specified)"
}

# =============================================================================
# Step 3: Build Compiler
# =============================================================================

if (-not $SkipCompilerBuild) {
    if (-not (Build-Compiler)) {
        Write-Err "Compiler build failed"
        exit 1
    }
}
else {
    Write-Step "Skipping compiler build (--skip-build specified)"
    if (-not (Test-Path "$BuildDir\bin\xxml.exe")) {
        Write-Err "Compiler not found at: $BuildDir\bin\xxml.exe"
        Write-Err "Build first without -SkipCompilerBuild"
        exit 1
    }
    Write-Success "Using existing compiler build"
}

# =============================================================================
# Step 4: Create Resources
# =============================================================================

New-InstallerResources

# =============================================================================
# Step 5: Build Installer
# =============================================================================

$installerPath = $null

if ($useInnoSetup -and $isccPath) {
    $installerPath = Build-InnoSetupInstaller -InnoSetupPath $isccPath -ProductVersion $Version -MinGWFinalPath $mingwFinalPath
}
else {
    Write-Header "Building Installer (WiX MSI)"
    Write-Warn "WiX MSI build not fully implemented - use Inno Setup for best results"
    # WiX implementation would go here
}

# =============================================================================
# Done
# =============================================================================

if ($installerPath -and (Test-Path $installerPath)) {
    $fileSize = [math]::Round((Get-Item $installerPath).Length / 1MB, 1)

    Write-Host ""
    Write-Host ("=" * 60) -ForegroundColor Green
    Write-Host "  BUILD SUCCESSFUL" -ForegroundColor Green
    Write-Host ("=" * 60) -ForegroundColor Green
    Write-Host ""
    Write-Host "  Installer: $installerPath" -ForegroundColor Cyan
    Write-Host "  Size:      $fileSize MB" -ForegroundColor DarkGray
    Write-Host ""
    Write-Host "  To install: " -NoNewline
    Write-Host $installerPath -ForegroundColor Yellow
    Write-Host ""
}
else {
    Write-Host ""
    Write-Err "Build failed - installer not created"
    exit 1
}
