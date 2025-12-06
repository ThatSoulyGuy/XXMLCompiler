#pragma once

#include <string>
#include <sstream>

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

private:
    TargetPlatform platform_;

    // Helper methods for generating preamble sections
    void emitHeader(std::stringstream& out) const;
    void emitTargetInfo(std::stringstream& out) const;
    void emitBuiltinTypes(std::stringstream& out) const;
    void emitMemoryManagement(std::stringstream& out) const;
    void emitIntegerOperations(std::stringstream& out) const;
    void emitFloatOperations(std::stringstream& out) const;
    void emitDoubleOperations(std::stringstream& out) const;
    void emitStringOperations(std::stringstream& out) const;
    void emitBoolOperations(std::stringstream& out) const;
    void emitListOperations(std::stringstream& out) const;
    void emitConsoleIO(std::stringstream& out) const;
    void emitSystemFunctions(std::stringstream& out) const;
    void emitReflectionRuntime(std::stringstream& out) const;
    void emitAnnotationRuntime(std::stringstream& out) const;
    void emitProcessorAPI(std::stringstream& out) const;
    void emitDynamicValueMethods(std::stringstream& out) const;
    void emitUtilityFunctions(std::stringstream& out) const;
    void emitThreadingFunctions(std::stringstream& out) const;
    void emitMutexFunctions(std::stringstream& out) const;
    void emitConditionVariableFunctions(std::stringstream& out) const;
    void emitAtomicFunctions(std::stringstream& out) const;
    void emitTLSFunctions(std::stringstream& out) const;
    void emitFileIOFunctions(std::stringstream& out) const;
    void emitDirectoryFunctions(std::stringstream& out) const;
    void emitPathFunctions(std::stringstream& out) const;
    void emitFFIRuntime(std::stringstream& out) const;
    void emitWindowsDLLFunctions(std::stringstream& out) const;
    void emitAttributes(std::stringstream& out) const;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
