#pragma once

#include "Backends/Codegen/CodegenContext.h"
#include "Parser/AST.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace XXML {
namespace Backends {
namespace Codegen {

/**
 * @brief Metadata for a single class/type for reflection
 */
struct ReflectionMetadata {
    std::string name;
    std::string namespaceName;
    std::string fullName;
    bool isTemplate = false;
    std::vector<std::string> templateParams;

    // Properties: (name, typeName)
    std::vector<std::pair<std::string, std::string>> properties;
    std::vector<std::string> propertyOwnerships;

    // Methods: (name, returnType)
    std::vector<std::pair<std::string, std::string>> methods;
    std::vector<std::string> methodReturnOwnerships;

    // Method parameters: for each method, list of (name, type, ownership)
    std::vector<std::vector<std::tuple<std::string, std::string, std::string>>> methodParameters;

    int64_t instanceSize = 0;
};

/**
 * @brief Generates reflection metadata for classes
 *
 * Responsible for:
 * - Collecting type metadata from class declarations
 * - Generating ReflectionTypeInfo structures
 * - Generating property, method, and parameter info arrays
 * - Emitting __reflection_init() function to register types
 * - Setting up llvm.global_ctors for automatic initialization
 */
class MetadataGen {
public:
    explicit MetadataGen(CodegenContext& ctx);
    ~MetadataGen() = default;

    /// Register metadata for a class
    void registerClass(Parser::ClassDecl* classDecl, const std::string& namespaceName = "");

    /// Get registered metadata for a type
    const ReflectionMetadata* getMetadata(const std::string& fullName) const;

    /// Generate all reflection metadata
    void generate();

    /// Generate reflection struct type definitions
    void generateStructTypes();

    /// Check if any metadata is registered
    bool hasMetadata() const { return !metadata_.empty(); }

    /// Get all registered metadata
    const std::unordered_map<std::string, ReflectionMetadata>& getAllMetadata() const {
        return metadata_;
    }

private:
    CodegenContext& ctx_;
    std::unordered_map<std::string, ReflectionMetadata> metadata_;
    std::unordered_map<std::string, std::string> stringLabelMap_;
    int stringCounter_ = 0;

    /// Create or get a global string constant
    LLVMIR::PtrValue getOrCreateString(const std::string& content, const std::string& prefix);

    /// Generate property info array for a class
    void generatePropertyArray(const std::string& mangledName, const ReflectionMetadata& meta);

    /// Generate method info array for a class
    void generateMethodArray(const std::string& mangledName, const ReflectionMetadata& meta);

    /// Generate parameter arrays for all methods
    void generateParameterArrays(const std::string& mangledName, const ReflectionMetadata& meta);

    /// Generate the ReflectionTypeInfo global for a class
    void generateTypeInfo(const std::string& mangledName, const ReflectionMetadata& meta);

    /// Generate __reflection_init() function
    void generateInitFunction();

    /// Convert ownership string to integer enum value
    static int32_t ownershipToInt(const std::string& ownership);

    /// Mangle a type name for use as LLVM identifier
    static std::string mangleName(const std::string& name);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
