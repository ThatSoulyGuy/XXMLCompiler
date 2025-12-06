#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Backends/LLVMIR/GlobalBuilder.h"
#include <string>
#include <memory>
#include <vector>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Generates LLVM IR for annotation metadata using type-safe builders
 *
 * Produces metadata for retained annotations that can be
 * queried at runtime through the XXML reflection API.
 *
 * This class uses GlobalBuilder to generate all annotation metadata
 * in a type-safe manner, instead of raw string emission.
 */
class AnnotationCodegen {
public:
    explicit AnnotationCodegen(CodegenContext& ctx);
    ~AnnotationCodegen() = default;

    // Non-copyable
    AnnotationCodegen(const AnnotationCodegen&) = delete;
    AnnotationCodegen& operator=(const AnnotationCodegen&) = delete;

    /// Generate all annotation metadata from collected annotations.
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

    // Type-safe builder
    std::unique_ptr<LLVMIR::GlobalBuilder> globalBuilder_;

    // Grouping structure for annotations by target
    struct TargetAnnotations {
        std::string targetType;
        std::string typeName;
        std::string memberName;
        std::vector<const PendingAnnotationMetadata*> annotations;
    };

    std::vector<TargetAnnotations> groupAnnotationsByTarget();
    void generateAnnotationGroup(int groupId, const TargetAnnotations& group);

    // Struct types for annotation metadata
    LLVMIR::StructType* annotationArgType_ = nullptr;
    LLVMIR::StructType* annotationInfoType_ = nullptr;

    void initializeAnnotationTypes();
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
