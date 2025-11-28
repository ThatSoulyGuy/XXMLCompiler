#pragma once

#include <string>
#include <memory>
#include <unordered_map>
#include <functional>
#include "../Common/Error.h"
#include "../../runtime/xxml_processor_api.h"

namespace XXML {
namespace AnnotationProcessor {

/**
 * ProcessorDLL - Manages loading and unloading of a processor DLL
 *
 * This class provides platform-agnostic dynamic library loading for
 * annotation processor DLLs.
 */
class ProcessorDLL {
public:
    /**
     * Function signature for processor entry point
     */
    using ProcessorFn = void(*)(
        ProcessorReflectionContext* reflection,
        ProcessorCompilationContext* compilation,
        ProcessorAnnotationArgs* args
    );

    /**
     * Load a processor DLL from the given path
     * @param path Path to the DLL/SO file
     */
    explicit ProcessorDLL(const std::string& path);

    /**
     * Unload the DLL
     */
    ~ProcessorDLL();

    // Non-copyable
    ProcessorDLL(const ProcessorDLL&) = delete;
    ProcessorDLL& operator=(const ProcessorDLL&) = delete;

    // Movable
    ProcessorDLL(ProcessorDLL&& other) noexcept;
    ProcessorDLL& operator=(ProcessorDLL&& other) noexcept;

    /**
     * Check if the DLL was loaded successfully
     */
    bool isLoaded() const { return handle_ != nullptr; }

    /**
     * Get the processor entry point function
     * @return The processor function, or nullptr if not found
     */
    ProcessorFn getProcessorFunction() const;

    /**
     * Get the path to the DLL
     */
    const std::string& getPath() const { return path_; }

    /**
     * Get error message if loading failed
     */
    const std::string& getError() const { return error_; }

private:
    void* handle_ = nullptr;
    std::string path_;
    std::string error_;
    mutable ProcessorFn cachedFn_ = nullptr;
};

/**
 * ProcessorRegistry - Manages loaded processor DLLs
 *
 * Maps annotation names to their processor DLLs and provides
 * lookup functionality.
 */
class ProcessorRegistry {
public:
    ProcessorRegistry() = default;
    ~ProcessorRegistry() = default;

    // Non-copyable
    ProcessorRegistry(const ProcessorRegistry&) = delete;
    ProcessorRegistry& operator=(const ProcessorRegistry&) = delete;

    /**
     * Load and register a processor DLL
     * @param dllPath Path to the processor DLL
     * @param errorReporter Error reporter for loading errors
     * @return true if loaded successfully
     */
    bool loadProcessor(const std::string& dllPath, Common::ErrorReporter& errorReporter);

    /**
     * Register a processor for a specific annotation name
     * @param annotationName The annotation this processor handles
     * @param dllPath Path to the processor DLL (must already be loaded)
     */
    void registerProcessor(const std::string& annotationName, const std::string& dllPath);

    /**
     * Get the processor DLL for an annotation
     * @param annotationName Name of the annotation
     * @return Pointer to the ProcessorDLL, or nullptr if not found
     */
    ProcessorDLL* getProcessor(const std::string& annotationName);

    /**
     * Check if a processor exists for the given annotation
     */
    bool hasProcessor(const std::string& annotationName) const;

    /**
     * Get all loaded processor paths
     */
    std::vector<std::string> getLoadedPaths() const;

    /**
     * Clear all loaded processors
     */
    void clear();

private:
    // Map from DLL path to loaded DLL
    std::unordered_map<std::string, std::unique_ptr<ProcessorDLL>> loadedDLLs_;

    // Map from annotation name to DLL path
    std::unordered_map<std::string, std::string> annotationToPath_;
};

/**
 * Helper function to extract annotation name from a processor DLL
 *
 * Processor DLLs may export a function that returns the annotation name:
 *   extern "C" const char* __xxml_processor_annotation_name();
 *
 * @param dll The loaded processor DLL
 * @return The annotation name, or empty string if not found
 */
std::string getAnnotationNameFromDLL(const ProcessorDLL& dll);

} // namespace AnnotationProcessor
} // namespace XXML
