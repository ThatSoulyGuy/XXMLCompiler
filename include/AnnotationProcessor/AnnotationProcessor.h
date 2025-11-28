#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>
#include "../Parser/AST.h"
#include "../Common/Error.h"

namespace XXML {

namespace Core { class CompilationContext; }
namespace Semantic { class SemanticAnalyzer; }

namespace AnnotationProcessor {

// Forward declaration
class ProcessorRegistry;

/**
 * Information about the target of an annotation (the element it's applied to)
 */
struct AnnotationTarget {
    enum class Kind {
        Class,
        Method,
        Property,
        Variable
    };

    Kind kind;
    std::string name;           // Name of the target element
    std::string typeName;       // Type name (for properties/variables)
    std::string className;      // Containing class (for methods/properties)
    std::string namespaceName;  // Containing namespace
    Common::SourceLocation location;
    Parser::ASTNode* astNode;   // Pointer to the AST node
};

/**
 * Context provided to annotation processors for reflection on the target
 */
class ReflectionContext {
public:
    ReflectionContext(const AnnotationTarget& target);

    // Target information
    AnnotationTarget::Kind getTargetKind() const { return target_.kind; }
    const std::string& getTargetName() const { return target_.name; }
    const std::string& getTypeName() const { return target_.typeName; }
    const std::string& getClassName() const { return target_.className; }
    const std::string& getNamespaceName() const { return target_.namespaceName; }
    const Common::SourceLocation& getLocation() const { return target_.location; }

    // String conversion for target kind
    std::string getTargetKindString() const;

private:
    AnnotationTarget target_;
};

/**
 * Context provided to annotation processors for compiler interaction
 */
class CompilationContext {
public:
    CompilationContext(Common::ErrorReporter& reporter);

    // Compiler feedback methods
    void message(const std::string& msg);
    void warning(const std::string& msg, const Common::SourceLocation& loc);
    void error(const std::string& msg, const Common::SourceLocation& loc);

    // Check if errors have been reported
    bool hasErrors() const;

private:
    Common::ErrorReporter& errorReporter_;
    std::vector<std::string> messages_;
};

/**
 * A pending annotation to be processed
 */
struct PendingAnnotation {
    std::string annotationName;
    Parser::AnnotationUsage* usage;
    AnnotationTarget target;
    std::vector<std::pair<std::string, Parser::Expression*>> arguments;
};

/**
 * Built-in processor function type
 */
using BuiltinProcessorFn = std::function<void(
    const PendingAnnotation& annotation,
    ReflectionContext& reflection,
    CompilationContext& compilation
)>;

/**
 * Annotation Processor - runs annotation processors after semantic analysis
 */
class AnnotationProcessor {
public:
    AnnotationProcessor(Common::ErrorReporter& reporter);

    /**
     * Register a built-in annotation processor
     */
    void registerBuiltinProcessor(const std::string& annotationName, BuiltinProcessorFn processor);

    /**
     * Set the processor registry for user-defined processors
     * @param registry Pointer to the processor registry (can be null)
     */
    void setProcessorRegistry(ProcessorRegistry* registry);

    /**
     * Add a pending annotation to be processed
     */
    void addPendingAnnotation(const PendingAnnotation& annotation);

    /**
     * Process all pending annotations
     * Should be called after semantic analysis, before code generation
     */
    void processAll();

    /**
     * Clear all pending annotations
     */
    void clear();

    /**
     * Get all pending annotations (for code generation of retained annotations)
     */
    const std::vector<PendingAnnotation>& getPendingAnnotations() const {
        return pendingAnnotations_;
    }

private:
    Common::ErrorReporter& errorReporter_;
    std::vector<PendingAnnotation> pendingAnnotations_;
    std::unordered_map<std::string, BuiltinProcessorFn> builtinProcessors_;
    ProcessorRegistry* processorRegistry_ = nullptr;

    // Built-in processor implementations
    static void processDeprecated(const PendingAnnotation& annotation,
                                  ReflectionContext& reflection,
                                  CompilationContext& compilation);

    // Execute a user-defined processor from DLL
    void executeUserProcessor(const PendingAnnotation& pending,
                              ReflectionContext& reflection,
                              CompilationContext& compilation);
};

} // namespace AnnotationProcessor
} // namespace XXML
