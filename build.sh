#!/bin/bash
# Build script for XXML Compiler on Linux/macOS

set -e

echo "================================"
echo "XXML Compiler Build Script"
echo "================================"
echo ""

# Check if CMake is available
if ! command -v cmake &> /dev/null; then
    echo "ERROR: CMake not found. Please install CMake."
    exit 1
fi

# Create build directory
mkdir -p build
cd build

echo "Configuring with CMake..."
cmake -DCMAKE_BUILD_TYPE=Release ..

echo ""
echo "Building project..."
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)

echo ""
echo "================================"
echo "Build completed successfully!"
echo "================================"
echo ""
echo "Compiler executable: build/bin/xxml"
echo ""
echo "To test the compiler, run:"
echo "  ./build/bin/xxml Test.XXML output.cpp"
echo ""

cd ..
