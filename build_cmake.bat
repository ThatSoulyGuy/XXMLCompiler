@echo off
REM Build script for XXML Compiler using CMake (without LLVM libraries)

echo ================================
echo XXML Compiler Build Script (CMake)
echo ================================
echo.

REM Check if CMake is available
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo ERROR: CMake not found. Please install CMake and add it to PATH.
    exit /b 1
)

REM Create build directory
if not exist build_cmake mkdir build_cmake
cd build_cmake

echo Configuring with CMake (LLVM backend disabled)...
cmake -G "Visual Studio 17 2022" -A x64 -DXXML_ENABLE_LLVM_BACKEND=OFF ..
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
echo Compiler executable: build_cmake\Release\XXMLCompiler.exe
echo.
echo To test the compiler, run:
echo   build_cmake\Release\XXMLCompiler.exe Test.XXML output.ll 2
echo.

cd ..
