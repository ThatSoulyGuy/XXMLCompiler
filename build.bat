@echo off
REM Build script for XXML Compiler on Windows

echo ================================
echo XXML Compiler Build Script
echo ================================
echo.

REM Check if CMake is available
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake and add it to PATH.
    exit /b 1
)

REM Create build directory
if not exist build mkdir build
cd build

echo Configuring with CMake...
cmake -G "Visual Studio 17 2022" -A x64 ..
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake configuration failed
    cd ..
    exit /b 1
)

echo.
echo Building project...
cmake --build . --config Release
if %ERRORLEVEL% neq 0 (
    echo ERROR: Build failed
    cd ..
    exit /b 1
)

echo.
echo ================================
echo Build completed successfully!
echo ================================
echo.
echo Compiler executable: build\bin\Release\xxml.exe
echo.
echo To test the compiler, run:
echo   build\bin\Release\xxml.exe Test.XXML output.cpp
echo.

cd ..
