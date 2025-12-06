#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/FFIBuilder.h"
#include "Backends/LLVMIR/GlobalBuilder.h"
#include "Parser/AST.h"
#include <string>
#include <vector>
#include <memory>

namespace XXML {

namespace Core { class CompilationContext; }

namespace Backends {
namespace Codegen {

/**
 * @brief Handles FFI native method thunk generation using type-safe builders
 *
 * This class generates LLVM IR thunks for native methods that call
 * external DLL functions. It uses the typed IR system (FFIBuilder)
 * instead of raw string emission.
 *
 * Thunks are added directly to the Module and emitted via LLVMEmitter.
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
     * The thunk is added to the Module and will be emitted via LLVMEmitter.
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
     * @deprecated Thunks are now part of the Module, use LLVMEmitter instead.
     * Returns empty string for backwards compatibility.
     */
    std::string getIR() const { return ""; }

    /**
     * Get string literals that need to be emitted as globals.
     * @deprecated String literals are now added to the Module directly.
     * Returns empty vector for backwards compatibility.
     */
    const std::vector<std::pair<std::string, std::string>>& stringLiterals() const {
        return emptyStringLiterals_;
    }

    /**
     * Clear generated state (no-op, state is in Module).
     */
    void reset() {}

private:
    CodegenContext& ctx_;
    Core::CompilationContext* compCtx_;

    // Type-safe builders
    std::unique_ptr<LLVMIR::GlobalBuilder> globalBuilder_;
    std::unique_ptr<LLVMIR::FFIBuilder> ffiBuilder_;

    // Empty vector for backwards compatibility
    std::vector<std::pair<std::string, std::string>> emptyStringLiterals_;

    /**
     * Get qualified function name (Class_Method format)
     */
    std::string getQualifiedName(const std::string& className, const std::string& methodName) const;

    /**
     * Map XXML type to LLVM Type*
     */
    LLVMIR::Type* getLLVMType(const std::string& xxmlType) const;

    /**
     * Map NativeType<...> to LLVM type string, then to Type*
     */
    std::string mapNativeTypeToLLVMString(const std::string& nativeType) const;

    /**
     * Parse an LLVM type string to Type*
     */
    LLVMIR::Type* parseTypeString(const std::string& typeStr, LLVMIR::TypeContext& typeCtx) const;

    /**
     * Convert Parser::CallingConvention to LLVMIR::FFICallingConv
     */
    static LLVMIR::FFICallingConv mapCallingConvention(Parser::CallingConvention conv);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
