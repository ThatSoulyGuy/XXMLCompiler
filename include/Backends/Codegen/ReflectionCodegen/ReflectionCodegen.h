#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/MetadataBuilder.h"
#include "Backends/LLVMIR/GlobalBuilder.h"
#include <string>
#include <memory>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Generates LLVM IR for reflection metadata using type-safe builders
 *
 * Produces type information, property descriptors, and method metadata
 * that can be queried at runtime through the XXML reflection API.
 *
 * This class uses MetadataBuilder to generate all reflection metadata
 * in a type-safe manner, instead of raw string emission.
 */
class ReflectionCodegen {
public:
    explicit ReflectionCodegen(CodegenContext& ctx);
    ~ReflectionCodegen() = default;

    // Non-copyable
    ReflectionCodegen(const ReflectionCodegen&) = delete;
    ReflectionCodegen& operator=(const ReflectionCodegen&) = delete;

    /// Generate all reflection metadata from collected class metadata.
    /// Metadata is added directly to the Module and will be emitted via LLVMEmitter.
    void generate();

    /**
     * Get generated IR as string.
     * @deprecated Metadata is now part of the Module, use LLVMEmitter instead.
     * Returns empty string for backwards compatibility.
     */
    std::string getIR() const { return ""; }

private:
    CodegenContext& ctx_;

    // Type-safe builders
    std::unique_ptr<LLVMIR::GlobalBuilder> globalBuilder_;
    std::unique_ptr<LLVMIR::MetadataBuilder> metadataBuilder_;

    /**
     * Convert CodegenContext's ReflectionClassMetadata to MetadataBuilder's ReflectionClassInfo
     */
    LLVMIR::ReflectionClassInfo convertMetadata(const ReflectionClassMetadata& metadata) const;

    /**
     * Convert ownership string to OwnershipKind enum
     */
    static LLVMIR::OwnershipKind parseOwnership(const std::string& ownership);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
