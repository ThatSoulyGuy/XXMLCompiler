#pragma once

#include <string>
#include <string_view>
#include <memory>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include "Concepts.h"

namespace XXML {
namespace Parser { class Program; class ASTNode; }
namespace Core {

class CompilationContext;
class ITypeSystem;

/// Backend target identifiers
enum class BackendTarget {
    LLVM_IR,        // LLVM intermediate representation (default)
    WebAssembly,    // WebAssembly (future)
    Custom          // User-defined backend
};

/// Backend capabilities flags
enum class BackendCapability {
    SmartPointers,      // Supports std::unique_ptr, std::shared_ptr
    Templates,          // Supports templates
    Concepts,           // Supports C++20 concepts
    Modules,            // Supports C++20 modules
    Coroutines,         // Supports coroutines
    Ranges,             // Supports ranges
    Exceptions,         // Supports exception handling
    RTTI,               // Supports runtime type information
    Threading,          // Supports multithreading
    CustomAllocators,   // Supports custom memory allocators
    GarbageCollection,  // Has garbage collection
    ValueSemantics,     // Supports value semantics
    Optimizations       // Supports backend-specific optimizations
};

/// Backend configuration options
struct BackendConfig {
    // Output formatting
    bool prettyPrint = true;
    int indentSize = 4;
    bool useSpaces = true;

    // Generation options
    bool generateComments = true;
    bool generateDebugInfo = false;
    bool enableOptimizations = false;
    int optimizationLevel = 0; // 0-3

    // Feature toggles
    bool useModernFeatures = true;
    bool preferStdAlgorithms = true;
    bool inlineSmallFunctions = false;

    // Custom options (backend-specific)
    std::unordered_map<std::string, std::string> customOptions;
};

/// Abstract interface for code generation backends
class IBackend {
public:
    virtual ~IBackend() = default;

    // Backend identity
    virtual std::string targetName() const = 0;
    virtual BackendTarget targetType() const = 0;
    virtual std::string version() const = 0;

    // Capabilities
    virtual bool supportsCapability(BackendCapability capability) const = 0;
    virtual std::vector<BackendCapability> getCapabilities() const = 0;
    virtual bool supportsFeature(std::string_view feature) const = 0;

    // Initialization
    virtual void initialize(CompilationContext& context) = 0;
    virtual void setConfig(const BackendConfig& config) = 0;
    virtual BackendConfig getConfig() const = 0;

    // Code generation - main entry points
    virtual std::string generate(Parser::Program& program) = 0;
    virtual std::string generateHeader(Parser::Program& program) = 0;  // For separate header generation
    virtual std::string generateImplementation(Parser::Program& program) = 0;

    // Preamble and includes
    virtual std::string generatePreamble() = 0;
    virtual std::vector<std::string> getRequiredIncludes() const = 0;
    virtual std::vector<std::string> getRequiredLibraries() const = 0;

    // Type system integration
    virtual std::string convertType(std::string_view xxmlType) const = 0;
    virtual std::string convertOwnership(std::string_view type,
                                        std::string_view ownershipIndicator) const = 0;

    // Optimization passes (optional)
    virtual void runOptimizationPasses(Parser::Program& program) {
        // Default: no optimization
    }

    // Error and diagnostic reporting
    virtual void reportError(std::string_view message) = 0;
    virtual void reportWarning(std::string_view message) = 0;

    // Utility
    virtual void reset() = 0;  // Reset backend state between compilations
};

/// Helper base class for implementing backends
class BackendBase : public IBackend {
protected:
    CompilationContext* context_ = nullptr;
    BackendConfig config_;
    std::string currentIndent_;
    int indentLevel_ = 0;
    std::vector<std::string> errors_;
    std::vector<std::string> warnings_;
    std::unordered_set<BackendCapability> capabilities_;

    // Helper methods for derived classes
    void indent() { indentLevel_++; updateIndentString(); }
    void dedent() { if (indentLevel_ > 0) indentLevel_--; updateIndentString(); }
    void updateIndentString();
    std::string getIndent() const { return currentIndent_; }

    void addCapability(BackendCapability cap) { capabilities_.insert(cap); }

    template<StringLike T>
    void emitError(const T& message) {
        errors_.push_back(std::string(message));
    }

    template<StringLike T>
    void emitWarning(const T& message) {
        warnings_.push_back(std::string(message));
    }

public:
    virtual ~BackendBase() = default;

    // Implement common IBackend methods
    void setConfig(const BackendConfig& config) override { config_ = config; }
    BackendConfig getConfig() const override { return config_; }

    bool supportsCapability(BackendCapability capability) const override {
        return capabilities_.contains(capability);
    }

    std::vector<BackendCapability> getCapabilities() const override {
        return std::vector<BackendCapability>(capabilities_.begin(), capabilities_.end());
    }

    void reportError(std::string_view message) override {
        errors_.push_back(std::string(message));
    }

    void reportWarning(std::string_view message) override {
        warnings_.push_back(std::string(message));
    }

    void reset() override {
        indentLevel_ = 0;
        updateIndentString();
        errors_.clear();
        warnings_.clear();
    }

    // Getters for errors/warnings
    const std::vector<std::string>& getErrors() const { return errors_; }
    const std::vector<std::string>& getWarnings() const { return warnings_; }
    bool hasErrors() const { return !errors_.empty(); }
};

} // namespace Core
} // namespace XXML
