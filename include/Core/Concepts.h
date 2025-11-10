#pragma once

#include <concepts>
#include <string>
#include <string_view>
#include <memory>
#include <type_traits>

namespace XXML {
namespace Parser { class ASTNode; class Program; }  // Forward declare in parent namespace
}

namespace XXML::Core {

// Forward declarations
class TypeInfo;
class CompilationContext;

// ============================================================================
// AST Node Concepts
// ============================================================================

/// Concept for types that are AST nodes
template<typename T>
concept ASTNodeType = std::is_base_of_v<XXML::Parser::ASTNode, T> &&
                      requires(T t) {
    { t.accept(std::declval<class ASTVisitor&>()) } -> std::same_as<void>;
};

/// Concept for AST visitor types
template<typename T>
concept ASTVisitorType = requires(T visitor) {
    requires std::is_class_v<T>;  // Must be a class type
};

// ============================================================================
// Type System Concepts
// ============================================================================

/// Concept for type metadata providers
template<typename T>
concept TypeMetadata = requires(T t) {
    { t.name() } -> std::convertible_to<std::string>;
    { t.cppType() } -> std::convertible_to<std::string>;
    { t.isPrimitive() } -> std::same_as<bool>;
    { t.isBuiltin() } -> std::same_as<bool>;
};

/// Concept for type system implementations
template<typename T>
concept TypeSystem = requires(T system, const std::string& typeName) {
    { system.isRegistered(typeName) } -> std::same_as<bool>;
    { system.getTypeInfo(typeName) } -> std::convertible_to<const TypeInfo*>;
    { system.isCompatible(typeName, typeName) } -> std::same_as<bool>;
};

/// Concept for ownership semantic providers
template<typename T>
concept OwnershipProvider = requires(T provider, const std::string& typeName) {
    { provider.requiresSmartPointer(typeName) } -> std::same_as<bool>;
    { provider.isValueType(typeName) } -> std::same_as<bool>;
};

// ============================================================================
// Code Generation Concepts
// ============================================================================

/// Concept for code generation backends
template<typename T>
concept CodeGenBackend = requires(T backend) {
    { backend.targetName() } -> std::convertible_to<std::string>;
    { backend.generate(std::declval<XXML::Parser::Program&>()) } -> std::convertible_to<std::string>;
    { backend.supportsFeature(std::declval<std::string_view>()) } -> std::same_as<bool>;
};

/// Concept for backend initialization
template<typename T>
concept InitializableBackend = CodeGenBackend<T> && requires(T backend, CompilationContext& ctx) {
    { backend.initialize(ctx) } -> std::same_as<void>;
};

/// Concept for code generation strategies
template<typename T>
concept CodeGenStrategy = requires(T strategy, XXML::Parser::ASTNode& node) {
    { strategy.generate(node) } -> std::convertible_to<std::string>;
    { strategy.canHandle(node) } -> std::same_as<bool>;
};

// ============================================================================
// Operator Concepts
// ============================================================================

/// Concept for operator information
template<typename T>
concept OperatorInfo = requires(T op) {
    { op.symbol() } -> std::convertible_to<std::string>;
    { op.precedence() } -> std::convertible_to<int>;
};

/// Concept for operator generators
template<typename T>
concept OperatorGenerator = requires(T gen, const std::string& lhs, const std::string& rhs) {
    { gen(lhs, rhs) } -> std::convertible_to<std::string>;
};

// ============================================================================
// Registry Concepts
// ============================================================================

/// Concept for registrable items
template<typename T>
concept Registrable = requires(T item) {
    { item.name() } -> std::convertible_to<std::string>;
};

/// Concept for registry types
template<typename T, typename Item>
concept Registry = requires(T registry, const std::string& name, Item item) {
    { registry.registerItem(name, item) } -> std::same_as<void>;
    { registry.isRegistered(name) } -> std::same_as<bool>;
    { registry.getItem(name) } -> std::convertible_to<Item>;
};

// ============================================================================
// Context Concepts
// ============================================================================

/// Concept for compilation context providers
template<typename T>
concept ContextProvider = requires(T provider) {
    { provider.getContext() } -> std::convertible_to<CompilationContext&>;
};

// ============================================================================
// String Concepts (for better template constraints)
// ============================================================================

/// Concept for string-like types
template<typename T>
concept StringLike = std::convertible_to<T, std::string_view> ||
                     std::convertible_to<T, std::string> ||
                     std::same_as<T, const char*>;

/// Concept for formattable types (placeholder until std::format widely available)
template<typename T>
concept Formattable = requires(T t) {
    { std::to_string(t) } -> std::convertible_to<std::string>;
} || std::is_convertible_v<T, std::string>;

// ============================================================================
// Utility Concepts
// ============================================================================

/// Concept for callable types
template<typename F, typename... Args>
concept Callable = requires(F f, Args... args) {
    { f(args...) };
};

/// Concept for callable types with specific return type
template<typename F, typename Return, typename... Args>
concept CallableReturning = requires(F f, Args... args) {
    { f(args...) } -> std::convertible_to<Return>;
};

/// Concept for cloneable types
template<typename T>
concept Cloneable = requires(const T t) {
    { t.clone() } -> std::same_as<std::unique_ptr<T>>;
};

/// Concept for types that can be converted to debug strings
template<typename T>
concept Debuggable = requires(const T t) {
    { t.toDebugString() } -> std::convertible_to<std::string>;
};

} // namespace XXML::Core
