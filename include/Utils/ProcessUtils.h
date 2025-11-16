#pragma once

#include <string>
#include <vector>

namespace XXML {
namespace Utils {

/**
 * Result of a process execution
 */
struct ProcessResult {
    int exitCode;
    std::string output;      // stdout (renamed to avoid Windows macro collision)
    std::string error;       // stderr (renamed to avoid Windows macro collision)
    bool success;

    ProcessResult() : exitCode(-1), success(false) {}
};

/**
 * Cross-platform utilities for executing system processes
 */
class ProcessUtils {
public:
    /**
     * Execute a command and capture its output
     * @param command The command to execute (e.g., "llc", "link.exe")
     * @param args Vector of arguments
     * @param workingDir Working directory (empty = current directory)
     * @return ProcessResult containing exit code, stdout, and stderr
     */
    static ProcessResult execute(
        const std::string& command,
        const std::vector<std::string>& args,
        const std::string& workingDir = ""
    );

    /**
     * Execute a command without capturing output (for user-visible output)
     * @param command The command to execute
     * @param args Vector of arguments
     * @param workingDir Working directory (empty = current directory)
     * @return Exit code
     */
    static int executeVisible(
        const std::string& command,
        const std::vector<std::string>& args,
        const std::string& workingDir = ""
    );

    /**
     * Find an executable in the system PATH
     * @param name Executable name (e.g., "clang", "gcc", "link.exe")
     * @return Full path to executable, or empty string if not found
     */
    static std::string findInPath(const std::string& name);

    /**
     * Check if a file exists
     * @param path File path
     * @return true if file exists
     */
    static bool fileExists(const std::string& path);

    /**
     * Delete a file
     * @param path File path
     * @return true if successfully deleted
     */
    static bool deleteFile(const std::string& path);

    /**
     * Get the directory containing the current executable
     * @return Directory path
     */
    static std::string getExecutableDirectory();

    /**
     * Join path components
     * @param parts Path components
     * @return Joined path with correct separators for platform
     */
    static std::string joinPath(const std::vector<std::string>& parts);

    /**
     * Get platform-specific path separator
     * @return '\\' on Windows, '/' on Unix
     */
    static char pathSeparator();

private:
    // Platform-specific implementations
#ifdef _WIN32
    static ProcessResult executeWindows(
        const std::string& command,
        const std::vector<std::string>& args,
        const std::string& workingDir,
        bool captureOutput
    );
#else
    static ProcessResult executeUnix(
        const std::string& command,
        const std::vector<std::string>& args,
        const std::string& workingDir,
        bool captureOutput
    );
#endif

    static std::string buildCommandLine(
        const std::string& command,
        const std::vector<std::string>& args
    );
};

} // namespace Utils
} // namespace XXML
