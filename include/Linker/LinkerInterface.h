#pragma once

#include <string>
#include <vector>
#include <memory>

namespace XXML {
namespace Linker {

/**
 * Configuration for linking
 */
struct LinkConfig {
    std::vector<std::string> objectFiles;      // .o or .obj files to link
    std::vector<std::string> libraryPaths;     // -L paths
    std::vector<std::string> libraries;        // -l names (without lib prefix or extension)
    std::vector<std::string> additionalFlags;  // Platform-specific flags
    std::string outputPath;                    // Output executable path
    bool verbose = false;                      // Print linker commands
    int optimizationLevel = 0;                 // 0-3
    bool stripSymbols = false;                 // Strip debug symbols from output

    // Platform-specific settings
    bool createConsoleApp = true;              // Windows: /SUBSYSTEM:CONSOLE vs WINDOWS
    bool staticLink = false;                   // Static vs dynamic linking
    bool createDLL = false;                    // Create shared library (DLL on Windows, .so on Unix)
};

/**
 * Result of a linking operation
 */
struct LinkResult {
    bool success;
    std::string outputPath;
    std::string output;      // stdout (renamed to avoid Windows macro collision)
    std::string error;       // stderr (renamed to avoid Windows macro collision)
    int exitCode;

    LinkResult() : success(false), exitCode(-1) {}
};

/**
 * Abstract interface for platform-specific linkers
 */
class ILinker {
public:
    virtual ~ILinker() = default;

    /**
     * Get linker name/description
     */
    virtual std::string name() const = 0;

    /**
     * Check if this linker is available on the system
     * @return true if linker tools are found in PATH
     */
    virtual bool isAvailable() const = 0;

    /**
     * Link object files into an executable
     * @param config Linking configuration
     * @return LinkResult with success status and output
     */
    virtual LinkResult link(const LinkConfig& config) = 0;

    /**
     * Get the expected object file extension for this linker
     * @return ".o" for Unix, ".obj" for Windows
     */
    virtual std::string objectFileExtension() const = 0;

    /**
     * Get the expected executable extension for this linker
     * @return ".exe" for Windows, "" for Unix
     */
    virtual std::string executableExtension() const = 0;
};

/**
 * Factory for creating platform-appropriate linker
 */
class LinkerFactory {
public:
    /**
     * Create a linker for the current platform
     * @param preferLLD If true, prefer LLVM's lld linker if available
     * @return Smart pointer to ILinker instance
     */
    static std::unique_ptr<ILinker> createLinker(bool preferLLD = false);

    /**
     * Create a specific linker by name
     * @param name "msvc", "gnu", "lld"
     * @return Smart pointer to ILinker instance, or nullptr if not available
     */
    static std::unique_ptr<ILinker> createLinkerByName(const std::string& name);
};

} // namespace Linker
} // namespace XXML
