#include "Utils/ProcessUtils.h"
#include <iostream>
#include <sstream>
#include <fstream>
#include <filesystem>

#include <cstdlib>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#endif

namespace XXML {
namespace Utils {

// Platform-specific path separator
char ProcessUtils::pathSeparator() {
#ifdef _WIN32
    return '\\';
#else
    return '/';
#endif
}

// Build command line string
std::string ProcessUtils::buildCommandLine(
    const std::string& command,
    const std::vector<std::string>& args
) {
    std::ostringstream cmdLine;

    // Quote command path if it contains spaces
    bool commandNeedsQuotes = command.find(' ') != std::string::npos ||
                             command.find('\t') != std::string::npos;

    if (commandNeedsQuotes) {
        cmdLine << "\"" << command << "\"";
    } else {
        cmdLine << command;
    }

    for (const auto& arg : args) {
        cmdLine << " ";

        // Quote arguments with spaces
        bool needsQuotes = arg.find(' ') != std::string::npos ||
                          arg.find('\t') != std::string::npos;

        if (needsQuotes) {
            cmdLine << "\"" << arg << "\"";
        } else {
            cmdLine << arg;
        }
    }

    return cmdLine.str();
}

// Check if file exists
bool ProcessUtils::fileExists(const std::string& path) {
    return std::filesystem::exists(path);
}

// Delete file
bool ProcessUtils::deleteFile(const std::string& path) {
    try {
        return std::filesystem::remove(path);
    } catch (...) {
        return false;
    }
}

// Join path components
std::string ProcessUtils::joinPath(const std::vector<std::string>& parts) {
    if (parts.empty()) {
        return "";
    }

    std::ostringstream result;
    result << parts[0];

    for (size_t i = 1; i < parts.size(); i++) {
        if (!parts[i].empty()) {
            result << pathSeparator() << parts[i];
        }
    }

    return result.str();
}

// Get executable directory
std::string ProcessUtils::getExecutableDirectory() {
#ifdef _WIN32
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    std::string exePath(buffer);
    size_t pos = exePath.find_last_of("\\/");
    return (pos != std::string::npos) ? exePath.substr(0, pos) : "";
#else
    char buffer[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len != -1) {
        buffer[len] = '\0';
        std::string exePath(buffer);
        size_t pos = exePath.find_last_of('/');
        return (pos != std::string::npos) ? exePath.substr(0, pos) : "";
    }
    return "";
#endif
}

// Find executable in PATH
std::string ProcessUtils::findInPath(const std::string& name) {
#ifdef _WIN32
    // On Windows, try with and without .exe extension
    std::vector<std::string> candidates = {name, name + ".exe"};

    // Get PATH environment variable (use std::getenv for cross-platform compatibility)
    const char* pathEnv = std::getenv("PATH");
    if (!pathEnv) {
        return "";
    }

    std::string pathStr(pathEnv);

    // Split PATH by semicolon
    std::vector<std::string> paths;
    std::istringstream pathStream(pathStr);
    std::string dir;
    while (std::getline(pathStream, dir, ';')) {
        if (!dir.empty()) {
            paths.push_back(dir);
        }
    }

    // Search each directory
    for (const auto& candidate : candidates) {
        for (const auto& dir : paths) {
            std::string fullPath = joinPath({dir, candidate});
            if (fileExists(fullPath)) {
                return fullPath;
            }
        }
    }

    return "";
#else
    // On Unix, use 'which' command
    std::string cmd = "which " + name + " 2>/dev/null";
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) {
        return "";
    }

    char buffer[256];
    std::string result;
    if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
        result = buffer;
        // Remove trailing newline
        if (!result.empty() && result.back() == '\n') {
            result.pop_back();
        }
    }

    pclose(pipe);
    return result;
#endif
}

#ifdef _WIN32
// Windows implementation
ProcessResult ProcessUtils::executeWindows(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::string& workingDir,
    bool captureOutput
) {
    ProcessResult result;

    // Build command line
    std::string cmdLine = buildCommandLine(command, args);

    // Create pipes for stdout/stderr if capturing
    HANDLE hStdoutRead = NULL, hStdoutWrite = NULL;
    HANDLE hStderrRead = NULL, hStderrWrite = NULL;

    if (captureOutput) {
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = NULL;

        if (!CreatePipe(&hStdoutRead, &hStdoutWrite, &sa, 0) ||
            !CreatePipe(&hStderrRead, &hStderrWrite, &sa, 0)) {
            result.error = "Failed to create pipes";
            return result;
        }

        // Ensure read handles are not inherited
        SetHandleInformation(hStdoutRead, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hStderrRead, HANDLE_FLAG_INHERIT, 0);
    }

    // Setup process
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    if (captureOutput) {
        si.dwFlags |= STARTF_USESTDHANDLES;
        si.hStdOutput = hStdoutWrite;
        si.hStdError = hStderrWrite;
    }

    // Convert to non-const for CreateProcessA
    char* cmdLineBuf = _strdup(cmdLine.c_str());
    const char* workDirPtr = workingDir.empty() ? NULL : workingDir.c_str();

    // Use the full command path as lpApplicationName to avoid MSYS2/Git Bash
    // path resolution issues where /usr/bin/link might be found instead of link.exe
    // Only use appName if command contains a path separator (full path), otherwise
    // let CreateProcessA search PATH by passing NULL
    const char* appName = NULL;
    if (!command.empty() && (command.find('/') != std::string::npos ||
                             command.find('\\') != std::string::npos ||
                             command.find(':') != std::string::npos)) {
        // Command contains a path - use it as app name to bypass PATH resolution
        appName = command.c_str();
    }

    // Create process
    BOOL success = CreateProcessA(
        appName,        // Application name (full path to avoid PATH issues)
        cmdLineBuf,     // Command line
        NULL,           // Process security attributes
        NULL,           // Thread security attributes
        TRUE,           // Inherit handles
        0,              // Creation flags
        NULL,           // Environment
        workDirPtr,     // Working directory
        &si,            // Startup info
        &pi             // Process information
    );

    free(cmdLineBuf);

    if (!success) {
        if (captureOutput) {
            CloseHandle(hStdoutRead);
            CloseHandle(hStdoutWrite);
            CloseHandle(hStderrRead);
            CloseHandle(hStderrWrite);
        }
        result.error = "Failed to create process: " + std::to_string(GetLastError());
        return result;
    }

    // Close write ends of pipes (child has them)
    if (captureOutput) {
        CloseHandle(hStdoutWrite);
        CloseHandle(hStderrWrite);
    }

    // Wait for process to complete
    WaitForSingleObject(pi.hProcess, INFINITE);

    // Get exit code
    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    result.exitCode = exitCode;
    result.success = (exitCode == 0);

    // Read output
    if (captureOutput) {
        char buffer[4096];
        DWORD bytesRead;

        // Read stdout
        while (ReadFile(hStdoutRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result.output += buffer;
        }

        // Read stderr
        while (ReadFile(hStderrRead, buffer, sizeof(buffer) - 1, &bytesRead, NULL) && bytesRead > 0) {
            buffer[bytesRead] = '\0';
            result.error += buffer;
        }

        CloseHandle(hStdoutRead);
        CloseHandle(hStderrRead);
    }

    // Cleanup
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return result;
}

ProcessResult ProcessUtils::execute(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::string& workingDir
) {
    return executeWindows(command, args, workingDir, true);
}

int ProcessUtils::executeVisible(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::string& workingDir
) {
    ProcessResult result = executeWindows(command, args, workingDir, false);
    return result.exitCode;
}

#else
// Unix implementation
ProcessResult ProcessUtils::executeUnix(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::string& workingDir,
    bool captureOutput
) {
    ProcessResult result;

    // Create pipes for stdout/stderr if capturing
    int stdoutPipe[2], stderrPipe[2];

    if (captureOutput) {
        if (pipe(stdoutPipe) == -1 || pipe(stderrPipe) == -1) {
            result.error = "Failed to create pipes";
            return result;
        }
    }

    // Fork process
    pid_t pid = fork();

    if (pid == -1) {
        result.error = "Failed to fork process";
        if (captureOutput) {
            close(stdoutPipe[0]);
            close(stdoutPipe[1]);
            close(stderrPipe[0]);
            close(stderrPipe[1]);
        }
        return result;
    }

    if (pid == 0) {
        // Child process

        // Change working directory if specified
        if (!workingDir.empty()) {
            if (chdir(workingDir.c_str()) != 0) {
                _exit(127);
            }
        }

        // Redirect stdout/stderr if capturing
        if (captureOutput) {
            dup2(stdoutPipe[1], STDOUT_FILENO);
            dup2(stderrPipe[1], STDERR_FILENO);

            close(stdoutPipe[0]);
            close(stdoutPipe[1]);
            close(stderrPipe[0]);
            close(stderrPipe[1]);
        }

        // Build argument array
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(command.c_str()));

        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);

        // Execute - use execv for absolute paths, execvp for names that need PATH search
        // Check if command is an absolute path (Windows: drive letter, Unix: starts with /)
        bool isWindowsPath = (command.length() > 2 && command[1] == ':');  // Windows: C:\...
        bool isUnixPath = (!command.empty() && command[0] == '/');         // Unix: /...

        // Convert Windows path to Unix format for MSYS2/Git Bash compatibility
        std::string execCommand = command;
        if (isWindowsPath) {
            // Convert D:\path\to\file.exe to /d/path/to/file.exe
            execCommand = "/" + std::string(1, std::tolower(command[0]));  // /d
            for (size_t i = 2; i < command.length(); ++i) {
                if (command[i] == '\\') {
                    execCommand += '/';
                } else {
                    execCommand += command[i];
                }
            }
            // Update argv[0] with converted path
            argv[0] = const_cast<char*>(execCommand.c_str());
            execv(execCommand.c_str(), argv.data());
        } else if (isUnixPath) {
            execv(command.c_str(), argv.data());
        } else {
            execvp(command.c_str(), argv.data());
        }

        // If we get here, exec failed
        _exit(127);
    }

    // Parent process
    if (captureOutput) {
        close(stdoutPipe[1]);
        close(stderrPipe[1]);

        // Read stdout
        char buffer[4096];
        ssize_t bytesRead;

        while ((bytesRead = read(stdoutPipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytesRead] = '\0';
            result.output += buffer;
        }

        // Read stderr
        while ((bytesRead = read(stderrPipe[0], buffer, sizeof(buffer) - 1)) > 0) {
            buffer[bytesRead] = '\0';
            result.error += buffer;
        }

        close(stdoutPipe[0]);
        close(stderrPipe[0]);
    }

    // Wait for child
    int status;
    waitpid(pid, &status, 0);

    if (WIFEXITED(status)) {
        result.exitCode = WEXITSTATUS(status);
        result.success = (result.exitCode == 0);
    } else {
        result.exitCode = -1;
        result.success = false;
    }

    return result;
}

ProcessResult ProcessUtils::execute(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::string& workingDir
) {
    return executeUnix(command, args, workingDir, true);
}

int ProcessUtils::executeVisible(
    const std::string& command,
    const std::vector<std::string>& args,
    const std::string& workingDir
) {
    ProcessResult result = executeUnix(command, args, workingDir, false);
    return result.exitCode;
}

#endif

} // namespace Utils
} // namespace XXML
