#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include <string>
#include <sstream>
#include <unordered_map>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Generates LLVM IR for reflection metadata
 *
 * Produces type information, property descriptors, and method metadata
 * that can be queried at runtime through the XXML reflection API.
 */
class ReflectionCodegen {
public:
    explicit ReflectionCodegen(CodegenContext& ctx);

    /// Generate all reflection metadata from collected class metadata
    void generate();

    /// Get generated IR as string (appends to module's output)
    std::string getIR() const;

private:
    CodegenContext& ctx_;

    // String label tracking for deduplication
    std::unordered_map<std::string, std::string> stringLabelMap_;
    int stringCounter_ = 0;

    // Generated IR output
    std::stringstream output_;

    // Helper methods
    void emitLine(const std::string& line);
    std::string escapeString(const std::string& str) const;
    int32_t ownershipToInt(const std::string& ownership) const;
    std::string emitStringLiteral(const std::string& content, const std::string& prefix);
    std::string mangleName(const std::string& fullName) const;

    // Generation phases
    void emitReflectionStructTypes();
    void emitStringLiterals(const ReflectionClassMetadata& metadata);
    void emitPropertyArray(const std::string& mangledName, const ReflectionClassMetadata& metadata);
    void emitMethodParameterArrays(const std::string& mangledName, const ReflectionClassMetadata& metadata);
    void emitMethodArray(const std::string& mangledName, const ReflectionClassMetadata& metadata);
    void emitTemplateParamArray(const std::string& mangledName, const ReflectionClassMetadata& metadata);
    void emitTypeInfo(const std::string& mangledName, const ReflectionClassMetadata& metadata);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
