#include "Driver/CompilationDriver.h"
#include "Core/CompilationContext.h"
#include "Backends/LLVMBackend.h"
#include "Linker/LinkerInterface.h"
#include "Utils/ProcessUtils.h"
#include <iostream>
#include <fstream>
#include <filesystem>

namespace XXML {
namespace Driver {

CompilationDriver::CompilationDriver(const CompilationConfig& config)
    : config_(config) {

    // Initialize compilation context
    context_ = std::make_unique<Core::CompilationContext>();

    // Initialize LLVM backend if not using C++ backend
    if (!config_.useCppBackend) {
        llvmBackend_ = std::make_unique<Backends::LLVMBackend>(context_.get());
    }

    // Initialize linker
    linker_ = Linker::LinkerFactory::createLinker();
    if (!linker_) {
        std::cerr << "Warning: No suitable linker found on this system" << std::endl;
    }
}

CompilationResult CompilationDriver::compile() {
    CompilationResult result;

    logVerbose("=== XXML Compilation Driver ===");
    logVerbose("Input: " + config_.inputFile);
    logVerbose("Mode: " + std::to_string(static_cast<int>(config_.mode)));
    logVerbose("Optimization: -O" + std::to_string(config_.optimizationLevel));
    logVerbose("");

    // Stage 1: Parse and type check
    logVerbose("[1/4] Parsing and type checking...");
    if (!parseAndTypeCheck(result.errorMessage)) {
        result.success = false;
        result.exitCode = 1;
        return result;
    }
    logVerbose("✓ Parse and type check complete");

    // Stage 2: Generate LLVM IR
    std::string irCode;
    logVerbose("[2/4] Generating LLVM IR...");
    if (!generateLLVMIR(irCode, result.errorMessage)) {
        result.success = false;
        result.exitCode = 2;
        return result;
    }

    // Determine LLVM IR path
    std::string llvmIRPath = getLLVMIRPath();
    if (config_.mode == CompilationMode::EmitLLVM || config_.emitLLVMIR) {
        // Write LLVM IR to file
        std::ofstream llFile(llvmIRPath);
        if (!llFile) {
            result.errorMessage = "Failed to write LLVM IR file: " + llvmIRPath;
            result.success = false;
            result.exitCode = 2;
            return result;
        }
        llFile << irCode;
        llFile.close();
        result.llvmIRPath = llvmIRPath;

        logVerbose("✓ LLVM IR generated: " + llvmIRPath);

        if (config_.mode == CompilationMode::EmitLLVM) {
            // Only emit LLVM IR, stop here
            result.success = true;
            result.outputPath = llvmIRPath;
            result.exitCode = 0;
            return result;
        }
    }

    // Stage 3: Generate object file
    std::string objectPath = getObjectFilePath();
    logVerbose("[3/4] Generating object file...");
    if (!generateObjectFile(irCode, objectPath, result.errorMessage)) {
        cleanup();
        result.success = false;
        result.exitCode = 3;
        return result;
    }
    result.objectFilePath = objectPath;
    logVerbose("✓ Object file generated: " + objectPath);

    if (config_.mode == CompilationMode::CompileOnly) {
        // Only compile, don't link
        result.success = true;
        result.outputPath = objectPath;
        result.exitCode = 0;

        // Keep object file, but clean up LLVM IR if not requested
        if (!config_.emitLLVMIR && !config_.keepTemporaryFiles) {
            if (!llvmIRPath.empty() && Utils::ProcessUtils::fileExists(llvmIRPath)) {
                temporaryFiles_.push_back(llvmIRPath);
            }
        }

        cleanup();
        return result;
    }

    // Stage 4: Link executable
    std::string executablePath = config_.outputFile.empty() ?
        getDefaultOutputPath() : config_.outputFile;

    logVerbose("[4/4] Linking executable...");
    if (!linkExecutable(objectPath, executablePath, result.errorMessage)) {
        cleanup();
        result.success = false;
        result.exitCode = 4;
        return result;
    }
    logVerbose("✓ Executable linked: " + executablePath);

    // Success!
    result.success = true;
    result.outputPath = executablePath;
    result.exitCode = 0;

    // Cleanup temporary files
    if (!config_.keepTemporaryFiles) {
        if (!config_.emitLLVMIR && !llvmIRPath.empty()) {
            temporaryFiles_.push_back(llvmIRPath);
        }
        temporaryFiles_.push_back(objectPath);
    }

    cleanup();

    logVerbose("");
    logVerbose("=== Compilation successful! ===");
    logVerbose("Output: " + result.outputPath);

    return result;
}

bool CompilationDriver::parseAndTypeCheck(std::string& errorMessage) {
    // TODO: Integrate with existing parser and type checker from main.cpp
    // For now, this is a placeholder that would:
    // 1. Load and parse the input file
    // 2. Resolve imports
    // 3. Run type checking
    // 4. Build the AST

    // Check if input file exists
    if (!Utils::ProcessUtils::fileExists(config_.inputFile)) {
        errorMessage = "Input file not found: " + config_.inputFile;
        return false;
    }

    // This would be implemented by calling the existing parser/typechecker
    // from the main compilation context
    return true;
}

bool CompilationDriver::generateLLVMIR(std::string& irCode, std::string& errorMessage) {
    if (!llvmBackend_) {
        errorMessage = "LLVM backend not initialized";
        return false;
    }

    // TODO: Get the AST from the compilation context and generate IR
    // For now, this is a placeholder
    // In real implementation:
    // irCode = llvmBackend_->generate(*program);

    errorMessage = "LLVM IR generation not yet integrated with parser";
    return false;
}

bool CompilationDriver::generateObjectFile(const std::string& irCode,
                                          const std::string& objectPath,
                                          std::string& errorMessage) {
    if (!llvmBackend_) {
        errorMessage = "LLVM backend not initialized";
        return false;
    }

    logVerbose("  Invoking LLVM tools to generate object file...");

    bool success = llvmBackend_->generateObjectFile(
        irCode,
        objectPath,
        config_.optimizationLevel
    );

    if (!success) {
        errorMessage = "Failed to generate object file";
        return false;
    }

    if (!Utils::ProcessUtils::fileExists(objectPath)) {
        errorMessage = "Object file was not created: " + objectPath;
        return false;
    }

    return true;
}

bool CompilationDriver::linkExecutable(const std::string& objectPath,
                                      const std::string& executablePath,
                                      std::string& errorMessage) {
    if (!linker_) {
        errorMessage = "No linker available on this system";
        return false;
    }

    logVerbose("  Using linker: " + linker_->name());

    // Build link configuration
    Linker::LinkConfig linkConfig;
    linkConfig.objectFiles.push_back(objectPath);

    // Add any additional object files
    for (const auto& obj : config_.objectFiles) {
        linkConfig.objectFiles.push_back(obj);
    }

    linkConfig.outputPath = executablePath;
    linkConfig.verbose = config_.verbose;
    linkConfig.optimizationLevel = config_.optimizationLevel;

    // Add runtime library
    std::string runtimePath = getRuntimeLibraryPath();
    if (!runtimePath.empty()) {
        // Extract directory and library name
        std::filesystem::path runtimePathObj(runtimePath);
        std::string libraryDir = runtimePathObj.parent_path().string();
        std::string libraryName = runtimePathObj.filename().string();

        // Remove "lib" prefix and extension for GNU linker
        if (libraryName.find("lib") == 0) {
            libraryName = libraryName.substr(3);
        }
        if (libraryName.find(".a") != std::string::npos) {
            libraryName = libraryName.substr(0, libraryName.find(".a"));
        }
        if (libraryName.find(".lib") != std::string::npos) {
            libraryName = libraryName.substr(0, libraryName.find(".lib"));
        }

        linkConfig.libraryPaths.push_back(libraryDir);
        linkConfig.libraries.push_back(libraryName);

        logVerbose("  Runtime library: " + runtimePath);
    } else {
        logVerbose("  Warning: Runtime library not found, linking may fail");
    }

    // Add user-specified libraries
    for (const auto& libPath : config_.libraryPaths) {
        linkConfig.libraryPaths.push_back(libPath);
    }
    for (const auto& lib : config_.libraries) {
        linkConfig.libraries.push_back(lib);
    }

    // Execute linker
    Linker::LinkResult linkResult = linker_->link(linkConfig);

    if (!linkResult.success) {
        errorMessage = "Linking failed: " + linkResult.error;
        return false;
    }

    return true;
}

std::string CompilationDriver::getDefaultOutputPath() const {
    std::filesystem::path inputPath(config_.inputFile);
    std::string baseName = inputPath.stem().string();

    if (linker_) {
        return baseName + linker_->executableExtension();
    }

#ifdef _WIN32
    return baseName + ".exe";
#else
    return baseName;
#endif
}

std::string CompilationDriver::getRuntimeLibraryPath() const {
    using namespace Utils;

    // 1. Check user-specified path
    if (!config_.runtimeLibraryPath.empty()) {
        if (ProcessUtils::fileExists(config_.runtimeLibraryPath)) {
            return config_.runtimeLibraryPath;
        }
    }

    // 2. Check relative to compiler executable
    std::string exeDir = ProcessUtils::getExecutableDirectory();
    std::vector<std::string> candidates = {
        ProcessUtils::joinPath({exeDir, "..", "lib", "libXXMLLLVMRuntime.a"}),
        ProcessUtils::joinPath({exeDir, "..", "lib", "XXMLLLVMRuntime.lib"}),
        ProcessUtils::joinPath({exeDir, "lib", "libXXMLLLVMRuntime.a"}),
        ProcessUtils::joinPath({exeDir, "lib", "XXMLLLVMRuntime.lib"}),
    };

    for (const auto& candidate : candidates) {
        if (ProcessUtils::fileExists(candidate)) {
            return candidate;
        }
    }

    // 3. Check standard installation paths
#ifdef _WIN32
    // Windows: Check Program Files
    const char* programFiles = getenv("ProgramFiles");
    if (programFiles) {
        std::string path = ProcessUtils::joinPath({
            std::string(programFiles),
            "XXML", "lib", "XXMLLLVMRuntime.lib"
        });
        if (ProcessUtils::fileExists(path)) {
            return path;
        }
    }
#else
    // Unix: Check /usr/local and /usr
    std::vector<std::string> unixPaths = {
        "/usr/local/lib/libXXMLLLVMRuntime.a",
        "/usr/lib/libXXMLLLVMRuntime.a",
        "/usr/local/share/xxml/runtime/libXXMLLLVMRuntime.a"
    };

    for (const auto& path : unixPaths) {
        if (ProcessUtils::fileExists(path)) {
            return path;
        }
    }
#endif

    // Not found
    return "";
}

std::string CompilationDriver::getObjectFilePath() const {
    std::filesystem::path inputPath(config_.inputFile);
    std::string baseName = inputPath.stem().string();

    if (linker_) {
        return baseName + linker_->objectFileExtension();
    }

#ifdef _WIN32
    return baseName + ".obj";
#else
    return baseName + ".o";
#endif
}

std::string CompilationDriver::getLLVMIRPath() const {
    std::filesystem::path inputPath(config_.inputFile);
    std::string baseName = inputPath.stem().string();
    return baseName + ".ll";
}

void CompilationDriver::logVerbose(const std::string& message) const {
    if (config_.verbose) {
        std::cout << message << std::endl;
    }
}

void CompilationDriver::cleanup() {
    for (const auto& file : temporaryFiles_) {
        if (Utils::ProcessUtils::fileExists(file)) {
            logVerbose("Removing temporary file: " + file);
            Utils::ProcessUtils::deleteFile(file);
        }
    }
    temporaryFiles_.clear();
}

} // namespace Driver
} // namespace XXML
