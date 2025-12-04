#pragma once
#include <stdexcept>
#include <string>
#include <sstream>
#include "../Common/SourceLocation.h"

namespace XXML {
namespace Semantic {

//==============================================================================
// BASE SEMANTIC ERROR
//==============================================================================

class SemanticError : public std::runtime_error {
public:
    SemanticError(const std::string& message)
        : std::runtime_error(message) {}

    SemanticError(const std::string& message, const Common::SourceLocation& loc)
        : std::runtime_error(formatMessage(message, loc)), location_(loc) {}

    const Common::SourceLocation& location() const { return location_; }

protected:
    static std::string formatMessage(const std::string& msg, const Common::SourceLocation& loc) {
        std::ostringstream oss;
        oss << loc.filename << ":" << loc.line << ":" << loc.column << ": " << msg;
        return oss.str();
    }

private:
    Common::SourceLocation location_;
};

//==============================================================================
// INVARIANT VIOLATIONS (Hard Failures)
//==============================================================================

// Base class for all invariant violations
// These indicate bugs in the compiler, not user errors
class InvariantViolation : public SemanticError {
public:
    InvariantViolation(const std::string& invariant, const std::string& details)
        : SemanticError(formatInvariant(invariant, details)),
          invariant_(invariant), details_(details) {}

    InvariantViolation(const std::string& invariant, const std::string& details,
                       const Common::SourceLocation& loc)
        : SemanticError(formatInvariant(invariant, details), loc),
          invariant_(invariant), details_(details) {}

    const std::string& invariant() const { return invariant_; }
    const std::string& details() const { return details_; }

protected:
    static std::string formatInvariant(const std::string& inv, const std::string& det) {
        return "INVARIANT VIOLATION [" + inv + "]: " + det;
    }

private:
    std::string invariant_;
    std::string details_;
};

// Thrown when an unknown/unresolved type reaches IR generation
class UnresolvedTypeError : public InvariantViolation {
public:
    UnresolvedTypeError(const std::string& typeName)
        : InvariantViolation("TYPE_RESOLUTION",
                             "Unknown type '" + typeName + "' reached IR generation. "
                             "All types must be resolved before codegen."),
          typeName_(typeName) {}

    UnresolvedTypeError(const std::string& typeName, const Common::SourceLocation& loc)
        : InvariantViolation("TYPE_RESOLUTION",
                             "Unknown type '" + typeName + "' reached IR generation. "
                             "All types must be resolved before codegen.", loc),
          typeName_(typeName) {}

    const std::string& typeName() const { return typeName_; }

private:
    std::string typeName_;
};

// Thrown when a forward reference was never resolved
class UnresolvedForwardRefError : public InvariantViolation {
public:
    UnresolvedForwardRefError(const std::string& typeName, const std::string& referencedFrom)
        : InvariantViolation("FORWARD_REFERENCE",
                             "Forward reference to '" + typeName + "' from '" + referencedFrom +
                             "' was never resolved."),
          typeName_(typeName), referencedFrom_(referencedFrom) {}

    const std::string& typeName() const { return typeName_; }
    const std::string& referencedFrom() const { return referencedFrom_; }

private:
    std::string typeName_;
    std::string referencedFrom_;
};

// Thrown when template instantiation is incomplete
class UnexpandedTemplateError : public InvariantViolation {
public:
    UnexpandedTemplateError(const std::string& templateName,
                            const std::vector<std::string>& typeArgs)
        : InvariantViolation("TEMPLATE_EXPANSION",
                             "Template '" + templateName + "' with arguments [" +
                             joinArgs(typeArgs) + "] was not fully instantiated."),
          templateName_(templateName), typeArgs_(typeArgs) {}

    const std::string& templateName() const { return templateName_; }
    const std::vector<std::string>& typeArgs() const { return typeArgs_; }

private:
    static std::string joinArgs(const std::vector<std::string>& args) {
        std::string result;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) result += ", ";
            result += args[i];
        }
        return result;
    }

    std::string templateName_;
    std::vector<std::string> typeArgs_;
};

// Thrown when class layout was not computed before codegen
class MissingLayoutError : public InvariantViolation {
public:
    MissingLayoutError(const std::string& className)
        : InvariantViolation("LAYOUT_COMPUTATION",
                             "Class '" + className + "' has no computed layout. "
                             "All class layouts must be computed before codegen."),
          className_(className) {}

    const std::string& className() const { return className_; }

private:
    std::string className_;
};

// Thrown when ownership annotation is missing
class MissingOwnershipError : public InvariantViolation {
public:
    MissingOwnershipError(const std::string& typeName, const std::string& context)
        : InvariantViolation("OWNERSHIP_ANNOTATION",
                             "Type '" + typeName + "' in " + context +
                             " is missing ownership annotation (^, &, or %)."),
          typeName_(typeName), context_(context) {}

    const std::string& typeName() const { return typeName_; }
    const std::string& context() const { return context_; }

private:
    std::string typeName_;
    std::string context_;
};

//==============================================================================
// CODEGEN INVARIANT VIOLATIONS
//==============================================================================

// Base class for codegen-specific invariant violations
class CodegenInvariantViolation : public InvariantViolation {
public:
    CodegenInvariantViolation(const std::string& invariant, const std::string& details)
        : InvariantViolation("CODEGEN:" + invariant, details) {}
};

// Thrown when trying to access a member on a missing class
class MissingClassError : public CodegenInvariantViolation {
public:
    MissingClassError(const std::string& className, const std::string& context)
        : CodegenInvariantViolation("MISSING_CLASS",
                                    "Class '" + className + "' not found in codegen context. " +
                                    "Context: " + context),
          className_(className) {}

    const std::string& className() const { return className_; }

private:
    std::string className_;
};

// Thrown when trying to access a missing property
class MissingPropertyError : public CodegenInvariantViolation {
public:
    MissingPropertyError(const std::string& className, const std::string& propertyName)
        : CodegenInvariantViolation("MISSING_PROPERTY",
                                    "Property '" + propertyName + "' not found in class '" +
                                    className + "'."),
          className_(className), propertyName_(propertyName) {}

    const std::string& className() const { return className_; }
    const std::string& propertyName() const { return propertyName_; }

private:
    std::string className_;
    std::string propertyName_;
};

// Thrown when trying to call an undeclared function
class UndeclaredFunctionError : public CodegenInvariantViolation {
public:
    UndeclaredFunctionError(const std::string& functionName)
        : CodegenInvariantViolation("UNDECLARED_FUNCTION",
                                    "Function '" + functionName + "' was not declared. "
                                    "All functions must be declared before IR generation."),
          functionName_(functionName) {}

    const std::string& functionName() const { return functionName_; }

private:
    std::string functionName_;
};

// Thrown when trying to access an unresolved identifier
class UnresolvedIdentifierError : public CodegenInvariantViolation {
public:
    UnresolvedIdentifierError(const std::string& identifier)
        : CodegenInvariantViolation("UNRESOLVED_IDENTIFIER",
                                    "Identifier '" + identifier + "' could not be resolved. "
                                    "All identifiers must be resolved before IR generation."),
          identifier_(identifier) {}

    const std::string& identifier() const { return identifier_; }

private:
    std::string identifier_;
};

// Thrown when IR type mismatch occurs
class IRTypeMismatchError : public CodegenInvariantViolation {
public:
    IRTypeMismatchError(const std::string& expected, const std::string& actual,
                        const std::string& context)
        : CodegenInvariantViolation("TYPE_MISMATCH",
                                    "Expected IR type '" + expected + "' but got '" + actual +
                                    "'. Context: " + context),
          expected_(expected), actual_(actual) {}

    const std::string& expected() const { return expected_; }
    const std::string& actual() const { return actual_; }

private:
    std::string expected_;
    std::string actual_;
};

// Thrown when control flow is incomplete (missing terminator)
class IncompleteControlFlowError : public CodegenInvariantViolation {
public:
    IncompleteControlFlowError(const std::string& blockName)
        : CodegenInvariantViolation("INCOMPLETE_CONTROL_FLOW",
                                    "Basic block '" + blockName +
                                    "' has no terminator instruction."),
          blockName_(blockName) {}

    const std::string& blockName() const { return blockName_; }

private:
    std::string blockName_;
};

//==============================================================================
// OWNERSHIP VIOLATIONS
//==============================================================================

// Thrown on use-after-move
class UseAfterMoveError : public InvariantViolation {
public:
    UseAfterMoveError(const std::string& variableName,
                      const Common::SourceLocation& useLoc,
                      const Common::SourceLocation& moveLoc)
        : InvariantViolation("USE_AFTER_MOVE",
                             "Variable '" + variableName + "' used after being moved at " +
                             formatLoc(moveLoc), useLoc),
          variableName_(variableName), moveLocation_(moveLoc) {}

    const std::string& variableName() const { return variableName_; }
    const Common::SourceLocation& moveLocation() const { return moveLocation_; }

private:
    static std::string formatLoc(const Common::SourceLocation& loc) {
        return loc.filename + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    }

    std::string variableName_;
    Common::SourceLocation moveLocation_;
};

// Thrown on double-move
class DoubleMoveError : public InvariantViolation {
public:
    DoubleMoveError(const std::string& variableName,
                    const Common::SourceLocation& secondMoveLoc,
                    const Common::SourceLocation& firstMoveLoc)
        : InvariantViolation("DOUBLE_MOVE",
                             "Variable '" + variableName + "' moved twice. First moved at " +
                             formatLoc(firstMoveLoc), secondMoveLoc),
          variableName_(variableName), firstMoveLocation_(firstMoveLoc) {}

    const std::string& variableName() const { return variableName_; }
    const Common::SourceLocation& firstMoveLocation() const { return firstMoveLocation_; }

private:
    static std::string formatLoc(const Common::SourceLocation& loc) {
        return loc.filename + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column);
    }

    std::string variableName_;
    Common::SourceLocation firstMoveLocation_;
};

// Thrown on dangling reference
class DanglingReferenceError : public InvariantViolation {
public:
    DanglingReferenceError(const std::string& refName, const std::string& targetName,
                           const Common::SourceLocation& loc)
        : InvariantViolation("DANGLING_REFERENCE",
                             "Reference '" + refName + "' to '" + targetName +
                             "' would outlive its target.", loc),
          refName_(refName), targetName_(targetName) {}

    const std::string& refName() const { return refName_; }
    const std::string& targetName() const { return targetName_; }

private:
    std::string refName_;
    std::string targetName_;
};

//==============================================================================
// CONSTRAINT VIOLATIONS
//==============================================================================

// Thrown when a template constraint is not satisfied
class ConstraintViolationError : public SemanticError {
public:
    ConstraintViolationError(const std::string& constraintName,
                             const std::string& typeName,
                             const std::string& reason,
                             const Common::SourceLocation& loc)
        : SemanticError("Constraint '" + constraintName + "' not satisfied by type '" +
                        typeName + "': " + reason, loc),
          constraintName_(constraintName), typeName_(typeName), reason_(reason) {}

    const std::string& constraintName() const { return constraintName_; }
    const std::string& typeName() const { return typeName_; }
    const std::string& reason() const { return reason_; }

private:
    std::string constraintName_;
    std::string typeName_;
    std::string reason_;
};

//==============================================================================
// FFI/ABI ERRORS
//==============================================================================

// Thrown when FFI signature is invalid
class InvalidFFISignatureError : public InvariantViolation {
public:
    InvalidFFISignatureError(const std::string& methodName, const std::string& reason)
        : InvariantViolation("FFI_SIGNATURE",
                             "Invalid FFI signature for method '" + methodName + "': " + reason),
          methodName_(methodName), reason_(reason) {}

    const std::string& methodName() const { return methodName_; }
    const std::string& reason() const { return reason_; }

private:
    std::string methodName_;
    std::string reason_;
};

// Thrown when calling convention is invalid for platform
class InvalidCallingConventionError : public InvariantViolation {
public:
    InvalidCallingConventionError(const std::string& convention, const std::string& platform)
        : InvariantViolation("CALLING_CONVENTION",
                             "Calling convention '" + convention +
                             "' is not valid on platform '" + platform + "'."),
          convention_(convention), platform_(platform) {}

    const std::string& convention() const { return convention_; }
    const std::string& platform() const { return platform_; }

private:
    std::string convention_;
    std::string platform_;
};

} // namespace Semantic
} // namespace XXML
