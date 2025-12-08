#pragma once

/**
 * @file XXML.h
 * @brief Public API for XXML Compiler Extensibility
 *
 * This header provides the complete public API for extending the XXML compiler.
 * Users can register custom types, operators, backends, and more.
 *
 * @version 3.0.0
 * @date 2025
 *
 * ## Features:
 * - ✅ Runtime type registration
 * - ✅ Custom operator definitions
 * - ✅ Multi-backend code generation
 * - ✅ Thread-safe compilation
 * - ✅ C++20 concepts for type safety
 *
 * ## Example Usage:
 *
 * @code{.cpp}
 * #include <XXML.h>
 *
 * int main() {
 *     // Create compilation context
 *     XXML::Core::CompilationContext context;
 *
 *     // Register custom type
 *     context.types().registerType({
 *         .xxmlName = "Vector3",
 *         .cppType = "glm::vec3",
 *         .category = XXML::Core::TypeCategory::Class,
 *         .ownership = XXML::Core::OwnershipSemantics::Value
 *     });
 *
 *     // Register custom operator
 *     context.operators().registerBinaryOperator(
 *         "dot",  // Custom operator
 *         XXML::Core::OperatorPrecedence::Multiplicative,
 *         XXML::Core::Associativity::Left
 *     );
 *
 *     // Select backend
 *     context.setActiveBackend(XXML::Core::BackendTarget::LLVM_IR);
 *
 *     // Compile
 *     // ... your compilation code here
 * }
 * @endcode
 */

// Core API
#include "Core/CompilationContext.h"
#include "Core/TypeRegistry.h"
#include "Core/OperatorRegistry.h"
#include "Core/BackendRegistry.h"
#include "Core/ITypeSystem.h"
#include "Core/IBackend.h"
#include "Core/Concepts.h"

// Backends
#include "Backends/LLVMBackend.h"

// Parser and AST
#include "Parser/AST.h"
#include "Parser/Parser.h"

// Semantic Analysis
#include "Semantic/SemanticAnalyzer.h"
#include "Semantic/SymbolTable.h"

// Lexer
#include "Lexer/Lexer.h"
#include "Lexer/Token.h"

// Common utilities
#include "Common/Error.h"
#include "Common/SourceLocation.h"

// Import system
#include "Import/Module.h"
#include "Import/ImportResolver.h"
#include "Import/DependencyGraph.h"

/**
 * @namespace XXML
 * @brief Root namespace for the XXML compiler
 */
namespace XXML {

/**
 * @namespace XXML::Core
 * @brief Core compiler infrastructure (registries, contexts, interfaces)
 */
namespace Core {}

/**
 * @namespace XXML::Backends
 * @brief Code generation backends (LLVM IR)
 */
namespace Backends {}

/**
 * @namespace XXML::Parser
 * @brief Parsing and AST construction
 */
namespace Parser {}

/**
 * @namespace XXML::Semantic
 * @brief Semantic analysis and type checking
 */
namespace Semantic {}

/**
 * @namespace XXML::Lexer
 * @brief Lexical analysis and tokenization
 */
namespace Lexer {}

/**
 * @namespace XXML::Common
 * @brief Common utilities and error handling
 */
namespace Common {}

/**
 * @namespace XXML::Import
 * @brief Multi-file compilation and module system
 */
namespace Import {}

} // namespace XXML

/**
 * @page extension_guide Extension Guide
 *
 * # Extending the XXML Compiler
 *
 * ## Table of Contents
 * 1. [Custom Types](#custom-types)
 * 2. [Custom Operators](#custom-operators)
 * 3. [Custom Backends](#custom-backends)
 * 4. [Type System Extensions](#type-system-extensions)
 *
 * ## Custom Types {#custom-types}
 *
 * Register a new type with the TypeRegistry:
 *
 * ```cpp
 * XXML::Core::TypeInfo complexType;
 * complexType.xxmlName = "Complex";
 * complexType.cppType = "std::complex<double>";
 * complexType.llvmType = "{ double, double }";
 * complexType.category = XXML::Core::TypeCategory::Class;
 * complexType.ownership = XXML::Core::OwnershipSemantics::Value;
 * complexType.isBuiltin = false;
 *
 * context.types().registerType(complexType);
 * ```
 *
 * ## Custom Operators {#custom-operators}
 *
 * Define custom operators with precedence and code generation:
 *
 * ```cpp
 * context.operators().registerBinaryOperatorWithGenerator(
 *     "|>",  // Pipe operator
 *     XXML::Core::OperatorPrecedence::Additive,
 *     XXML::Core::Associativity::Left,
 *     [](std::string_view lhs, std::string_view rhs) {
 *         return std::format("{}({})", rhs, lhs);
 *     }
 * );
 * ```
 *
 * ## Custom Backends {#custom-backends}
 *
 * Implement IBackend interface for new output targets:
 *
 * ```cpp
 * class MyCustomBackend : public XXML::Core::BackendBase {
 * public:
 *     std::string targetName() const override { return "MyTarget"; }
 *
 *     std::string generate(XXML::Parser::Program& program) override {
 *         // Your code generation logic
 *         return generatedCode;
 *     }
 *
 *     // Implement other required methods...
 * };
 *
 * // Register it
 * context.backends().emplaceBackend<MyCustomBackend>("mytarget");
 * ```
 *
 * ## Type System Extensions {#type-system-extensions}
 *
 * Add custom type compatibility rules:
 *
 * ```cpp
 * class CustomTypeSystem : public XXML::Core::TypeRegistry {
 * public:
 *     bool isCompatible(std::string_view from, std::string_view to) const override {
 *         // Custom compatibility logic
 *         if (from == "MyType" && to == "OtherType") {
 *             return true;  // Allow conversion
 *         }
 *         return TypeRegistry::isCompatible(from, to);
 *     }
 * };
 * ```
 *
 * ## Thread Safety
 *
 * All registries are thread-safe using std::mutex. You can create multiple
 * CompilationContext instances for parallel compilation:
 *
 * ```cpp
 * std::vector<std::thread> threads;
 *
 * for (auto& file : files) {
 *     threads.emplace_back([file]() {
 *         XXML::Core::CompilationContext context;  // Independent instance
 *         // Compile file...
 *     });
 * }
 *
 * for (auto& thread : threads) {
 *     thread.join();
 * }
 * ```
 *
 * ## Best Practices
 *
 * 1. **Always use CompilationContext**: Never create registries directly
 * 2. **Register types early**: Before parsing, during context initialization
 * 3. **Use concepts**: Leverage C++20 concepts for type-safe extensions
 * 4. **Test backends**: Verify output for all registered backends
 * 5. **Document operators**: Clearly document custom operator semantics
 *
 * ## Performance Tips
 *
 * - Reuse CompilationContext for multiple files with same configuration
 * - Use `context.reset()` to clear state between compilations
 * - Enable optimizations with `config.enableOptimizations = true`
 * - Profile type lookups if registering 1000+ types
 *
 * ## Error Handling
 *
 * The compiler uses a centralized error reporting system:
 *
 * ```cpp
 * if (context.hasErrors()) {
 *     for (const auto& error : context.getErrors()) {
 *         std::cerr << error.toString() << "\n";
 *     }
 *     return 1;
 * }
 * ```
 */
