#pragma once

#include <string>
#include <vector>
#include "../Parser/AST.h"
#include "../Common/Error.h"

namespace XXML {
namespace Derive {

/**
 * DeriveCompiler - Compiles inline derive definitions to DLLs
 *
 * This class handles the auto-compilation of inline derives defined
 * using the [ Derive <Name> ] syntax. It generates standalone XXML source,
 * invokes the compiler in derive mode, and returns the DLL path.
 */
class DeriveCompiler {
public:
    /**
     * Information about a derive to be compiled
     */
    struct DeriveInfo {
        std::string deriveName;
        Parser::DeriveDecl* deriveDecl;
        std::vector<std::string> imports;  // Imports from the source file
        std::vector<Parser::ClassDecl*> userClasses;  // User-defined classes from the same file
    };

    /**
     * Result of derive compilation
     */
    struct CompilationResult {
        bool success;
        std::string dllPath;
        std::string errorMessage;
    };

    /**
     * Constructor
     * @param compilerPath Path to the xxml compiler executable
     * @param tempDir Directory for temporary files (defaults to system temp)
     */
    DeriveCompiler(const std::string& compilerPath, const std::string& tempDir = "");

    /**
     * Compile an inline derive to a DLL
     * @param info Information about the derive to compile
     * @param errorReporter Error reporter for compilation errors
     * @return CompilationResult with success status and DLL path
     */
    CompilationResult compileDerive(const DeriveInfo& info, Common::ErrorReporter& errorReporter);

    /**
     * Generate standalone XXML source for a derive
     * @param info Derive information
     * @return Generated XXML source code
     */
    std::string generateDeriveSource(const DeriveInfo& info);

    /**
     * Clean up temporary files created during compilation
     */
    void cleanup();

private:
    std::string compilerPath_;
    std::string tempDir_;
    std::vector<std::string> tempFiles_;  // Track temp files for cleanup

    /**
     * Serialize a DeriveDecl to XXML syntax
     */
    std::string serializeDerive(Parser::DeriveDecl* derive);

    /**
     * Serialize a ClassDecl to XXML syntax
     */
    std::string serializeClass(Parser::ClassDecl* classDecl);

    /**
     * Serialize a MethodDecl to XXML syntax
     */
    std::string serializeMethod(Parser::MethodDecl* method, int indent);

    /**
     * Serialize a statement to XXML syntax
     */
    std::string serializeStatement(Parser::Statement* stmt, int indent);

    /**
     * Serialize an expression to XXML syntax
     */
    std::string serializeExpression(Parser::Expression* expr);

    /**
     * Escape special characters in a string for XXML output
     */
    std::string escapeString(const std::string& str);

    /**
     * Get a unique temp file path
     */
    std::string getTempFilePath(const std::string& baseName, const std::string& extension);
};

} // namespace Derive
} // namespace XXML
