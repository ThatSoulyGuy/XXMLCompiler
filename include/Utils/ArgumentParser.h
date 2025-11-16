#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <optional>

namespace XXML {
namespace Utils {

/**
 * Simple command-line argument parser
 */
class ArgumentParser {
public:
    ArgumentParser(int argc, char* argv[]) {
        programName_ = argv[0];

        for (int i = 1; i < argc; i++) {
            std::string arg = argv[i];

            if (arg[0] == '-') {
                // Flag or option
                if (arg == "-o" && i + 1 < argc) {
                    // Output file
                    outputFile_ = argv[++i];
                }
                else if (arg == "-c") {
                    // Compile only (no linking)
                    compileOnly_ = true;
                }
                else if (arg == "--emit-llvm" || arg == "-S") {
                    // Emit LLVM IR
                    emitLLVM_ = true;
                }
                else if (arg == "-v" || arg == "--verbose") {
                    // Verbose output
                    verbose_ = true;
                }
                else if (arg == "-O0") {
                    optimizationLevel_ = 0;
                }
                else if (arg == "-O1" || arg == "-O") {
                    optimizationLevel_ = 1;
                }
                else if (arg == "-O2") {
                    optimizationLevel_ = 2;
                }
                else if (arg == "-O3") {
                    optimizationLevel_ = 3;
                }
                else if (arg == "-L" && i + 1 < argc) {
                    // Library path
                    libraryPaths_.push_back(argv[++i]);
                }
                else if (arg == "-l" && i + 1 < argc) {
                    // Library name
                    libraries_.push_back(argv[++i]);
                }
                else if (arg == "--keep-temps") {
                    // Keep temporary files
                    keepTemporaryFiles_ = true;
                }
                else if (arg == "--backend=cpp" || arg == "--cpp") {
                    // Use C++ backend
                    useCppBackend_ = true;
                }
                else if (arg == "--backend=llvm" || arg == "--llvm") {
                    // Use LLVM backend (default)
                    useCppBackend_ = false;
                }
                else if (arg == "--runtime" && i + 1 < argc) {
                    // Runtime library path
                    runtimeLibraryPath_ = argv[++i];
                }
                else if (arg == "--help" || arg == "-h") {
                    showHelp_ = true;
                }
                else {
                    unknownFlags_.push_back(arg);
                }
            }
            else {
                // Positional argument (input file or object file)
                if (arg.find(".o") != std::string::npos ||
                    arg.find(".obj") != std::string::npos) {
                    objectFiles_.push_back(arg);
                }
                else {
                    // Assume it's the input file
                    if (inputFile_.empty()) {
                        inputFile_ = arg;
                    }
                    else {
                        // Multiple input files not supported yet
                        unknownArgs_.push_back(arg);
                    }
                }
            }
        }
    }

    std::string programName() const { return programName_; }
    std::string inputFile() const { return inputFile_; }
    std::string outputFile() const { return outputFile_; }

    bool compileOnly() const { return compileOnly_; }
    bool emitLLVM() const { return emitLLVM_; }
    bool verbose() const { return verbose_; }
    bool keepTemporaryFiles() const { return keepTemporaryFiles_; }
    bool useCppBackend() const { return useCppBackend_; }
    bool showHelp() const { return showHelp_; }

    int optimizationLevel() const { return optimizationLevel_; }

    const std::vector<std::string>& libraryPaths() const { return libraryPaths_; }
    const std::vector<std::string>& libraries() const { return libraries_; }
    const std::vector<std::string>& objectFiles() const { return objectFiles_; }
    std::string runtimeLibraryPath() const { return runtimeLibraryPath_; }

    const std::vector<std::string>& unknownFlags() const { return unknownFlags_; }
    const std::vector<std::string>& unknownArgs() const { return unknownArgs_; }

    bool hasErrors() const {
        return !unknownFlags_.empty() || !unknownArgs_.empty() ||
               (inputFile_.empty() && !showHelp_);
    }

    void printHelp() const {
        std::cout << "XXML Compiler v2.0 - Complete Toolchain\n";
        std::cout << "========================================\n\n";
        std::cout << "Usage: " << programName_ << " [options] <input.xxml>\n\n";
        std::cout << "Options:\n";
        std::cout << "  -o <file>        Specify output file\n";
        std::cout << "  -c               Compile only (generate .o/.obj file)\n";
        std::cout << "  -S, --emit-llvm  Emit LLVM IR (.ll file)\n";
        std::cout << "  -O0, -O1, -O2, -O3  Optimization level (default: -O0)\n";
        std::cout << "  -v, --verbose    Verbose output\n";
        std::cout << "  -L <path>        Add library search path\n";
        std::cout << "  -l <name>        Link with library\n";
        std::cout << "  --keep-temps     Keep temporary files (.ll, .o)\n";
        std::cout << "  --backend=cpp    Use C++ backend instead of LLVM\n";
        std::cout << "  --backend=llvm   Use LLVM backend (default)\n";
        std::cout << "  --runtime <path> Specify runtime library path\n";
        std::cout << "  -h, --help       Show this help message\n\n";
        std::cout << "Examples:\n";
        std::cout << "  " << programName_ << " hello.xxml              # Compile to executable\n";
        std::cout << "  " << programName_ << " hello.xxml -o hello    # Specify output name\n";
        std::cout << "  " << programName_ << " hello.xxml -c          # Compile to object file\n";
        std::cout << "  " << programName_ << " hello.xxml -S          # Generate LLVM IR\n";
        std::cout << "  " << programName_ << " hello.xxml -O2 -v      # Optimized build with verbose output\n";
    }

    void printErrors() const {
        if (!unknownFlags_.empty()) {
            std::cerr << "Error: Unknown flags:";
            for (const auto& flag : unknownFlags_) {
                std::cerr << " " << flag;
            }
            std::cerr << "\n";
        }

        if (!unknownArgs_.empty()) {
            std::cerr << "Error: Unknown arguments:";
            for (const auto& arg : unknownArgs_) {
                std::cerr << " " << arg;
            }
            std::cerr << "\n";
        }

        if (inputFile_.empty() && !showHelp_) {
            std::cerr << "Error: No input file specified\n";
        }

        std::cerr << "Use --help for usage information\n";
    }

private:
    std::string programName_;
    std::string inputFile_;
    std::string outputFile_;
    std::vector<std::string> libraryPaths_;
    std::vector<std::string> libraries_;
    std::vector<std::string> objectFiles_;
    std::string runtimeLibraryPath_;

    bool compileOnly_ = false;
    bool emitLLVM_ = false;
    bool verbose_ = false;
    bool keepTemporaryFiles_ = false;
    bool useCppBackend_ = false;
    bool showHelp_ = false;

    int optimizationLevel_ = 0;

    std::vector<std::string> unknownFlags_;
    std::vector<std::string> unknownArgs_;
};

} // namespace Utils
} // namespace XXML
