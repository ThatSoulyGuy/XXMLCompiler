#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include <string>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Generates LLVM IR for annotation metadata
 *
 * Produces metadata for retained annotations that can be
 * queried at runtime through the XXML reflection API.
 */
class AnnotationCodegen {
public:
    explicit AnnotationCodegen(CodegenContext& ctx);

    /// Generate all annotation metadata from collected annotations
    void generate();

    /// Get generated IR as string
    std::string getIR() const;

private:
    CodegenContext& ctx_;

    // Generated IR output
    std::stringstream output_;

    // Helper methods
    void emitLine(const std::string& line);
    std::string escapeString(const std::string& str) const;

    // Grouping structure for annotations by target
    struct TargetAnnotations {
        std::string targetType;
        std::string typeName;
        std::string memberName;
        std::vector<const PendingAnnotationMetadata*> annotations;
    };

    std::vector<TargetAnnotations> groupAnnotationsByTarget();
    void emitAnnotationGroup(int groupId, const TargetAnnotations& group);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
