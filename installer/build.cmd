@echo off
:: XXML Installer Build Script
:: Run this to build the installer with a single command
::
:: Usage:
::   build.cmd              - Build with all defaults (downloads MinGW if needed)
::   build.cmd --no-mingw   - Build without MinGW
::   build.cmd --help       - Show help
::
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"
set "PROJECT_ROOT=%SCRIPT_DIR%.."

:: Parse arguments
set "EXTRA_ARGS="
:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="--help" goto :show_help
if /i "%~1"=="-h" goto :show_help
set "EXTRA_ARGS=%EXTRA_ARGS% %~1"
shift
goto :parse_args
:done_args

echo.
echo ========================================
echo   XXML Installer Builder
echo ========================================
echo.

:: Check for PowerShell
where powershell >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: PowerShell not found. Please install PowerShell.
    exit /b 1
)

:: Run the PowerShell build script
echo Running build script...
echo.
powershell -NoProfile -ExecutionPolicy Bypass -File "%SCRIPT_DIR%build-installer.ps1" %EXTRA_ARGS%

if %ERRORLEVEL% neq 0 (
    echo.
    echo Build failed with error code %ERRORLEVEL%
    exit /b %ERRORLEVEL%
)

echo.
echo Build completed successfully!
exit /b 0

:show_help
echo.
echo XXML Installer Build Script
echo.
echo Usage: build.cmd [options]
echo.
echo Options:
echo   --no-mingw        Don't include MinGW GCC in the installer
echo   --skip-build      Skip compiler build (use existing)
echo   --version X.Y.Z   Set version number
echo   --help, -h        Show this help
echo.
echo Examples:
echo   build.cmd                    Full build with MinGW
echo   build.cmd --no-mingw         Build without MinGW
echo   build.cmd --skip-build       Use existing compiler build
echo.
exit /b 0
