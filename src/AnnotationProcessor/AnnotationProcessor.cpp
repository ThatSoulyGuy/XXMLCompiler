#include "../../include/AnnotationProcessor/AnnotationProcessor.h"
#include "../../include/AnnotationProcessor/ProcessorLoader.h"
#include <iostream>
#include <cstring>

namespace XXML {
namespace AnnotationProcessor {

// ============================================================================
// ReflectionContext Implementation
// ============================================================================

ReflectionContext::ReflectionContext(const AnnotationTarget& target)
    : target_(target) {}

std::string ReflectionContext::getTargetKindString() const {
    switch (target_.kind) {
        case AnnotationTarget::Kind::Class: return "class";
        case AnnotationTarget::Kind::Method: return "method";
        case AnnotationTarget::Kind::Property: return "property";
        case AnnotationTarget::Kind::Variable: return "variable";
        default: return "unknown";
    }
}

// ============================================================================
// CompilationContext Implementation
// ============================================================================

CompilationContext::CompilationContext(Common::ErrorReporter& reporter)
    : errorReporter_(reporter) {}

void CompilationContext::message(const std::string& msg) {
    messages_.push_back(msg);
    std::cout << "[Annotation Processor] " << msg << std::endl;
}

void CompilationContext::warning(const std::string& msg, const Common::SourceLocation& loc) {
    errorReporter_.reportWarning(Common::ErrorCode::ConstraintViolation, msg, loc);
}

void CompilationContext::error(const std::string& msg, const Common::SourceLocation& loc) {
    errorReporter_.reportError(Common::ErrorCode::ConstraintViolation, msg, loc);
}

bool CompilationContext::hasErrors() const {
    return errorReporter_.hasErrors();
}

// ============================================================================
// AnnotationProcessor Implementation
// ============================================================================

AnnotationProcessor::AnnotationProcessor(Common::ErrorReporter& reporter)
    : errorReporter_(reporter) {
    // Register built-in processors
    registerBuiltinProcessor("Deprecated", processDeprecated);
}

void AnnotationProcessor::registerBuiltinProcessor(const std::string& annotationName,
                                                    BuiltinProcessorFn processor) {
    builtinProcessors_[annotationName] = processor;
}

void AnnotationProcessor::setProcessorRegistry(ProcessorRegistry* registry) {
    processorRegistry_ = registry;
}

void AnnotationProcessor::addPendingAnnotation(const PendingAnnotation& annotation) {
    pendingAnnotations_.push_back(annotation);
}

void AnnotationProcessor::processAll() {
    if (pendingAnnotations_.empty()) {
        return;  // Nothing to process
    }

    CompilationContext compilationCtx(errorReporter_);

    for (const auto& pending : pendingAnnotations_) {
        ReflectionContext reflectionCtx(pending.target);

        // Check if there's a built-in processor for this annotation
        auto it = builtinProcessors_.find(pending.annotationName);
        if (it != builtinProcessors_.end()) {
            it->second(pending, reflectionCtx, compilationCtx);
            continue;
        }

        // Try user-defined processor from loaded DLLs
        if (processorRegistry_) {
            executeUserProcessor(pending, reflectionCtx, compilationCtx);
        }
    }
}

void AnnotationProcessor::executeUserProcessor(const PendingAnnotation& pending,
                                                ReflectionContext& reflection,
                                                CompilationContext& compilation) {
    if (!processorRegistry_) {
        return;
    }

    ProcessorDLL* dll = processorRegistry_->getProcessor(pending.annotationName);
    if (!dll || !dll->isLoaded()) {
        // No user-defined processor for this annotation - silently skip
        return;
    }

    auto processFn = dll->getProcessorFunction();
    if (!processFn) {
        return;
    }

    // Create C-compatible reflection context
    ::ProcessorReflectionContext cReflection;
    cReflection.targetKind = reflection.getTargetKindString().c_str();
    cReflection.targetName = reflection.getTargetName().c_str();
    cReflection.typeName = reflection.getTypeName().c_str();
    cReflection.className = reflection.getClassName().c_str();
    cReflection.namespaceName = reflection.getNamespaceName().c_str();
    cReflection.sourceFile = reflection.getLocation().filename.c_str();
    cReflection.lineNumber = reflection.getLocation().line;
    cReflection.columnNumber = reflection.getLocation().column;

    // Build extended reflection data from AST
    // Storage vectors must outlive the processor call
    std::vector<std::string> propNames, propTypes, propOwnerships;
    std::vector<std::string> methodNames, methodReturnTypes;
    std::vector<std::string> paramNames, paramTypes;
    std::vector<const char*> propNamePtrs, propTypePtrs, propOwnershipPtrs;
    std::vector<const char*> methodNamePtrs, methodReturnTypePtrs;
    std::vector<const char*> paramNamePtrs, paramTypePtrs;
    std::string baseClassName, returnTypeName, ownership;
    int isFinal = 0, isStatic = 0, hasDefault = 0;
    int targetValueType = 0;  // 0=none, 1=int, 2=string, 3=bool, 4=double
    int64_t targetIntValue = 0;
    std::string targetStringValue;
    int targetBoolValue = 0;
    double targetDoubleValue = 0.0;
    void* targetValuePtr = nullptr;

    // Populate based on target kind
    if (pending.target.astNode) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(pending.target.astNode)) {
            // Gather class properties and methods
            isFinal = classDecl->isFinal ? 1 : 0;
            if (!classDecl->baseClass.empty()) {
                baseClassName = classDecl->baseClass;
            }

            for (const auto& section : classDecl->sections) {
                if (!section) continue;
                for (const auto& decl : section->declarations) {
                    if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(decl.get())) {
                        propNames.push_back(prop->name);
                        propTypes.push_back(prop->type ? prop->type->typeName : "");
                        std::string own = "";
                        if (prop->type) {
                            switch (prop->type->ownership) {
                                case Parser::OwnershipType::Owned: own = "^"; break;
                                case Parser::OwnershipType::Reference: own = "&"; break;
                                case Parser::OwnershipType::Copy: own = "%"; break;
                                default: break;
                            }
                        }
                        propOwnerships.push_back(own);
                    } else if (auto* method = dynamic_cast<Parser::MethodDecl*>(decl.get())) {
                        methodNames.push_back(method->name);
                        methodReturnTypes.push_back(method->returnType ? method->returnType->typeName : "None");
                    }
                }
            }

            // Build pointer arrays
            for (const auto& s : propNames) propNamePtrs.push_back(s.c_str());
            for (const auto& s : propTypes) propTypePtrs.push_back(s.c_str());
            for (const auto& s : propOwnerships) propOwnershipPtrs.push_back(s.c_str());
            for (const auto& s : methodNames) methodNamePtrs.push_back(s.c_str());
            for (const auto& s : methodReturnTypes) methodReturnTypePtrs.push_back(s.c_str());
        }
        else if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(pending.target.astNode)) {
            // Gather method parameters
            // Note: isStatic is not currently tracked in MethodDecl
            returnTypeName = methodDecl->returnType ? methodDecl->returnType->typeName : "None";

            for (const auto& param : methodDecl->parameters) {
                paramNames.push_back(param->name);
                paramTypes.push_back(param->type ? param->type->typeName : "");
            }

            // Build pointer arrays
            for (const auto& s : paramNames) paramNamePtrs.push_back(s.c_str());
            for (const auto& s : paramTypes) paramTypePtrs.push_back(s.c_str());
        }
        else if (auto* propDecl = dynamic_cast<Parser::PropertyDecl*>(pending.target.astNode)) {
            // Property info
            // Note: hasDefault is not currently tracked in PropertyDecl
            if (propDecl->type) {
                switch (propDecl->type->ownership) {
                    case Parser::OwnershipType::Owned: ownership = "^"; break;
                    case Parser::OwnershipType::Reference: ownership = "&"; break;
                    case Parser::OwnershipType::Copy: ownership = "%"; break;
                    default: break;
                }
            }
        }
        else if (auto* instStmt = dynamic_cast<Parser::InstantiateStmt*>(pending.target.astNode)) {
            // Variable instantiation - extract constant initializer value if available
            // Store primitive values; the Processor_getTargetValue runtime function will
            // wrap them in proper XXML objects
            if (instStmt->initializer) {
                if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(instStmt->initializer.get())) {
                    targetValueType = 1;  // int
                    targetIntValue = intLit->value;
                    targetValuePtr = &targetIntValue;
                }
                else if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(instStmt->initializer.get())) {
                    targetValueType = 2;  // string
                    targetStringValue = strLit->value;
                    targetValuePtr = const_cast<char*>(targetStringValue.c_str());
                }
                else if (auto* boolLit = dynamic_cast<Parser::BoolLiteralExpr*>(instStmt->initializer.get())) {
                    targetValueType = 3;  // bool
                    targetBoolValue = boolLit->value ? 1 : 0;
                    targetValuePtr = &targetBoolValue;
                }
                else if (auto* doubleLit = dynamic_cast<Parser::DoubleLiteralExpr*>(instStmt->initializer.get())) {
                    targetValueType = 4;  // double
                    targetDoubleValue = doubleLit->value;
                    targetValuePtr = &targetDoubleValue;
                }
                else if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(instStmt->initializer.get())) {
                    // Handle Type::Constructor(value) patterns
                    if (!callExpr->arguments.empty()) {
                        if (auto* innerInt = dynamic_cast<Parser::IntegerLiteralExpr*>(callExpr->arguments[0].get())) {
                            targetValueType = 1;
                            targetIntValue = innerInt->value;
                            targetValuePtr = &targetIntValue;
                        }
                        else if (auto* innerStr = dynamic_cast<Parser::StringLiteralExpr*>(callExpr->arguments[0].get())) {
                            targetValueType = 2;
                            targetStringValue = innerStr->value;
                            targetValuePtr = const_cast<char*>(targetStringValue.c_str());
                        }
                        else if (auto* innerBool = dynamic_cast<Parser::BoolLiteralExpr*>(callExpr->arguments[0].get())) {
                            targetValueType = 3;
                            targetBoolValue = innerBool->value ? 1 : 0;
                            targetValuePtr = &targetBoolValue;
                        }
                        else if (auto* innerDouble = dynamic_cast<Parser::DoubleLiteralExpr*>(callExpr->arguments[0].get())) {
                            targetValueType = 4;
                            targetDoubleValue = innerDouble->value;
                            targetValuePtr = &targetDoubleValue;
                        }
                    }
                }
            }
        }
    }

    // Create extended reflection structure (matches ProcessorExtendedReflection in runtime)
    struct {
        int propertyCount;
        const char** propertyNames;
        const char** propertyTypes;
        const char** propertyOwnerships;
        int methodCount;
        const char** methodNames;
        const char** methodReturnTypes;
        const char* baseClassName;
        int isFinal;
        int parameterCount;
        const char** parameterNames;
        const char** parameterTypes;
        const char* returnTypeName;
        int isStatic;
        int hasDefault;
        const char* ownership;
        int targetValueType;
        void* targetValue;
        ::ProcessorAnnotationArgs* annotationArgs;
    } extReflection = {
        static_cast<int>(propNames.size()),
        propNamePtrs.empty() ? nullptr : propNamePtrs.data(),
        propTypePtrs.empty() ? nullptr : propTypePtrs.data(),
        propOwnershipPtrs.empty() ? nullptr : propOwnershipPtrs.data(),
        static_cast<int>(methodNames.size()),
        methodNamePtrs.empty() ? nullptr : methodNamePtrs.data(),
        methodReturnTypePtrs.empty() ? nullptr : methodReturnTypePtrs.data(),
        baseClassName.empty() ? nullptr : baseClassName.c_str(),
        isFinal,
        static_cast<int>(paramNames.size()),
        paramNamePtrs.empty() ? nullptr : paramNamePtrs.data(),
        paramTypePtrs.empty() ? nullptr : paramTypePtrs.data(),
        returnTypeName.empty() ? nullptr : returnTypeName.c_str(),
        isStatic,
        hasDefault,
        ownership.empty() ? nullptr : ownership.c_str(),
        targetValueType,
        targetValuePtr,
        nullptr  // annotationArgs - set after cArgs is built
    };

    cReflection._internal = &extReflection;

    // Create C-compatible compilation context with error flag
    // Structure matches ProcessorCompilerState in xxml_processor_api.c
    struct {
        int* errorFlag;
        const char* currentFile;
        int currentLine;
        int currentCol;
    } compilerState;

    int processorErrorFlag = 0;
    compilerState.errorFlag = &processorErrorFlag;
    compilerState.currentFile = reflection.getLocation().filename.c_str();
    compilerState.currentLine = reflection.getLocation().line;
    compilerState.currentCol = reflection.getLocation().column;

    ::ProcessorCompilationContext cCompilation;
    cCompilation._internal = &compilerState;

    // Create C-compatible annotation arguments
    std::vector<::ProcessorAnnotationArg> cArgArray;
    cArgArray.reserve(pending.arguments.size());

    for (const auto& [name, expr] : pending.arguments) {
        ::ProcessorAnnotationArg arg;
        arg.name = name.c_str();

        // Extract value from expression
        if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) {
            arg.type = PROCESSOR_ARG_INT;
            arg.value.intValue = intLit->value;
        } else if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
            arg.type = PROCESSOR_ARG_STRING;
            arg.value.stringValue = strLit->value.c_str();
        } else if (auto* boolLit = dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
            arg.type = PROCESSOR_ARG_BOOL;
            arg.value.boolValue = boolLit->value ? 1 : 0;
        } else if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
            // Handle String::Constructor("...") pattern
            if (!callExpr->arguments.empty()) {
                if (auto* innerStr = dynamic_cast<Parser::StringLiteralExpr*>(
                        callExpr->arguments[0].get())) {
                    arg.type = PROCESSOR_ARG_STRING;
                    arg.value.stringValue = innerStr->value.c_str();
                } else if (auto* innerInt = dynamic_cast<Parser::IntegerLiteralExpr*>(
                        callExpr->arguments[0].get())) {
                    arg.type = PROCESSOR_ARG_INT;
                    arg.value.intValue = innerInt->value;
                } else {
                    arg.type = PROCESSOR_ARG_STRING;
                    arg.value.stringValue = "";
                }
            } else {
                arg.type = PROCESSOR_ARG_STRING;
                arg.value.stringValue = "";
            }
        } else {
            // Default to empty string for unknown types
            arg.type = PROCESSOR_ARG_STRING;
            arg.value.stringValue = "";
        }

        cArgArray.push_back(arg);
    }

    ::ProcessorAnnotationArgs cArgs;
    cArgs.count = static_cast<int>(cArgArray.size());
    cArgs.args = cArgArray.empty() ? nullptr : cArgArray.data();

    // Set annotation args in the extended reflection struct
    extReflection.annotationArgs = &cArgs;

    // Call the processor
    processFn(&cReflection, &cCompilation, &cArgs);

    // Check if processor reported an error and propagate to error reporter
    if (processorErrorFlag) {
        compilation.error("Annotation processor error for @" + pending.annotationName,
                         reflection.getLocation());
    }
}

void AnnotationProcessor::clear() {
    pendingAnnotations_.clear();
}

// ============================================================================
// Built-in Processor Implementations
// ============================================================================

void AnnotationProcessor::processDeprecated(const PendingAnnotation& annotation,
                                            ReflectionContext& reflection,
                                            CompilationContext& compilation) {
    // Extract the reason argument if provided
    std::string reason = "This element is deprecated";

    for (const auto& arg : annotation.arguments) {
        if (arg.first == "reason") {
            // Try to extract string literal value
            if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(arg.second)) {
                // String::Constructor("...")
                if (!callExpr->arguments.empty()) {
                    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(
                            callExpr->arguments[0].get())) {
                        reason = strLit->value;
                    }
                }
            } else if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(arg.second)) {
                reason = strLit->value;
            }
        }
    }

    // Generate a deprecation warning
    std::string msg = reflection.getTargetKindString() + " '" +
                      reflection.getTargetName() + "' is deprecated: " + reason;

    compilation.warning(msg, reflection.getLocation());
}

} // namespace AnnotationProcessor
} // namespace XXML
