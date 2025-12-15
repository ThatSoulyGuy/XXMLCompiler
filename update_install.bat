@echo off
echo Updating XXML installation at C:\Program Files\XXML...
echo.

REM Check for admin privileges
net session >nul 2>&1
if %errorLevel% neq 0 (
    echo ERROR: This script requires Administrator privileges.
    echo Please right-click and select "Run as administrator"
    pause
    exit /b 1
)

set SOURCE=D:\VisualStudio\Projects\XXMLCompiler\build\release
set DEST=C:\Program Files\XXML

echo Copying compiler executable...
copy /Y "%SOURCE%\bin\xxml.exe" "%DEST%\bin\xxml.exe"
if %errorLevel% neq 0 (
    echo Failed to copy xxml.exe
    pause
    exit /b 1
)

echo Copying LSP server...
copy /Y "%SOURCE%\bin\xxml-lsp.exe" "%DEST%\bin\xxml-lsp.exe"

echo Copying runtime library...
copy /Y "%SOURCE%\lib\XXMLLLVMRuntime.lib" "%DEST%\lib\XXMLLLVMRuntime.lib"
if %errorLevel% neq 0 (
    echo Failed to copy XXMLLLVMRuntime.lib
    pause
    exit /b 1
)

echo.
echo ============================================
echo XXML installation updated successfully!
echo ============================================
pause
