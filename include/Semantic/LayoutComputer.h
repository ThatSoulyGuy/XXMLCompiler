#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include "../Parser/AST.h"
#include "../Common/Error.h"
#include "PassResults.h"
#include "SemanticError.h"

namespace XXML {
namespace Semantic {

//==============================================================================
// LAYOUT COMPUTER
//
// This pass computes all class/struct layouts before IR generation.
// It performs:
//   1. Field offset calculation with alignment
//   2. Total size computation with trailing padding
//   3. Inheritance layout handling
//   4. Reflection metadata validation
//
// After this pass completes successfully:
//   - All property lists are finalized
//   - All offsets are computed
//   - All alignments are fixed
//   - Base class fields are laid out
//   - Reflection metadata matches layout
//==============================================================================

class LayoutComputer {
public:
    LayoutComputer(Common::ErrorReporter& errorReporter,
                   const TypeResolutionResult& typeResolution,
                   const SemanticValidationResult& semanticResult);

    // Main entry point
    LayoutComputationResult run(
        const std::unordered_map<std::string, ClassInfo>& classRegistry);

    // Get the result
    const LayoutComputationResult& result() const { return result_; }

    // Query computed layouts
    const ClassLayout* getLayout(const std::string& className) const;
    const ReflectionMetadataInfo* getMetadata(const std::string& className) const;

    // Size and alignment queries
    size_t getTypeSize(const std::string& typeName) const;
    size_t getTypeAlignment(const std::string& typeName) const;

private:
    Common::ErrorReporter& errorReporter_;
    const TypeResolutionResult& typeResolution_;
    const SemanticValidationResult& semanticResult_;
    LayoutComputationResult result_;

    // Dependency tracking for layout order
    std::unordered_map<std::string, std::vector<std::string>> dependencies_;
    std::vector<std::string> layoutOrder_;

    // Compute layout for a single class
    ClassLayout computeLayout(const std::string& className,
                              const ClassInfo& classInfo);

    // Compute field layout
    FieldLayout computeFieldLayout(const std::string& propName,
                                   const std::string& propType,
                                   Parser::OwnershipType ownership,
                                   size_t& currentOffset);

    // Generate reflection metadata
    ReflectionMetadataInfo generateMetadata(const std::string& className,
                                            const ClassInfo& classInfo,
                                            const ClassLayout& layout);

    // Size/alignment helpers
    size_t getPrimitiveSize(const std::string& typeName) const;
    size_t getPrimitiveAlignment(const std::string& typeName) const;
    size_t getPointerSize() const { return 8; }  // 64-bit
    size_t getPointerAlignment() const { return 8; }

    // Alignment calculation
    size_t alignTo(size_t offset, size_t alignment) const {
        return (offset + alignment - 1) & ~(alignment - 1);
    }

    // Dependency resolution
    void buildDependencyGraph(
        const std::unordered_map<std::string, ClassInfo>& classRegistry);
    std::vector<std::string> topologicalSort();
    void detectCircularInheritance();

    // Validation
    void validateLayout(const std::string& className, const ClassLayout& layout);
    void validateReflectionMetadata(const std::string& className,
                                    const ReflectionMetadataInfo& metadata,
                                    const ClassLayout& layout);
};

} // namespace Semantic
} // namespace XXML
