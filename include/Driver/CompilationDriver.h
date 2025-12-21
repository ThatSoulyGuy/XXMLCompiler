#pragma once

#include <string>
#include <vector>
#include <memory>

namespace XXML {

namespace Core { class CompilationContext; }
namespace Parser { class Program; }
namespace Backends { class LLVMBackend; }
namespace Linker { class ILinker; }
namespace Common { class ErrorReporter; }
namespace Semantic { class SemanticAnalyzer; }

namespace Driver {

/**
 * Compilation mode
 */
enum class CompilationMode {
    FullPipeline,      // Parse → TypeCheck → CodeGen → ObjectGen → Link → Executable
    CompileOnly,       // Parse → TypeCheck → CodeGen → ObjectGen → .o/.obj file
    EmitLLVM,          // Parse → TypeCheck → CodeGen → .ll file
    LinkOnly           // Link existing .o/.obj files → Executable
};

/**
 * Target platform
 */
enum class TargetPlatform {
    Native,            // Detect host platform
    Windows_x64,       // Windows x86-64
    Linux_x64,         // Linux x86-64
    MacOS_x64,         // macOS x86-64
    MacOS_ARM64        // macOS Apple Silicon
};

/**
 * Compilation configuration
 */
struct CompilationConfig {
    // Input/Output
    std::string inputFile;                     // XXML source file
    std::string outputFile;                    // Output file (executable, .o, or .ll)

    // Compilation mode
    CompilationMode mode = CompilationMode::FullPipeline;

    // Target platform
    TargetPlatform target = TargetPlatform::Native;

    // Optimization
    int optimizationLevel = 0;                 // 0-3 (O0, O1, O2, O3)

    // Linking
    std::vector<std::string> libraryPaths;     // Additional library search paths
    std::vector<std::string> libraries;        // Additional libraries to link
    std::vector<std::string> objectFiles;      // Additional object files to link

    // Runtime library
    std::string runtimeLibraryPath;            // Path to XXML runtime library

    // Options
    bool verbose = false;                      // Print detailed compilation steps
    bool keepTemporaryFiles = false;           // Don't delete .ll and .o files
    bool emitLLVMIR = false;                   // Also emit .ll file alongside executable

    // Backend selection
    bool useCppBackend = false;                // Use C++ backend instead of LLVM
};

/**
 * Compilation result
 */
struct CompilationResult {
    bool success;
    std::string outputPath;
    std::string errorMessage;
    int exitCode;

    // Intermediate file paths (if kept)
    std::string llvmIRPath;
    std::string objectFilePath;

    CompilationResult() : success(false), exitCode(-1) {}
};

/**
 * Main compilation driver
 * Orchestrates the full compilation pipeline
 */
class CompilationDriver {
public:
    explicit CompilationDriver(const CompilationConfig& config);
    ~CompilationDriver() = default;

    /**
     * Execute the compilation pipeline
     * @return CompilationResult with success status and paths
     */
    CompilationResult compile();

private:
    CompilationConfig config_;
    std::unique_ptr<Core::CompilationContext> context_;
    std::unique_ptr<Backends::LLVMBackend> llvmBackend_;
    std::unique_ptr<Linker::ILinker> linker_;
    std::unique_ptr<Common::ErrorReporter> errorReporter_;
    std::unique_ptr<Parser::Program> program_;
    std::unique_ptr<Semantic::SemanticAnalyzer> analyzer_;

    // Pipeline stages
    bool parseAndTypeCheck(std::string& errorMessage);
    bool generateLLVMIR(std::string& irCode, std::string& errorMessage);
    bool generateObjectFile(const std::string& irCode,
                           const std::string& objectPath,
                           std::string& errorMessage);
    bool linkExecutable(const std::string& objectPath,
                       const std::string& executablePath,
                       std::string& errorMessage);

    // Helper methods
    std::string getDefaultOutputPath() const;
    std::string getRuntimeLibraryPath() const;
    std::string getObjectFilePath() const;
    std::string getLLVMIRPath() const;
    void logVerbose(const std::string& message) const;
    void cleanup();

    // Temporary files to clean up
    std::vector<std::string> temporaryFiles_;
};

} // namespace Driver
} // namespace XXML
