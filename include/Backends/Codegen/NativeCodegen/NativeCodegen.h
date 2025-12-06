#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Parser/AST.h"
#include <string>
#include <sstream>
#include <vector>

namespace XXML {

namespace Core { class CompilationContext; }

namespace Backends {
namespace Codegen {

/**
 * @brief Handles FFI native method thunk generation
 *
 * This class generates LLVM IR thunks for native methods that call
 * external DLL functions. It handles:
 * - DLL loading via xxml_FFI_loadLibrary
 * - Symbol lookup via xxml_FFI_getSymbol
 * - Parameter and return type marshalling
 * - Calling convention handling
 */
class NativeCodegen {
public:
    NativeCodegen(CodegenContext& ctx, Core::CompilationContext* compCtx);
    ~NativeCodegen() = default;

    // Non-copyable
    NativeCodegen(const NativeCodegen&) = delete;
    NativeCodegen& operator=(const NativeCodegen&) = delete;

    /**
     * Generate a native FFI thunk for a method with @NativeFunction annotation.
     * The thunk loads the DLL, gets the symbol, and calls the native function.
     *
     * @param node The method declaration with native function info
     * @param className Current class name (empty for top-level functions)
     * @param namespaceName Current namespace
     */
    void generateNativeThunk(Parser::MethodDecl& node,
                            const std::string& className,
                            const std::string& namespaceName);

    /**
     * Get the generated LLVM IR text for all thunks.
     */
    std::string getIR() const { return output_.str(); }

    /**
     * Get string literals that need to be emitted as globals.
     * Returns pairs of (label, content).
     */
    const std::vector<std::pair<std::string, std::string>>& stringLiterals() const {
        return stringLiterals_;
    }

    /**
     * Clear generated IR and reset state.
     */
    void reset();

private:
    CodegenContext& ctx_;
    Core::CompilationContext* compCtx_;

    // Text-based IR output
    std::stringstream output_;
    int registerCounter_ = 0;
    int labelCounter_ = 0;

    // String literals for FFI paths and symbols
    std::vector<std::pair<std::string, std::string>> stringLiterals_;

    // Helper methods
    void emitLine(const std::string& line);
    std::string allocateRegister();
    std::string allocateLabel(const std::string& prefix);

    /**
     * Get qualified function name (Class_Method format)
     */
    std::string getQualifiedName(const std::string& className, const std::string& methodName) const;

    /**
     * Map XXML type to LLVM type string
     */
    std::string getLLVMType(const std::string& xxmlType) const;

    /**
     * Map NativeType<...> to LLVM type
     */
    static std::string mapNativeTypeToLLVM(const std::string& nativeType);

    /**
     * Get LLVM calling convention string
     */
    static std::string getLLVMCallingConvention(Parser::CallingConvention conv);

    /**
     * Generate default return value for a type
     */
    static std::string getDefaultReturnValue(const std::string& llvmType);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
