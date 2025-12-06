#include "../../include/Semantic/SemanticVerifier.h"
#include "../../include/Semantic/SemanticAnalyzer.h"  // For UNKNOWN_TYPE, DEFERRED_TYPE
#include "../../include/Semantic/SemanticError.h"
#include "../../include/Semantic/ControlFlowAnalyzer.h"
#include <iostream>
#include <sstream>
#include <functional>

namespace XXML {
namespace Semantic {

//==============================================================================
// MAIN ENTRY POINTS
//==============================================================================

SemanticVerifier::VerificationResult SemanticVerifier::verify(
    SemanticAnalyzer& analyzer,
    Parser::Program& program,
    Mode mode) {

    VerificationResult result;

    // Type Resolution Invariants
    verifyNoUnknownTypes(analyzer, result, mode);
    verifyAllClassesResolved(analyzer, result);
    verifyAllTemplatesResolved(analyzer, result);
    verifyNoUnexpandedGenerics(analyzer, result);
    verifyOwnershipAnnotations(analyzer, program, result);

    // Control Flow Invariants
    verifyAllMethodsReturn(program, analyzer, result);
    verifyBreakContinueValid(program, result);

    return result;
}

SemanticVerifier::VerificationResult SemanticVerifier::verifyWithPassResults(
    const TypeResolutionResult& typeResult,
    const TemplateExpansionResult& templateResult,
    const SemanticValidationResult& semanticResult,
    const OwnershipAnalysisResult& ownershipResult,
    const LayoutComputationResult& layoutResult,
    const ABILoweringResult& abiResult,
    Mode mode) {

    VerificationResult result;

    // Check pass success flags first
    if (!typeResult.success) {
        result.addError("TYPE", "Type resolution pass failed");
    }
    if (!templateResult.success) {
        result.addError("TEMPLATE", "Template expansion pass failed");
    }
    if (!semanticResult.success) {
        result.addError("TYPE", "Semantic validation pass failed");
    }
    if (!ownershipResult.success) {
        result.addError("OWNERSHIP", "Ownership analysis pass failed");
    }
    if (!layoutResult.success) {
        result.addError("LAYOUT", "Layout computation pass failed");
    }
    if (!abiResult.success) {
        result.addError("ABI", "ABI lowering pass failed");
    }

    // Type Resolution Invariants
    verifyNoForwardReferences(typeResult, result);
    verifyNoCircularTypes(typeResult, result);

    // Template Invariants
    verifyConstraintsSatisfied(templateResult, result);

    // Ownership Invariants
    verifyNoUseAfterMove(ownershipResult, result);
    verifyNoDoubleMove(ownershipResult, result);
    verifyReferencesValid(ownershipResult, result);
    verifyCapturesValid(ownershipResult, result);

    // Layout Invariants
    verifyReflectionMatches(layoutResult, result);
    verifyAlignmentValid(layoutResult, result);
    verifyFieldOffsets(layoutResult, result);

    // ABI Invariants
    verifyFFISignaturesComplete(abiResult, result);
    verifyCallingConventionsValid(abiResult, result);
    verifyMarshalingStrategies(abiResult, result);

    return result;
}

void SemanticVerifier::assertAllInvariantsOrThrow(
    SemanticAnalyzer& analyzer,
    Parser::Program& program) {

    VerificationResult result = verify(analyzer, program, Mode::Strict);

    if (!result.success) {
        std::stringstream ss;
        ss << "Semantic verification failed with " << result.errors.size() << " errors:\n";
        for (const auto& error : result.errors) {
            ss << "  - " << error << "\n";
        }
        throw CodegenInvariantViolation("VERIFICATION_FAILED", ss.str());
    }
}

void SemanticVerifier::assertPreCodegenInvariants(
    const CompilationPassResults& passResults,
    Parser::Program& program,
    SemanticAnalyzer& analyzer) {

    VerificationResult result;

    // =========================================================================
    // 1. TYPE RESOLUTION MUST BE FINAL
    // =========================================================================
    verifyNoUnknownTypes(analyzer, result, Mode::Strict);
    verifyNoForwardReferences(passResults.typeResolution, result);
    verifyNoCircularTypes(passResults.typeResolution, result);

    // =========================================================================
    // 2. TEMPLATE INSTANTIATION MUST BE COMPLETE
    // =========================================================================
    verifyAllClassesResolved(analyzer, result);
    verifyAllTemplatesResolved(analyzer, result);
    verifyConstraintsSatisfied(passResults.templateExpansion, result);
    verifyNoUnexpandedGenerics(analyzer, result);

    // =========================================================================
    // 3. CLASS LAYOUT MUST BE FULLY DETERMINED
    // =========================================================================
    verifyAllLayoutsComputed(passResults.layoutComputation, analyzer, result);
    verifyReflectionMatches(passResults.layoutComputation, result);
    verifyAlignmentValid(passResults.layoutComputation, result);
    verifyFieldOffsets(passResults.layoutComputation, result);

    // =========================================================================
    // 4. OWNERSHIP AND LIFETIME MUST BE VALIDATED
    // =========================================================================
    verifyNoUseAfterMove(passResults.ownershipAnalysis, result);
    verifyNoDoubleMove(passResults.ownershipAnalysis, result);
    verifyReferencesValid(passResults.ownershipAnalysis, result);
    verifyCapturesValid(passResults.ownershipAnalysis, result);
    verifyOwnershipAnnotations(analyzer, program, result);

    // =========================================================================
    // 5. FFI CALLS MUST HAVE EXACT SIGNATURES
    // =========================================================================
    verifyFFISignaturesComplete(passResults.abiLowering, result);
    verifyCallingConventionsValid(passResults.abiLowering, result);
    verifyMarshalingStrategies(passResults.abiLowering, result);

    // =========================================================================
    // 6. CONTROL FLOW MUST BE COMPLETE
    // =========================================================================
    verifyAllMethodsReturn(program, analyzer, result);
    verifyBreakContinueValid(program, result);

    // =========================================================================
    // FATAL: If any invariant failed, abort compilation
    // =========================================================================
    if (!result.success) {
        std::cerr << "\n=== PRE-CODEGEN INVARIANT VIOLATIONS ===\n";
        std::cerr << result.getDetailedSummary();
        std::cerr << "\nErrors:\n";
        for (const auto& err : result.errors) {
            std::cerr << "  " << err << "\n";
        }
        if (!result.warnings.empty()) {
            std::cerr << "\nWarnings:\n";
            for (const auto& warn : result.warnings) {
                std::cerr << "  " << warn << "\n";
            }
        }
        std::cerr << "=========================================\n";

        throw std::runtime_error("Pre-codegen invariants not satisfied: " +
                                 std::to_string(result.errors.size()) + " errors");
    }
}

//==============================================================================
// TYPE RESOLUTION INVARIANTS
//==============================================================================

bool SemanticVerifier::verifyNoUnknownTypes(
    SemanticAnalyzer& analyzer,
    VerificationResult& result,
    Mode mode) {

    const auto& expressionTypes = analyzer.getExpressionTypes();
    int unknownCount = 0;
    int deferredCount = 0;
    std::vector<std::string> unknownLocations;

    // Helper to get expression kind name
    auto getExprKind = [](Parser::Expression* expr) -> std::string {
        if (dynamic_cast<Parser::CallExpr*>(expr)) return "CallExpr";
        if (dynamic_cast<Parser::MemberAccessExpr*>(expr)) return "MemberAccessExpr";
        if (dynamic_cast<Parser::IdentifierExpr*>(expr)) return "IdentifierExpr";
        if (dynamic_cast<Parser::BinaryExpr*>(expr)) return "BinaryExpr";
        if (dynamic_cast<Parser::LambdaExpr*>(expr)) return "LambdaExpr";
        if (dynamic_cast<Parser::StringLiteralExpr*>(expr)) return "StringLiteralExpr";
        if (dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) return "IntegerLiteralExpr";
        if (dynamic_cast<Parser::ReferenceExpr*>(expr)) return "ReferenceExpr";
        if (dynamic_cast<Parser::ThisExpr*>(expr)) return "ThisExpr";
        return "UnknownExprKind";
    };

    for (const auto& [expr, type] : expressionTypes) {
        // "Deferred" types are template-dependent and allowed (resolved at instantiation)
        if (type == DEFERRED_TYPE) {
            deferredCount++;
            continue;
        }

        // "Unknown" types are ALWAYS fatal errors - they indicate type resolution failure
        if (type == UNKNOWN_TYPE) {
            unknownCount++;

            if (unknownLocations.size() < 10) {
                std::string info = getExprKind(const_cast<Parser::Expression*>(expr));
                if (!expr->location.filename.empty()) {
                    info += " in " + expr->location.filename;
                }
                if (expr->location.line > 0) {
                    info += " at line " + std::to_string(expr->location.line);
                }
                unknownLocations.push_back(info + " has Unknown type");
            }
        }
    }

    // All Unknown types are now errors - no distinction between critical/non-critical
    if (unknownCount > 0) {
        for (const auto& loc : unknownLocations) {
            result.addError("TYPE", loc);
        }
        if (unknownCount > 10) {
            result.addError("TYPE", "... and " + std::to_string(unknownCount - 10) +
                           " more Unknown types");
        }
        return false;
    }

    // Deferred types are informational only (template parameters awaiting instantiation)
    if (deferredCount > 0 && mode == Mode::Strict) {
        result.addWarning(std::to_string(deferredCount) +
                         " expressions have Deferred type (template-dependent, will resolve at instantiation)");
    }

    return true;
}

bool SemanticVerifier::verifyNoForwardReferences(
    const TypeResolutionResult& typeResult,
    VerificationResult& result) {

    for (const auto& unresolved : typeResult.unresolvedReferences) {
        result.addError("TYPE", "Unresolved forward reference: '" + unresolved.typeName +
                       "' referenced from '" + unresolved.referencedFrom + "'");
    }

    return typeResult.unresolvedReferences.empty();
}

bool SemanticVerifier::verifyOwnershipAnnotations(
    SemanticAnalyzer& analyzer,
    Parser::Program& program,
    VerificationResult& result) {

    // Check all class properties have ownership annotations
    const auto& classRegistry = analyzer.getClassRegistry();

    for (const auto& [className, classInfo] : classRegistry) {
        // Skip template classes - their parameters don't have concrete ownership yet
        if (classInfo.isTemplate) continue;

        for (const auto& [propName, propInfo] : classInfo.properties) {
            Parser::OwnershipType ownership = propInfo.second;
            if (ownership == Parser::OwnershipType::None) {
                // None is acceptable for primitives
                const std::string& propType = propInfo.first;
                if (propType != "Integer" && propType != "Float" &&
                    propType != "Double" && propType != "Bool" &&
                    propType != "String" && propType != "Void" &&
                    propType.find("Language::Core::") != 0) {
                    result.addWarning("Property '" + propName + "' in class '" + className +
                                    "' has no ownership annotation (type: " + propType + ")");
                }
            }
        }
    }

    return true;
}

bool SemanticVerifier::verifyNoCircularTypes(
    const TypeResolutionResult& /* typeResult */,
    VerificationResult& /* result */) {

    // Circular dependency detection is handled during type canonicalization
    // If we get here, the type resolution succeeded without circular dependencies
    return true;
}

//==============================================================================
// TEMPLATE INVARIANTS
//==============================================================================

bool SemanticVerifier::verifyAllClassesResolved(
    SemanticAnalyzer& analyzer,
    VerificationResult& result) {

    const auto& classRegistry = analyzer.getClassRegistry();

    if (classRegistry.empty()) {
        result.addWarning("Class registry is empty - this may indicate incomplete analysis");
    }

    // Check that all referenced base classes exist
    for (const auto& [className, classInfo] : classRegistry) {
        if (!classInfo.baseClassName.empty()) {
            if (classRegistry.find(classInfo.baseClassName) == classRegistry.end()) {
                result.addError("TYPE", "Class '" + className +
                               "' extends undefined base class '" +
                               classInfo.baseClassName + "'");
            }
        }
    }

    return result.success;
}

bool SemanticVerifier::verifyAllTemplatesResolved(
    SemanticAnalyzer& analyzer,
    VerificationResult& result) {

    const auto& templateInstantiations = analyzer.getTemplateInstantiations();
    const auto& templateClasses = analyzer.getTemplateClasses();

    for (const auto& inst : templateInstantiations) {
        // Check that the template exists
        auto it = templateClasses.find(inst.templateName);
        if (it == templateClasses.end()) {
            // Try without namespace
            bool found = false;
            for (const auto& [name, info] : templateClasses) {
                size_t pos = name.rfind("::");
                std::string simpleName = (pos != std::string::npos) ? name.substr(pos + 2) : name;
                if (simpleName == inst.templateName) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                result.addError("TEMPLATE", "Template class '" + inst.templateName +
                               "' not found in registry");
            }
        }

        // Verify template arguments are not empty or contain unresolved types
        for (const auto& arg : inst.arguments) {
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                if (arg.typeArg.empty()) {
                    result.addError("TEMPLATE", "Template instantiation of '" + inst.templateName +
                                   "' has empty type argument");
                }
                if (arg.typeArg == "Unknown") {
                    result.addError("TEMPLATE", "Template instantiation of '" + inst.templateName +
                                   "' has unresolved type argument");
                }
            }
        }
    }

    return result.success;
}

bool SemanticVerifier::verifyConstraintsSatisfied(
    const TemplateExpansionResult& templateResult,
    VerificationResult& result) {

    bool allSatisfied = true;

    // Check constraint proofs on instantiated classes
    for (const auto& inst : templateResult.instantiatedClasses) {
        for (const auto& proof : inst.constraintProofs) {
            if (!proof.satisfied) {
                std::stringstream ss;
                ss << "Constraint '" << proof.constraintName << "' not satisfied for type '";
                for (size_t i = 0; i < proof.typeArgs.size(); ++i) {
                    if (i > 0) ss << ", ";
                    ss << proof.typeArgs[i];
                }
                ss << "'";
                if (!proof.failureReason.empty()) {
                    ss << ": " << proof.failureReason;
                }
                result.addError("TEMPLATE", ss.str());
                allSatisfied = false;
            }
        }
    }

    // Check constraint proofs on instantiated methods
    for (const auto& inst : templateResult.instantiatedMethods) {
        for (const auto& proof : inst.constraintProofs) {
            if (!proof.satisfied) {
                result.addError("TEMPLATE", "Constraint '" + proof.constraintName +
                               "' not satisfied in method template: " + proof.failureReason);
                allSatisfied = false;
            }
        }
    }

    return allSatisfied;
}

// Helper function to check if a type string contains unresolved template parameters
static bool hasUnresolvedTemplateArg(const std::string& type) {
    // Check for "Deferred" which indicates incomplete substitution
    if (type == "Deferred" || type.find("Deferred") == 0) {
        return true;
    }

    // Check for single uppercase letter (template param like T, U, V, K, etc.)
    if (type.length() == 1 && std::isupper(type[0])) {
        return true;
    }

    // Check for template syntax with nested args
    auto anglePos = type.find('<');
    if (anglePos == std::string::npos) {
        // No template args, check if the whole thing is a template param
        // Common patterns: T, T^, T&, T%
        std::string baseName = type;
        // Strip ownership markers
        if (!baseName.empty() && (baseName.back() == '^' || baseName.back() == '&' || baseName.back() == '%')) {
            baseName = baseName.substr(0, baseName.length() - 1);
        }
        if (baseName.length() == 1 && std::isupper(baseName[0])) {
            return true;
        }
        return false;
    }

    // Has template args - extract and recursively check each one
    auto closePos = type.rfind('>');
    if (closePos == std::string::npos || closePos <= anglePos) {
        return false;
    }

    std::string argsStr = type.substr(anglePos + 1, closePos - anglePos - 1);

    // Split by comma, handling nested templates
    int depth = 0;
    std::string currentArg;
    for (char c : argsStr) {
        if (c == '<') depth++;
        else if (c == '>') depth--;
        else if (c == ',' && depth == 0) {
            // End of argument
            // Trim whitespace
            size_t start = currentArg.find_first_not_of(" \t");
            size_t end = currentArg.find_last_not_of(" \t");
            if (start != std::string::npos) {
                std::string arg = currentArg.substr(start, end - start + 1);
                if (hasUnresolvedTemplateArg(arg)) {
                    return true;
                }
            }
            currentArg.clear();
            continue;
        }
        currentArg += c;
    }

    // Check last argument
    if (!currentArg.empty()) {
        size_t start = currentArg.find_first_not_of(" \t");
        size_t end = currentArg.find_last_not_of(" \t");
        if (start != std::string::npos) {
            std::string arg = currentArg.substr(start, end - start + 1);
            if (hasUnresolvedTemplateArg(arg)) {
                return true;
            }
        }
    }

    return false;
}

bool SemanticVerifier::verifyNoUnexpandedGenerics(
    SemanticAnalyzer& analyzer,
    VerificationResult& result) {

    const auto& classRegistry = analyzer.getClassRegistry();

    for (const auto& [className, classInfo] : classRegistry) {
        // Check if class has unsubstituted template parameters
        if (classInfo.isTemplate && !classInfo.isInstantiated) {
            // Template definitions are ok, but shouldn't be used in codegen
            continue;
        }

        // Check properties for unsubstituted type parameters
        for (const auto& [propName, propInfo] : classInfo.properties) {
            const std::string& propType = propInfo.first;
            if (hasUnresolvedTemplateArg(propType)) {
                result.addError("TEMPLATE", "Unsubstituted type parameter in property '" +
                               propName + "' of class '" + className +
                               "' (type: " + propType + ")");
            }
        }

        // Check method signatures for unresolved types
        for (const auto& [methodName, methodInfo] : classInfo.methods) {
            // Check return type
            if (hasUnresolvedTemplateArg(methodInfo.returnType)) {
                result.addError("TEMPLATE", "Unsubstituted type parameter in return type of method '" +
                               className + "::" + methodName +
                               "' (type: " + methodInfo.returnType + ")");
            }

            // Check parameter types
            for (const auto& [paramType, ownership] : methodInfo.parameters) {
                if (hasUnresolvedTemplateArg(paramType)) {
                    result.addError("TEMPLATE", "Unsubstituted type parameter in parameter of method '" +
                                   className + "::" + methodName +
                                   "' (type: " + paramType + ")");
                }
            }
        }
    }

    return result.templateErrors == 0;
}

//==============================================================================
// OWNERSHIP INVARIANTS
//==============================================================================

bool SemanticVerifier::verifyNoUseAfterMove(
    const OwnershipAnalysisResult& ownershipResult,
    VerificationResult& result) {

    for (const auto& violation : ownershipResult.violations) {
        if (violation.kind == OwnershipViolation::Kind::UseAfterMove) {
            std::stringstream ss;
            ss << "Use-after-move: variable '" << violation.variableName
               << "' used after being moved";
            if (!violation.moveLocation.filename.empty()) {
                ss << " (moved at " << violation.moveLocation.filename
                   << ":" << violation.moveLocation.line << ")";
            }
            result.addError("OWNERSHIP", ss.str());
        }
    }

    return true;
}

bool SemanticVerifier::verifyNoDoubleMove(
    const OwnershipAnalysisResult& ownershipResult,
    VerificationResult& result) {

    for (const auto& violation : ownershipResult.violations) {
        if (violation.kind == OwnershipViolation::Kind::DoubleMoveViolation) {
            std::stringstream ss;
            ss << "Double-move: variable '" << violation.variableName
               << "' moved more than once";
            result.addError("OWNERSHIP", ss.str());
        }
    }

    return true;
}

bool SemanticVerifier::verifyReferencesValid(
    const OwnershipAnalysisResult& ownershipResult,
    VerificationResult& result) {

    for (const auto& violation : ownershipResult.violations) {
        if (violation.kind == OwnershipViolation::Kind::DanglingReference) {
            std::stringstream ss;
            ss << "Dangling reference: reference to '" << violation.variableName
               << "' outlives its target";
            result.addError("OWNERSHIP", ss.str());
        }
    }

    return true;
}

bool SemanticVerifier::verifyCapturesValid(
    const OwnershipAnalysisResult& ownershipResult,
    VerificationResult& result) {

    for (const auto& violation : ownershipResult.violations) {
        if (violation.kind == OwnershipViolation::Kind::InvalidCapture) {
            std::stringstream ss;
            ss << "Invalid lambda capture: '" << violation.variableName
               << "' cannot be captured with specified ownership";
            result.addError("OWNERSHIP", ss.str());
        }
    }

    return true;
}

//==============================================================================
// LAYOUT INVARIANTS
//==============================================================================

bool SemanticVerifier::verifyAllLayoutsComputed(
    const LayoutComputationResult& layoutResult,
    SemanticAnalyzer& analyzer,
    VerificationResult& result) {

    const auto& classRegistry = analyzer.getClassRegistry();

    for (const auto& [className, classInfo] : classRegistry) {
        // Skip templates - only instantiated classes need layouts
        if (classInfo.isTemplate && !classInfo.isInstantiated) continue;

        auto it = layoutResult.layouts.find(className);
        if (it == layoutResult.layouts.end()) {
            result.addError("LAYOUT", "Class '" + className + "' has no computed layout");
        }
    }

    return result.success;
}

bool SemanticVerifier::verifyReflectionMatches(
    const LayoutComputationResult& layoutResult,
    VerificationResult& result) {

    for (const auto& [className, layout] : layoutResult.layouts) {
        auto metaIt = layoutResult.metadata.find(className);
        if (metaIt == layoutResult.metadata.end()) {
            result.addError("LAYOUT", "Class '" + className +
                           "' has layout but no reflection metadata");
            continue;
        }

        const auto& metadata = metaIt->second;
        if (metadata.instanceSize != static_cast<int64_t>(layout.totalSize)) {
            result.addError("LAYOUT", "Class '" + className +
                           "' reflection size (" + std::to_string(metadata.instanceSize) +
                           ") doesn't match layout size (" + std::to_string(layout.totalSize) + ")");
        }
    }

    return result.success;
}

bool SemanticVerifier::verifyAlignmentValid(
    const LayoutComputationResult& layoutResult,
    VerificationResult& result) {

    for (const auto& [className, layout] : layoutResult.layouts) {
        // Check alignment is power of 2
        if (layout.alignment == 0 || (layout.alignment & (layout.alignment - 1)) != 0) {
            result.addError("LAYOUT", "Class '" + className +
                           "' has invalid alignment: " + std::to_string(layout.alignment));
        }

        // Check field alignments
        for (const auto& field : layout.fields) {
            if (field.alignment == 0 || (field.alignment & (field.alignment - 1)) != 0) {
                result.addError("LAYOUT", "Field '" + field.name + "' in class '" + className +
                               "' has invalid alignment: " + std::to_string(field.alignment));
            }
        }
    }

    return result.success;
}

bool SemanticVerifier::verifyFieldOffsets(
    const LayoutComputationResult& layoutResult,
    VerificationResult& result) {

    for (const auto& [className, layout] : layoutResult.layouts) {
        size_t lastOffset = 0;
        for (const auto& field : layout.fields) {
            if (field.offset < lastOffset) {
                result.addError("LAYOUT", "Field '" + field.name + "' in class '" + className +
                               "' has non-increasing offset: " + std::to_string(field.offset) +
                               " (previous field ended at " + std::to_string(lastOffset) + ")");
            }
            lastOffset = field.offset + field.size;
        }

        // Check total size accommodates all fields
        if (!layout.fields.empty()) {
            const auto& lastField = layout.fields.back();
            size_t minSize = lastField.offset + lastField.size;
            if (layout.totalSize < minSize) {
                result.addError("LAYOUT", "Class '" + className +
                               "' total size (" + std::to_string(layout.totalSize) +
                               ") is less than required (" + std::to_string(minSize) + ")");
            }
        }
    }

    return result.success;
}

//==============================================================================
// ABI/FFI INVARIANTS
//==============================================================================

bool SemanticVerifier::verifyFFISignaturesComplete(
    const ABILoweringResult& abiResult,
    VerificationResult& result) {

    for (const auto& [name, sig] : abiResult.nativeMethods) {
        if (!sig.isValid) {
            result.addError("ABI", "Native method '" + name + "' has invalid signature");
        }

        if (sig.dllPath.empty() && sig.symbolName.empty()) {
            result.addError("ABI", "Native method '" + name +
                           "' has no DLL path or symbol name");
        }

        // Check parameters
        for (const auto& param : sig.parameters) {
            if (param.llvmType.empty()) {
                result.addError("ABI", "Parameter '" + param.paramName +
                               "' in native method '" + name + "' has no LLVM type");
            }
        }

        // Check return type
        if (sig.returnInfo.llvmType.empty() && !sig.returnInfo.isVoid) {
            result.addError("ABI", "Native method '" + name +
                           "' has no return type");
        }
    }

    return result.success;
}

bool SemanticVerifier::verifyCallingConventionsValid(
    const ABILoweringResult& abiResult,
    VerificationResult& result) {

    for (const auto& [name, sig] : abiResult.nativeMethods) {
        // Check that calling convention is valid
        switch (sig.convention) {
            case Parser::CallingConvention::Auto:
                result.addError("ABI", "Native method '" + name +
                               "' has unresolved calling convention (Auto)");
                break;
            case Parser::CallingConvention::CDecl:
            case Parser::CallingConvention::StdCall:
            case Parser::CallingConvention::FastCall:
                // Valid conventions
                break;
            default:
                result.addError("ABI", "Native method '" + name +
                               "' has unknown calling convention");
                break;
        }
    }

    return result.success;
}

bool SemanticVerifier::verifyMarshalingStrategies(
    const ABILoweringResult& abiResult,
    VerificationResult& result) {

    for (const auto& [name, sig] : abiResult.nativeMethods) {
        for (const auto& param : sig.parameters) {
            // If parameter requires marshaling but strategy is None
            if (param.xxmlType != param.llvmType &&
                param.marshal == MarshalStrategy::None) {
                // Check if it's a native type (no marshaling needed)
                if (param.xxmlType.find("NativeType<") != 0) {
                    // FFI parameter requires explicit marshaling strategy
                    result.addError("ABI", "FFI parameter '" + param.paramName +
                                    "' in method '" + name +
                                    "' requires marshaling but none defined (XXML: " + param.xxmlType +
                                    " -> LLVM: " + param.llvmType + ")");
                }
            }
        }

        // Check return type marshaling
        if (!sig.returnInfo.isVoid &&
            sig.returnInfo.xxmlType != sig.returnInfo.llvmType &&
            sig.returnInfo.marshal == MarshalStrategy::None) {
            if (sig.returnInfo.xxmlType.find("NativeType<") != 0) {
                result.addError("ABI", "FFI return type in method '" + name +
                               "' requires marshaling but none defined (XXML: " + sig.returnInfo.xxmlType +
                               " -> LLVM: " + sig.returnInfo.llvmType + ")");
            }
        }
    }

    return result.abiErrors == 0;
}

//==============================================================================
// CONTROL FLOW INVARIANTS
//==============================================================================

// Control flow verification using CFG analysis
// Implemented in ControlFlowAnalyzer.h/cpp

bool SemanticVerifier::verifyAllMethodsReturn(
    Parser::Program& program,
    SemanticAnalyzer& /* analyzer */,
    VerificationResult& result) {

    // Traverse all declarations looking for classes/methods
    for (const auto& decl : program.declarations) {
        // Handle class declarations
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            const std::string& className = classDecl->name;

            // Iterate over all sections (public, private, protected)
            for (const auto& section : classDecl->sections) {
                for (const auto& memberDecl : section->declarations) {
                    // Handle method declarations
                    if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                        // Skip void/None methods - they don't need to return
                        std::string returnType = method->returnType ? method->returnType->typeName : "None";
                        if (returnType == "None" || returnType == "Void" || returnType.empty()) {
                            continue;
                        }

                        // Use ControlFlowVerifier to check this method
                        auto cfResult = XXML::Semantic::ControlFlowVerifier::verifyMethodReturns(
                            className, method->name, returnType, method->body);

                        // Merge errors
                        for (size_t i = 0; i < cfResult.errors.size(); ++i) {
                            result.addError("CONTROL_FLOW", cfResult.errors[i]);
                        }
                        // Merge warnings
                        for (size_t i = 0; i < cfResult.warnings.size(); ++i) {
                            result.addWarning(cfResult.warnings[i]);
                        }
                    }
                }
            }
        }
        // Handle namespace declarations (they can contain classes)
        else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            for (const auto& nsContent : nsDecl->declarations) {
                if (auto* innerClassDecl = dynamic_cast<Parser::ClassDecl*>(nsContent.get())) {
                    std::string className = nsDecl->name + "::" + innerClassDecl->name;

                    for (const auto& section : innerClassDecl->sections) {
                        for (const auto& memberDecl : section->declarations) {
                            if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                                std::string returnType = method->returnType ? method->returnType->typeName : "None";
                                if (returnType == "None" || returnType == "Void" || returnType.empty()) {
                                    continue;
                                }

                                auto cfResult = XXML::Semantic::ControlFlowVerifier::verifyMethodReturns(
                                    className, method->name, returnType, method->body);

                                for (size_t i = 0; i < cfResult.errors.size(); ++i) {
                                    result.addError("CONTROL_FLOW", cfResult.errors[i]);
                                }
                                for (size_t i = 0; i < cfResult.warnings.size(); ++i) {
                                    result.addWarning(cfResult.warnings[i]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return result.controlFlowErrors == 0;
}

bool SemanticVerifier::verifyBreakContinueValid(
    Parser::Program& program,
    VerificationResult& result) {

    // Traverse all declarations looking for classes/methods
    for (const auto& decl : program.declarations) {
        if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl.get())) {
            const std::string& className = classDecl->name;

            for (const auto& section : classDecl->sections) {
                for (const auto& memberDecl : section->declarations) {
                    if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                        auto cfResult = XXML::Semantic::ControlFlowVerifier::verifyBreakContinue(
                            className, method->name, method->body);

                        for (size_t i = 0; i < cfResult.errors.size(); ++i) {
                            result.addError("CONTROL_FLOW", cfResult.errors[i]);
                        }
                    }
                }
            }
        }
        else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl.get())) {
            for (const auto& nsContent : nsDecl->declarations) {
                if (auto* innerClassDecl = dynamic_cast<Parser::ClassDecl*>(nsContent.get())) {
                    std::string className = nsDecl->name + "::" + innerClassDecl->name;

                    for (const auto& section : innerClassDecl->sections) {
                        for (const auto& memberDecl : section->declarations) {
                            if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                                auto cfResult = XXML::Semantic::ControlFlowVerifier::verifyBreakContinue(
                                    className, method->name, method->body);

                                for (size_t i = 0; i < cfResult.errors.size(); ++i) {
                                    result.addError("CONTROL_FLOW", cfResult.errors[i]);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return result.controlFlowErrors == 0;
}

} // namespace Semantic
} // namespace XXML
