#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/TypedModule.h"
#include <string>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Generates the LLVM IR preamble (declarations, built-in types, runtime functions)
 *
 * Responsible for emitting:
 * - Target triple and data layout
 * - Built-in struct types (Integer, String, Bool, Float, Double)
 * - Runtime function declarations (memory, string ops, console I/O)
 * - Reflection runtime declarations
 * - FFI runtime declarations
 * - Threading runtime declarations
 * - File I/O runtime declarations
 */
class PreambleGen {
public:
    explicit PreambleGen(CodegenContext& ctx);
    ~PreambleGen() = default;

    /// Generate complete preamble as string (for legacy emitter)
    std::string generate();

    /// Generate built-in types in the IR module
    void generateBuiltinTypes();

    /// Declare all runtime functions in the IR module
    void declareRuntimeFunctions();

    // Individual declaration groups (for fine-grained control)
    void declareMemoryFunctions();
    void declareIntegerFunctions();
    void declareFloatFunctions();
    void declareDoubleFunctions();
    void declareStringFunctions();
    void declareBoolFunctions();
    void declareConsoleFunctions();
    void declareReflectionFunctions();
    void declareAnnotationFunctions();
    void declareFFIFunctions();
    void declareThreadingFunctions();
    void declareFileFunctions();
    void declareUtilityFunctions();

    /// Get target triple string
    std::string getTargetTriple() const;

    /// Get data layout string
    std::string getDataLayout() const;

private:
    CodegenContext& ctx_;

    /// Helper to declare a function if not already declared
    void declareFunc(const std::string& name, LLVMIR::Type* retTy,
                     std::vector<LLVMIR::Type*> paramTys);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
