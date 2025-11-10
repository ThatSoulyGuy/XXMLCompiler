#include "Core/CompilationContext.h"
#include "Core/TypeRegistry.h"
#include "Core/OperatorRegistry.h"
#include "Core/BackendRegistry.h"
#include "Semantic/SymbolTable.h"
#include "Common/Error.h"
#include "Backends/Cpp20Backend.h"
#include "Core/FormatCompat.h"  // Compatibility layer for std::format
#include <sstream>

namespace XXML::Core {

CompilationContext::CompilationContext()
    : CompilationContext(CompilerConfig{}) {
}

CompilationContext::CompilationContext(const CompilerConfig& config)
    : config_(config),
      typeRegistry_(std::make_unique<TypeRegistry>()),
      operatorRegistry_(std::make_unique<OperatorRegistry>()),
      backendRegistry_(std::make_unique<BackendRegistry>()),
      symbolTable_(std::make_unique<Semantic::SymbolTable>()) {

    // Initialize built-in types and operators
    initializeBuiltins();
}

CompilationContext::~CompilationContext() = default;

CompilationContext::CompilationContext(CompilationContext&&) noexcept = default;
CompilationContext& CompilationContext::operator=(CompilationContext&&) noexcept = default;

TypeRegistry& CompilationContext::types() {
    return *typeRegistry_;
}

const TypeRegistry& CompilationContext::types() const {
    return *typeRegistry_;
}

OperatorRegistry& CompilationContext::operators() {
    return *operatorRegistry_;
}

const OperatorRegistry& CompilationContext::operators() const {
    return *operatorRegistry_;
}

BackendRegistry& CompilationContext::backends() {
    return *backendRegistry_;
}

const BackendRegistry& CompilationContext::backends() const {
    return *backendRegistry_;
}

Semantic::SymbolTable& CompilationContext::symbolTable() {
    return *symbolTable_;
}

const Semantic::SymbolTable& CompilationContext::symbolTable() const {
    return *symbolTable_;
}

void CompilationContext::reportError(const Common::Error& error) {
    errors_.push_back(error);

    // Stop compilation if we exceed max errors
    if (config_.maxErrors > 0 && errors_.size() >= static_cast<size_t>(config_.maxErrors)) {
        // Add a fatal error to stop compilation
        errors_.push_back(Common::Error{
            Common::ErrorLevel::Fatal,
            Common::ErrorCode::InternalError,
            std::format("Too many errors ({}), stopping compilation", config_.maxErrors),
            Common::SourceLocation{}
        });
    }
}

void CompilationContext::reportWarning(const std::string& message) {
    warnings_.push_back(message);

    // If warnings are treated as errors, convert to error
    if (config_.warningsAsErrors) {
        errors_.push_back(Common::Error{
            Common::ErrorLevel::Error,
            Common::ErrorCode::InternalError,
            std::format("Warning treated as error: {}", message),
            Common::SourceLocation{}
        });
    }
}

void CompilationContext::clearDiagnostics() {
    errors_.clear();
    warnings_.clear();
}

void CompilationContext::setActiveBackend(BackendTarget target) {
    activeBackend_ = backendRegistry_->getBackend(target);
    if (activeBackend_) {
        activeBackend_->initialize(*this);
    }
}

void CompilationContext::setActiveBackend(std::string_view backendName) {
    activeBackend_ = backendRegistry_->getBackend(backendName);
    if (activeBackend_) {
        activeBackend_->initialize(*this);
    }
}

IBackend* CompilationContext::getActiveBackend() {
    if (!activeBackend_) {
        // Try to get default backend
        activeBackend_ = backendRegistry_->getDefaultBackend();
    }
    return activeBackend_;
}

const IBackend* CompilationContext::getActiveBackend() const {
    if (!activeBackend_) {
        // Try to get default backend
        return backendRegistry_->getDefaultBackend();
    }
    return activeBackend_;
}

void CompilationContext::initializeBuiltins() {
    // Register all built-in types
    typeRegistry_->registerBuiltinTypes();

    // Register all built-in operators
    operatorRegistry_->registerBuiltinOperators();

    // Register built-in backends
    backendRegistry_->emplaceBackend<Backends::Cpp20Backend>(
        BackendTarget::Cpp20, this);

    // Set default backend
    setActiveBackend(config_.defaultBackend);
}

void CompilationContext::reset() {
    // Clear diagnostics
    clearDiagnostics();

    // Reset symbol table
    symbolTable_ = std::make_unique<Semantic::SymbolTable>();

    // Reset backend
    if (activeBackend_) {
        activeBackend_->reset();
    }
}

CompilationContext::Stats CompilationContext::getStats() const {
    Stats stats;
    stats.typesRegistered = typeRegistry_->size();
    stats.operatorsRegistered = operatorRegistry_->getAllBinaryOperators().size() +
                               operatorRegistry_->getAllUnaryOperators().size();
    stats.backendsRegistered = backendRegistry_->size();
    stats.errorsReported = errors_.size();
    stats.warningsReported = warnings_.size();
    return stats;
}

} // namespace XXML::Core
