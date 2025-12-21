#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <sstream>

namespace XXML {
namespace Backends {
namespace Codegen {

/// Information about a runtime function in the XXML runtime library
struct RuntimeFunctionInfo {
    std::string name;           // e.g., "xxml_malloc"
    std::string returnType;     // LLVM type: "ptr", "i64", "void", etc.
    std::vector<std::string> paramTypes;  // Parameter LLVM types
    std::string category;       // For organization: "Memory", "Console", etc.
    std::string xxmlReturnType; // XXML return type for semantic analysis (optional)
    bool isDefinition = false;  // true if we provide inline LLVM IR definition
    std::string definition;     // Inline LLVM IR definition (if isDefinition is true)
};

/// Single source of truth for all XXML runtime functions
/// Consolidates information from:
///   - PreambleGen (LLVM IR declarations)
///   - SemanticAnalyzer intrinsicMethods_ (syscall signatures)
///   - CodegenContext preambleFunctions (return type mappings)
class RuntimeManifest {
public:
    RuntimeManifest();
    ~RuntimeManifest() = default;

    /// Get function info by name, returns nullptr if not found
    const RuntimeFunctionInfo* getFunction(const std::string& name) const;

    /// Get LLVM return type for a function
    std::string getLLVMReturnType(const std::string& name) const;

    /// Get XXML return type for a function (for semantic analysis)
    std::string getXXMLReturnType(const std::string& name) const;

    /// Check if a function is a known runtime function
    bool hasFunction(const std::string& name) const;

    /// Emit all function declarations as LLVM IR
    void emitDeclarations(std::stringstream& out) const;

    /// Emit function declarations for a specific category
    void emitDeclarations(std::stringstream& out, const std::string& category) const;

    /// Get all functions in a category
    std::vector<const RuntimeFunctionInfo*> getFunctionsInCategory(const std::string& category) const;

    /// Get all category names
    std::vector<std::string> getCategories() const;

    /// Get all function names
    std::vector<std::string> getAllFunctionNames() const;

private:
    void loadDefaults();

    // Helper to add a function declaration
    void addFunction(const std::string& name,
                     const std::string& returnType,
                     const std::vector<std::string>& paramTypes,
                     const std::string& category,
                     const std::string& xxmlReturnType = "");

    // Helper to add a function definition (inline LLVM IR)
    void addDefinition(const std::string& name,
                       const std::string& returnType,
                       const std::vector<std::string>& paramTypes,
                       const std::string& category,
                       const std::string& definition,
                       const std::string& xxmlReturnType = "");

    // All registered functions
    std::unordered_map<std::string, RuntimeFunctionInfo> functions_;

    // Category -> function names mapping for organized output
    std::unordered_map<std::string, std::vector<std::string>> categories_;

    // Ordered list of categories for consistent output
    std::vector<std::string> categoryOrder_;
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
