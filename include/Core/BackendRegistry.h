#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <vector>
#include <mutex>
#include <optional>
#include "IBackend.h"
#include "Concepts.h"

namespace XXML::Core {

/// Registry for code generation backends
/// Allows registration and selection of multiple backends (C++, LLVM, etc.)
class BackendRegistry {
public:
    BackendRegistry();
    ~BackendRegistry() = default;

    // Backend registration
    void registerBackend(std::string_view name, std::unique_ptr<IBackend> backend);
    void registerBackend(BackendTarget target, std::unique_ptr<IBackend> backend);

    // Template method for in-place construction
    template<typename BackendType, typename... Args>
        requires std::is_base_of_v<IBackend, BackendType>
    void emplaceBackend(std::string_view name, Args&&... args) {
        auto backend = std::make_unique<BackendType>(std::forward<Args>(args)...);
        registerBackend(name, std::move(backend));
    }

    template<typename BackendType, typename... Args>
        requires std::is_base_of_v<IBackend, BackendType>
    void emplaceBackend(BackendTarget target, Args&&... args) {
        auto backend = std::make_unique<BackendType>(std::forward<Args>(args)...);
        registerBackend(target, std::move(backend));
    }

    // Backend queries
    bool isRegistered(std::string_view name) const;
    bool isRegistered(BackendTarget target) const;

    IBackend* getBackend(std::string_view name);
    const IBackend* getBackend(std::string_view name) const;

    IBackend* getBackend(BackendTarget target);
    const IBackend* getBackend(BackendTarget target) const;

    // Default backend management
    void setDefaultBackend(std::string_view name);
    void setDefaultBackend(BackendTarget target);
    IBackend* getDefaultBackend();
    const IBackend* getDefaultBackend() const;

    // Backend enumeration
    std::vector<std::string> getAllBackendNames() const;
    std::vector<BackendTarget> getAllBackendTargets() const;

    // Backend capabilities
    std::vector<std::string> getBackendsWithCapability(BackendCapability capability) const;
    std::vector<std::string> getBackendsWithFeature(std::string_view feature) const;

    // Utility
    void clear();
    size_t size() const;

    // Built-in backend registration
    void registerBuiltinBackends();

    // Fluent API
    template<typename BackendType, typename... Args>
        requires std::is_base_of_v<IBackend, BackendType>
    BackendRegistry& with(std::string_view name, Args&&... args) {
        emplaceBackend<BackendType>(name, std::forward<Args>(args)...);
        return *this;
    }

private:
    // Storage: both by name and by target enum
    std::unordered_map<std::string, std::unique_ptr<IBackend>> backendsByName_;
    std::unordered_map<BackendTarget, IBackend*> backendsByTarget_;  // Non-owning pointers

    IBackend* defaultBackend_ = nullptr;
    mutable std::mutex mutex_;  // Thread-safety

    // Helper: convert target enum to name
    std::string targetToName(BackendTarget target) const;
};

// Helper function to get target name as string
inline std::string backendTargetToString(BackendTarget target) {
    switch (target) {
        case BackendTarget::Cpp20: return "cpp20";
        case BackendTarget::Cpp17: return "cpp17";
        case BackendTarget::Cpp14: return "cpp14";
        case BackendTarget::LLVM_IR: return "llvm";
        case BackendTarget::WebAssembly: return "wasm";
        case BackendTarget::JavaScript: return "js";
        case BackendTarget::Custom: return "custom";
        default: return "unknown";
    }
}

// Helper function to parse target from string
inline std::optional<BackendTarget> stringToBackendTarget(std::string_view str) {
    if (str == "cpp20" || str == "c++20") return BackendTarget::Cpp20;
    if (str == "cpp17" || str == "c++17") return BackendTarget::Cpp17;
    if (str == "cpp14" || str == "c++14") return BackendTarget::Cpp14;
    if (str == "llvm" || str == "llvm-ir") return BackendTarget::LLVM_IR;
    if (str == "wasm" || str == "webassembly") return BackendTarget::WebAssembly;
    if (str == "js" || str == "javascript") return BackendTarget::JavaScript;
    if (str == "custom") return BackendTarget::Custom;
    return std::nullopt;
}

} // namespace XXML::Core
