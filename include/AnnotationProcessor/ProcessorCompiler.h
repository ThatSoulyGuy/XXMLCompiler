#pragma once

#include <string>
#include <vector>
#include "../Parser/AST.h"
#include "../Common/Error.h"

namespace XXML {
namespace AnnotationProcessor {

/**
 * ProcessorCompiler - Compiles inline annotation processors to DLLs
 *
 * This class handles the auto-compilation of inline processors defined
 * within annotation declarations. It generates standalone XXML source,
 * invokes the compiler in processor mode, and returns the DLL path.
 */
class ProcessorCompiler {
public:
    /**
     * Information about a processor to be compiled
     */
    struct ProcessorInfo {
        std::string annotationName;
        Parser::AnnotationDecl* annotDecl;
        Parser::ProcessorDecl* processorDecl;
        std::vector<std::string> imports;  // Imports from the source file
        std::vector<Parser::ClassDecl*> userClasses;  // User-defined classes from the same file
    };

    /**
     * Result of processor compilation
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
    ProcessorCompiler(const std::string& compilerPath, const std::string& tempDir = "");

    /**
     * Compile an inline processor to a DLL
     * @param info Information about the processor to compile
     * @param errorReporter Error reporter for compilation errors
     * @return CompilationResult with success status and DLL path
     */
    CompilationResult compileProcessor(const ProcessorInfo& info, Common::ErrorReporter& errorReporter);

    /**
     * Generate standalone XXML source for a processor
     * @param info Processor information
     * @return Generated XXML source code
     */
    std::string generateProcessorSource(const ProcessorInfo& info);

    /**
     * Clean up temporary files created during compilation
     */
    void cleanup();

private:
    std::string compilerPath_;
    std::string tempDir_;
    std::vector<std::string> tempFiles_;  // Track temp files for cleanup

    /**
     * Serialize a ProcessorDecl to XXML syntax
     */
    std::string serializeProcessor(Parser::ProcessorDecl* proc);

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
     * Get a unique temp file path
     */
    std::string getTempFilePath(const std::string& baseName, const std::string& extension);
};

} // namespace AnnotationProcessor
} // namespace XXML
