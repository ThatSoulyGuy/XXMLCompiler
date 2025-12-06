#pragma once

#include "Parser/AST.h"
#include <string>
#include <functional>

namespace XXML {
namespace Backends {
namespace Codegen {

// Forward declarations
class CodegenContext;
class DeclCodegen;

/**
 * @brief Handles module import processing and reflection metadata collection
 *
 * This class provides functionality to:
 * 1. Collect reflection metadata from imported modules
 * 2. Generate code for imported modules (delegating to DeclCodegen)
 *
 * It manages namespace context during module processing.
 */
class ModuleCodegen {
public:
    /**
     * @param ctx The shared codegen context
     * @param declCodegen Reference to DeclCodegen for code generation
     */
    ModuleCodegen(CodegenContext& ctx, DeclCodegen& declCodegen);
    ~ModuleCodegen() = default;

    // Non-copyable
    ModuleCodegen(const ModuleCodegen&) = delete;
    ModuleCodegen& operator=(const ModuleCodegen&) = delete;

    /**
     * Collect reflection metadata from a module WITHOUT generating code.
     * This is used for runtime modules where we need reflection info
     * but the actual implementations are provided by the runtime library.
     *
     * @param program The program/module to collect metadata from
     */
    void collectReflectionMetadata(Parser::Program& program);

    /**
     * Generate code for an imported module.
     * Processes classes, methods, enumerations, and native structures.
     * Skips entrypoints and import declarations.
     *
     * @param program The program/module to generate code for
     * @param moduleName The name of the module (used as default namespace)
     */
    void generateImportedModuleCode(Parser::Program& program, const std::string& moduleName = "");

private:
    CodegenContext& ctx_;
    DeclCodegen& declCodegen_;

    /**
     * Helper to collect metadata from a single class
     */
    void collectClassMetadata(Parser::ClassDecl* classDecl, const std::string& namespaceName);

    /**
     * Recursive helper to process namespace declarations for metadata collection
     */
    void processNamespaceForMetadata(Parser::NamespaceDecl* ns, const std::string& parentNamespace);

    /**
     * Recursive helper to process namespace declarations for code generation
     */
    void processNamespaceForCodegen(Parser::NamespaceDecl* ns);

    /**
     * Convert ownership type enum to string representation
     */
    static std::string ownershipToString(Parser::OwnershipType ownership);
};

} // namespace Codegen
} // namespace Backends
} // namespace XXML
