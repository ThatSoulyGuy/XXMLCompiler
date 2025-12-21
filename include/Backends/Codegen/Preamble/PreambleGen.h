#pragma once

#include <string>
#include <sstream>
#include <memory>
#include "Backends/Codegen/RuntimeManifest.h"

namespace XXML {
namespace Backends {
namespace Codegen {

/// Target platform for code generation
enum class TargetPlatform {
    X86_64_Windows,     // x86-64 Windows (MSVC ABI)
    X86_64_Linux,       // x86-64 Linux (System V ABI)
    X86_64_MacOS,       // x86-64 macOS
    ARM64_Linux,        // ARM64/AArch64 Linux
    ARM64_MacOS,        // ARM64 macOS (Apple Silicon)
    WebAssembly,        // WebAssembly (wasm32)
    Native              // Use host platform
};

/// Generates the LLVM IR preamble containing runtime declarations and type definitions
class PreambleGen {
public:
    explicit PreambleGen(TargetPlatform platform = TargetPlatform::Native);
    ~PreambleGen() = default;

    /// Generate the complete preamble as LLVM IR string
    std::string generate() const;

    /// Get target triple string for the current platform
    std::string getTargetTriple() const;

    /// Get data layout string for the current platform
    std::string getDataLayout() const;

    /// Set target platform
    void setTargetPlatform(TargetPlatform platform) { platform_ = platform; }

    /// Get current target platform
    TargetPlatform getTargetPlatform() const { return platform_; }

    /// Get the runtime manifest (single source of truth for runtime functions)
    const RuntimeManifest& getManifest() const { return manifest_; }

private:
    TargetPlatform platform_;
    RuntimeManifest manifest_;

    // Helper methods for generating preamble sections
    void emitHeader(std::stringstream& out) const;
    void emitTargetInfo(std::stringstream& out) const;
    void emitBuiltinTypes(std::stringstream& out) const;
    void emitWindowsDLLFunctions(std::stringstream& out) const;
    void emitAttributes(std::stringstream& out) const;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
