#include "../../include/AnnotationProcessor/ProcessorLoader.h"
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace XXML {
namespace AnnotationProcessor {

// ============================================================================
// ProcessorDLL Implementation
// ============================================================================

ProcessorDLL::ProcessorDLL(const std::string& path) : path_(path) {
#ifdef _WIN32
    handle_ = LoadLibraryA(path.c_str());
    if (!handle_) {
        DWORD err = GetLastError();
        char* msgBuf = nullptr;
        FormatMessageA(
            FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
            NULL,
            err,
            MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            (LPSTR)&msgBuf,
            0,
            NULL
        );
        if (msgBuf) {
            error_ = "Failed to load DLL: " + std::string(msgBuf);
            LocalFree(msgBuf);
        } else {
            error_ = "Failed to load DLL: error code " + std::to_string(err);
        }
    }
#else
    handle_ = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        const char* err = dlerror();
        error_ = "Failed to load shared library: " + std::string(err ? err : "unknown error");
    }
#endif
}

ProcessorDLL::~ProcessorDLL() {
    if (handle_) {
#ifdef _WIN32
        FreeLibrary((HMODULE)handle_);
#else
        dlclose(handle_);
#endif
        handle_ = nullptr;
    }
}

ProcessorDLL::ProcessorDLL(ProcessorDLL&& other) noexcept
    : handle_(other.handle_)
    , path_(std::move(other.path_))
    , error_(std::move(other.error_))
    , cachedFn_(other.cachedFn_)
{
    other.handle_ = nullptr;
    other.cachedFn_ = nullptr;
}

ProcessorDLL& ProcessorDLL::operator=(ProcessorDLL&& other) noexcept {
    if (this != &other) {
        // Clean up existing handle
        if (handle_) {
#ifdef _WIN32
            FreeLibrary((HMODULE)handle_);
#else
            dlclose(handle_);
#endif
        }

        handle_ = other.handle_;
        path_ = std::move(other.path_);
        error_ = std::move(other.error_);
        cachedFn_ = other.cachedFn_;

        other.handle_ = nullptr;
        other.cachedFn_ = nullptr;
    }
    return *this;
}

ProcessorDLL::ProcessorFn ProcessorDLL::getProcessorFunction() const {
    if (cachedFn_) {
        return cachedFn_;
    }

    if (!handle_) {
        return nullptr;
    }

#ifdef _WIN32
    cachedFn_ = (ProcessorFn)GetProcAddress((HMODULE)handle_, "__xxml_processor_process");
#else
    cachedFn_ = (ProcessorFn)dlsym(handle_, "__xxml_processor_process");
#endif

    return cachedFn_;
}

// ============================================================================
// ProcessorRegistry Implementation
// ============================================================================

bool ProcessorRegistry::loadProcessor(const std::string& dllPath, Common::ErrorReporter& errorReporter) {
    // Check if already loaded
    if (loadedDLLs_.find(dllPath) != loadedDLLs_.end()) {
        return true; // Already loaded
    }

    auto dll = std::make_unique<ProcessorDLL>(dllPath);

    if (!dll->isLoaded()) {
        errorReporter.reportError(
            Common::ErrorCode::IOError,
            dll->getError(),
            Common::SourceLocation()
        );
        return false;
    }

    // Verify the DLL exports the processor function
    if (!dll->getProcessorFunction()) {
        errorReporter.reportError(
            Common::ErrorCode::InternalError,
            "Processor DLL does not export __xxml_processor_process: " + dllPath,
            Common::SourceLocation()
        );
        return false;
    }

    // Try to get the annotation name from the DLL
    std::string annotationName = getAnnotationNameFromDLL(*dll);

    // Store the DLL
    loadedDLLs_[dllPath] = std::move(dll);

    // If we got an annotation name, register the mapping
    if (!annotationName.empty()) {
        annotationToPath_[annotationName] = dllPath;
        std::cout << "  Registered processor for annotation: @" << annotationName << "\n";
    } else {
        std::cout << "  Warning: Could not get annotation name from DLL\n";
    }

    return true;
}

void ProcessorRegistry::registerProcessor(const std::string& annotationName, const std::string& dllPath) {
    // Verify the DLL is loaded
    if (loadedDLLs_.find(dllPath) == loadedDLLs_.end()) {
        return; // DLL not loaded
    }

    annotationToPath_[annotationName] = dllPath;
}

ProcessorDLL* ProcessorRegistry::getProcessor(const std::string& annotationName) {
    auto it = annotationToPath_.find(annotationName);
    if (it == annotationToPath_.end()) {
        return nullptr;
    }

    auto dllIt = loadedDLLs_.find(it->second);
    if (dllIt == loadedDLLs_.end()) {
        return nullptr;
    }

    return dllIt->second.get();
}

bool ProcessorRegistry::hasProcessor(const std::string& annotationName) const {
    return annotationToPath_.find(annotationName) != annotationToPath_.end();
}

std::vector<std::string> ProcessorRegistry::getLoadedPaths() const {
    std::vector<std::string> paths;
    paths.reserve(loadedDLLs_.size());
    for (const auto& pair : loadedDLLs_) {
        paths.push_back(pair.first);
    }
    return paths;
}

void ProcessorRegistry::clear() {
    annotationToPath_.clear();
    loadedDLLs_.clear();
}

// ============================================================================
// Helper Functions
// ============================================================================

std::string getAnnotationNameFromDLL(const ProcessorDLL& dll) {
    if (!dll.isLoaded()) {
        return "";
    }

    // Try to get the annotation name export
    using NameFn = const char* (*)();
    NameFn nameFn = nullptr;

#ifdef _WIN32
    // Get handle - we need to access the internal handle
    // This is a bit of a hack, but necessary for this helper
    HMODULE handle = NULL;
    GetModuleHandleExA(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        (LPCSTR)dll.getProcessorFunction(),
        &handle
    );
    if (handle) {
        nameFn = (NameFn)GetProcAddress(handle, "__xxml_processor_annotation_name");
    }
#else
    // On Unix, we need the handle which we don't have direct access to
    // For now, return empty and let the registry use explicit registration
#endif

    if (nameFn) {
        const char* name = nameFn();
        if (name) {
            return std::string(name);
        }
    }

    return "";
}

} // namespace AnnotationProcessor
} // namespace XXML
