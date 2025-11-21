#pragma once

#include <any>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "Concepts.h"
#include "ITypeSystem.h"
#include "IBackend.h"

namespace XXML {
namespace Common { class Error; }
namespace Semantic { class SymbolTable; }

namespace Core {

// Forward declarations
class TypeRegistry;
class OperatorRegistry;
class BackendRegistry;

/// Configuration for the compilation process
struct CompilerConfig {
    // Language features
    bool strictTypeChecking = true;
    bool allowImplicitConversions = false;
    bool enableTemplates = true;

    // Standard library
    std::string stdlibPath = "Language/";
    std::vector<std::string> stdlibModules = {
        "Core/String.XXML",
        "Core/Integer.XXML",
        "Core/Bool.XXML",
        "Core/Float.XXML",
        "Core/Double.XXML",
        "System/Console.XXML"
    };

    // Output options
    BackendTarget defaultBackend = BackendTarget::LLVM_IR;
    BackendConfig backendConfig;

    // Diagnostic options
    bool verboseErrors = true;
    bool warningsAsErrors = false;
    int maxErrors = 50;

    // Optimization
    bool enableOptimizations = false;
    int optimizationLevel = 0;

    // Debug
    bool generateDebugInfo = false;
    bool dumpAST = false;
    bool dumpSymbolTable = false;
};

/// Central compilation context that replaces all static state
/// This class owns all registries and provides thread-safe compilation
class CompilationContext {
public:
    CompilationContext();
    explicit CompilationContext(const CompilerConfig& config);
    ~CompilationContext();

    // Disable copying, allow moving
    CompilationContext(const CompilationContext&) = delete;
    CompilationContext& operator=(const CompilationContext&) = delete;
    CompilationContext(CompilationContext&&) noexcept;
    CompilationContext& operator=(CompilationContext&&) noexcept;

    // Configuration
    void setConfig(const CompilerConfig& config) { config_ = config; }
    const CompilerConfig& getConfig() const { return config_; }
    CompilerConfig& getConfig() { return config_; }

    // Registry access
    TypeRegistry& types();
    const TypeRegistry& types() const;

    OperatorRegistry& operators();
    const OperatorRegistry& operators() const;

    BackendRegistry& backends();
    const BackendRegistry& backends() const;

    // Symbol table access (per-compilation instance)
    Semantic::SymbolTable& symbolTable();
    const Semantic::SymbolTable& symbolTable() const;

    // Error tracking
    void reportError(const Common::Error& error);
    void reportWarning(const std::string& message);
    const std::vector<Common::Error>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    bool hasErrors() const { return !errors_.empty(); }
    void clearDiagnostics();

    // Backend management
    void setActiveBackend(BackendTarget target);
    void setActiveBackend(std::string_view backendName);
    IBackend* getActiveBackend();
    const IBackend* getActiveBackend() const;

    // Initialization
    void initializeBuiltins();  // Register all built-in types and operators
    void reset();               // Reset for new compilation

    // Statistics
    struct Stats {
        size_t typesRegistered = 0;
        size_t operatorsRegistered = 0;
        size_t backendsRegistered = 0;
        size_t errorsReported = 0;
        size_t warningsReported = 0;
    };
    Stats getStats() const;

    // Custom data storage (for extensions)
    template<typename T>
    void setCustomData(std::string_view key, T&& value) {
        customData_[std::string(key)] = std::make_any<std::decay_t<T>>(std::forward<T>(value));
    }

    template<typename T>
    T* getCustomData(std::string_view key) {
        auto it = customData_.find(std::string(key));
        if (it != customData_.end()) {
            return std::any_cast<T>(&it->second);
        }
        return nullptr;
    }

private:
    CompilerConfig config_;

    // Registry instances (using unique_ptr for forward declaration)
    std::unique_ptr<TypeRegistry> typeRegistry_;
    std::unique_ptr<OperatorRegistry> operatorRegistry_;
    std::unique_ptr<BackendRegistry> backendRegistry_;
    std::unique_ptr<Semantic::SymbolTable> symbolTable_;

    // Active backend
    IBackend* activeBackend_ = nullptr;

    // Diagnostics
    std::vector<Common::Error> errors_;
    std::vector<std::string> warnings_;

    // Custom extension data
    std::unordered_map<std::string, std::any> customData_;
};

} // namespace Core
} // namespace XXML
