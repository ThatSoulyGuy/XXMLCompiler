// AST cloning implementations for template instantiation
#include "../../include/Parser/AST.h"
#include <stdexcept>
#include <iostream>

namespace XXML {
namespace Parser {

// ============================================================================
// TemplateArgument Clone Support
// ============================================================================

TemplateArgument::TemplateArgument(const TemplateArgument& other)
    : kind(other.kind), typeArg(other.typeArg), valueArg(nullptr), location(other.location) {
    if (other.valueArg) {
        valueArg = other.valueArg->cloneExpr();
    }
}

TemplateArgument& TemplateArgument::operator=(const TemplateArgument& other) {
    if (this != &other) {
        kind = other.kind;
        typeArg = other.typeArg;
        location = other.location;
        if (other.valueArg) {
            valueArg = other.valueArg->cloneExpr();
        } else {
            valueArg = nullptr;
        }
    }
    return *this;
}

// ============================================================================
// TypeRef Clone
// ============================================================================

std::unique_ptr<ASTNode> TypeRef::clone() const {
    return cloneType();
}

std::unique_ptr<TypeRef> TypeRef::cloneType() const {
    auto cloned = std::make_unique<TypeRef>(typeName, ownership, location, isTemplateParameter);
    // Deep clone template arguments
    for (const auto& arg : templateArgs) {
        cloned->templateArgs.push_back(arg);  // Uses TemplateArgument copy constructor
    }
    return cloned;
}

// ============================================================================
// FunctionTypeRef Clone
// ============================================================================

std::unique_ptr<ASTNode> FunctionTypeRef::clone() const {
    return cloneType();
}

std::unique_ptr<TypeRef> FunctionTypeRef::cloneType() const {
    std::vector<std::unique_ptr<TypeRef>> clonedParams;
    for (const auto& param : paramTypes) {
        clonedParams.push_back(param->cloneType());
    }
    return std::make_unique<FunctionTypeRef>(
        returnType->cloneType(),
        std::move(clonedParams),
        ownership,
        location
    );
}

// ============================================================================
// Expression Clones
// ============================================================================

std::unique_ptr<ASTNode> IntegerLiteralExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> IntegerLiteralExpr::cloneExpr() const {
    return std::make_unique<IntegerLiteralExpr>(value, location);
}

std::unique_ptr<ASTNode> StringLiteralExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> StringLiteralExpr::cloneExpr() const {
    return std::make_unique<StringLiteralExpr>(value, location);
}

std::unique_ptr<ASTNode> BoolLiteralExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> BoolLiteralExpr::cloneExpr() const {
    return std::make_unique<BoolLiteralExpr>(value, location);
}

std::unique_ptr<ASTNode> FloatLiteralExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> FloatLiteralExpr::cloneExpr() const {
    return std::make_unique<FloatLiteralExpr>(value, location);
}

std::unique_ptr<ASTNode> DoubleLiteralExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> DoubleLiteralExpr::cloneExpr() const {
    return std::make_unique<DoubleLiteralExpr>(value, location);
}

std::unique_ptr<ASTNode> ThisExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> ThisExpr::cloneExpr() const {
    return std::make_unique<ThisExpr>(location);
}

std::unique_ptr<ASTNode> IdentifierExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> IdentifierExpr::cloneExpr() const {
    return std::make_unique<IdentifierExpr>(name, location);
}

std::unique_ptr<ASTNode> ReferenceExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> ReferenceExpr::cloneExpr() const {
    return std::make_unique<ReferenceExpr>(expr->cloneExpr(), location);
}

std::unique_ptr<ASTNode> MemberAccessExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> MemberAccessExpr::cloneExpr() const {
    return std::make_unique<MemberAccessExpr>(object->cloneExpr(), member, location);
}

std::unique_ptr<ASTNode> CallExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> CallExpr::cloneExpr() const {
    std::vector<std::unique_ptr<Expression>> clonedArgs;
    for (const auto& arg : arguments) {
        clonedArgs.push_back(arg->cloneExpr());
    }
    return std::make_unique<CallExpr>(callee->cloneExpr(), std::move(clonedArgs), location);
}

std::unique_ptr<ASTNode> BinaryExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> BinaryExpr::cloneExpr() const {
    return std::make_unique<BinaryExpr>(
        left ? left->cloneExpr() : nullptr,
        op,
        right->cloneExpr(),
        location
    );
}

std::unique_ptr<ASTNode> TypeOfExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> TypeOfExpr::cloneExpr() const {
    return std::make_unique<TypeOfExpr>(type->cloneType(), location);
}

std::unique_ptr<ASTNode> LambdaExpr::clone() const {
    return cloneExpr();
}

std::unique_ptr<Expression> LambdaExpr::cloneExpr() const {
    // Clone captures (CaptureSpec is a simple struct, can copy directly)
    std::vector<CaptureSpec> clonedCaptures = captures;

    // Clone parameters
    std::vector<std::unique_ptr<ParameterDecl>> clonedParams;
    for (const auto& param : parameters) {
        clonedParams.push_back(
            std::unique_ptr<ParameterDecl>(
                static_cast<ParameterDecl*>(param->cloneDecl().release())
            )
        );
    }

    // Clone body
    std::vector<std::unique_ptr<Statement>> clonedBody;
    for (const auto& stmt : body) {
        clonedBody.push_back(stmt->cloneStmt());
    }

    auto cloned = std::make_unique<LambdaExpr>(
        std::move(clonedCaptures),
        std::move(clonedParams),
        returnType->cloneType(),
        std::move(clonedBody),
        location
    );
    cloned->isCompiletime = isCompiletime;
    return cloned;
}

// ============================================================================
// Statement Clones
// ============================================================================

std::unique_ptr<ASTNode> InstantiateStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> InstantiateStmt::cloneStmt() const {
    auto cloned = std::make_unique<InstantiateStmt>(
        type->cloneType(),
        variableName,
        initializer->cloneExpr(),
        location
    );
    cloned->isCompiletime = isCompiletime;
    return cloned;
}

std::unique_ptr<ASTNode> RequireStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> RequireStmt::cloneStmt() const {
    auto cloned = std::make_unique<RequireStmt>(kind, location);

    // Clone based on kind
    if (kind == RequirementKind::Method || kind == RequirementKind::CompiletimeMethod) {
        cloned->methodReturnType = methodReturnType->cloneType();
        cloned->methodName = methodName;
        cloned->targetParam = targetParam;
    } else if (kind == RequirementKind::Constructor || kind == RequirementKind::CompiletimeConstructor) {
        for (const auto& paramType : constructorParamTypes) {
            cloned->constructorParamTypes.push_back(paramType->cloneType());
        }
        cloned->targetParam = targetParam;
    } else if (kind == RequirementKind::Truth) {
        cloned->truthCondition = truthCondition->cloneExpr();
    }

    return cloned;
}

std::unique_ptr<ASTNode> RunStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> RunStmt::cloneStmt() const {
    return std::make_unique<RunStmt>(expression->cloneExpr(), location);
}

std::unique_ptr<ASTNode> ForStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> ForStmt::cloneStmt() const {
    std::vector<std::unique_ptr<Statement>> clonedBody;
    for (const auto& stmt : body) {
        clonedBody.push_back(stmt->cloneStmt());
    }

    return std::make_unique<ForStmt>(
        iteratorType->cloneType(),
        iteratorName,
        rangeStart->cloneExpr(),
        rangeEnd->cloneExpr(),
        std::move(clonedBody),
        location
    );
}

std::unique_ptr<ASTNode> ExitStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> ExitStmt::cloneStmt() const {
    return std::make_unique<ExitStmt>(exitCode->cloneExpr(), location);
}

std::unique_ptr<ASTNode> ReturnStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> ReturnStmt::cloneStmt() const {
    return std::make_unique<ReturnStmt>(value ? value->cloneExpr() : nullptr, location);
}

std::unique_ptr<ASTNode> IfStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> IfStmt::cloneStmt() const {
    std::vector<std::unique_ptr<Statement>> clonedThenBranch;
    for (const auto& stmt : thenBranch) {
        clonedThenBranch.push_back(stmt->cloneStmt());
    }

    std::vector<std::unique_ptr<Statement>> clonedElseBranch;
    for (const auto& stmt : elseBranch) {
        clonedElseBranch.push_back(stmt->cloneStmt());
    }

    return std::make_unique<IfStmt>(
        condition->cloneExpr(),
        std::move(clonedThenBranch),
        std::move(clonedElseBranch),
        location
    );
}

std::unique_ptr<ASTNode> WhileStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> WhileStmt::cloneStmt() const {
    std::vector<std::unique_ptr<Statement>> clonedBody;
    for (const auto& stmt : body) {
        clonedBody.push_back(stmt->cloneStmt());
    }

    return std::make_unique<WhileStmt>(
        condition->cloneExpr(),
        std::move(clonedBody),
        location
    );
}

std::unique_ptr<ASTNode> BreakStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> BreakStmt::cloneStmt() const {
    return std::make_unique<BreakStmt>(location);
}

std::unique_ptr<ASTNode> ContinueStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> ContinueStmt::cloneStmt() const {
    return std::make_unique<ContinueStmt>(location);
}

std::unique_ptr<ASTNode> AssignmentStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> AssignmentStmt::cloneStmt() const {
    return std::make_unique<AssignmentStmt>(
        target->cloneExpr(),
        value->cloneExpr(),
        location
    );
}

// ============================================================================
// Declaration Clones
// ============================================================================

std::unique_ptr<ASTNode> ParameterDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> ParameterDecl::cloneDecl() const {
    return std::make_unique<ParameterDecl>(name, type->cloneType(), location);
}

std::unique_ptr<ASTNode> PropertyDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> PropertyDecl::cloneDecl() const {
    auto cloned = std::make_unique<PropertyDecl>(name, type->cloneType(), location);
    cloned->isCompiletime = isCompiletime;
    return cloned;
}

std::unique_ptr<ASTNode> ConstructorDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> ConstructorDecl::cloneDecl() const {
    std::vector<std::unique_ptr<ParameterDecl>> clonedParams;
    for (const auto& param : parameters) {
        clonedParams.push_back(
            std::unique_ptr<ParameterDecl>(
                static_cast<ParameterDecl*>(param->cloneDecl().release())
            )
        );
    }

    std::vector<std::unique_ptr<Statement>> clonedBody;
    for (const auto& stmt : body) {
        clonedBody.push_back(stmt->cloneStmt());
    }

    auto cloned = std::make_unique<ConstructorDecl>(
        isDefault,
        std::move(clonedParams),
        std::move(clonedBody),
        location
    );
    cloned->isCompiletime = isCompiletime;
    return cloned;
}

std::unique_ptr<ASTNode> DestructorDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> DestructorDecl::cloneDecl() const {
    std::vector<std::unique_ptr<Statement>> clonedBody;
    for (const auto& stmt : body) {
        clonedBody.push_back(stmt->cloneStmt());
    }

    return std::make_unique<DestructorDecl>(
        std::move(clonedBody),
        location
    );
}

std::unique_ptr<ASTNode> MethodDecl::clone() const{
    return cloneDecl();
}

std::unique_ptr<Declaration> MethodDecl::cloneDecl() const {
    std::vector<std::unique_ptr<ParameterDecl>> clonedParams;
    for (const auto& param : parameters) {
        clonedParams.push_back(
            std::unique_ptr<ParameterDecl>(
                static_cast<ParameterDecl*>(param->cloneDecl().release())
            )
        );
    }

    std::vector<std::unique_ptr<Statement>> clonedBody;
    for (const auto& stmt : body) {
        clonedBody.push_back(stmt->cloneStmt());
    }

    auto cloned = std::make_unique<MethodDecl>(
        name,
        templateParams,  // Copy template parameters
        returnType->cloneType(),
        std::move(clonedParams),
        std::move(clonedBody),
        location
    );

    // Copy FFI fields
    cloned->isNative = isNative;
    cloned->nativePath = nativePath;
    cloned->nativeSymbol = nativeSymbol;
    cloned->callingConvention = callingConvention;

    // Copy compile-time flag
    cloned->isCompiletime = isCompiletime;

    return cloned;
}

// ============================================================================
// AccessSection Clone
// ============================================================================

std::unique_ptr<ASTNode> AccessSection::clone() const {
    auto cloned = std::make_unique<AccessSection>(modifier, location);

    for (const auto& decl : declarations) {
        cloned->declarations.push_back(decl->cloneDecl());
    }

    return cloned;
}

// AccessSection cloning helper
std::unique_ptr<AccessSection> cloneAccessSection(const AccessSection& section) {
    return std::unique_ptr<AccessSection>(
        static_cast<AccessSection*>(section.clone().release())
    );
}

std::unique_ptr<ASTNode> ClassDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> ClassDecl::cloneDecl() const {
    // Note: Template parameters are POD and can be copied directly
    auto cloned = std::make_unique<ClassDecl>(
        name,
        templateParams,
        isFinal,
        baseClass,
        location
    );

    // Clone sections
    for (const auto& section : sections) {
        cloned->sections.push_back(cloneAccessSection(*section));
    }

    // Copy compile-time flag
    cloned->isCompiletime = isCompiletime;

    return cloned;
}

std::unique_ptr<ASTNode> StructureDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> StructureDecl::cloneDecl() const {
    auto cloned = std::make_unique<StructureDecl>(
        name,
        templateParams,
        location
    );

    // Clone sections
    for (const auto& section : sections) {
        cloned->sections.push_back(cloneAccessSection(*section));
    }

    // Copy compile-time flag
    cloned->isCompiletime = isCompiletime;

    return cloned;
}

std::unique_ptr<ASTNode> NativeStructureDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> NativeStructureDecl::cloneDecl() const {
    auto cloned = std::make_unique<NativeStructureDecl>(name, alignment, location);

    // Clone properties
    for (const auto& prop : properties) {
        cloned->properties.push_back(
            std::unique_ptr<PropertyDecl>(
                static_cast<PropertyDecl*>(prop->cloneDecl().release())
            )
        );
    }

    return cloned;
}

std::unique_ptr<ASTNode> CallbackTypeDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> CallbackTypeDecl::cloneDecl() const {
    std::vector<std::unique_ptr<ParameterDecl>> clonedParams;
    for (const auto& param : parameters) {
        clonedParams.push_back(
            std::unique_ptr<ParameterDecl>(
                static_cast<ParameterDecl*>(param->cloneDecl().release())
            )
        );
    }

    return std::make_unique<CallbackTypeDecl>(
        name,
        convention,
        returnType->cloneType(),
        std::move(clonedParams),
        location
    );
}

std::unique_ptr<ASTNode> EnumValueDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> EnumValueDecl::cloneDecl() const {
    return std::make_unique<EnumValueDecl>(name, hasExplicitValue, value, location);
}

std::unique_ptr<ASTNode> EnumerationDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> EnumerationDecl::cloneDecl() const {
    auto cloned = std::make_unique<EnumerationDecl>(name, location);

    for (const auto& val : values) {
        cloned->values.push_back(
            std::unique_ptr<EnumValueDecl>(
                static_cast<EnumValueDecl*>(val->cloneDecl().release())
            )
        );
    }

    return cloned;
}

std::unique_ptr<ASTNode> NamespaceDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> NamespaceDecl::cloneDecl() const {
    auto cloned = std::make_unique<NamespaceDecl>(name, location);

    for (const auto& decl : declarations) {
        cloned->declarations.push_back(decl->cloneDecl());
    }

    return cloned;
}

std::unique_ptr<ASTNode> ImportDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> ImportDecl::cloneDecl() const {
    return std::make_unique<ImportDecl>(modulePath, location);
}

std::unique_ptr<ASTNode> EntrypointDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> EntrypointDecl::cloneDecl() const {
    std::vector<std::unique_ptr<Statement>> clonedBody;
    for (const auto& stmt : body) {
        clonedBody.push_back(stmt->cloneStmt());
    }

    return std::make_unique<EntrypointDecl>(std::move(clonedBody), location);
}

std::unique_ptr<ASTNode> ConstraintDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> ConstraintDecl::cloneDecl() const {
    auto cloned = std::make_unique<ConstraintDecl>(name, templateParams, paramBindings, location);

    // Clone all requirements
    for (const auto& req : requirements) {
        cloned->requirements.push_back(
            std::unique_ptr<RequireStmt>(static_cast<RequireStmt*>(req->cloneStmt().release()))
        );
    }

    return cloned;
}

// ============================================================================
// Annotation Clones
// ============================================================================

std::unique_ptr<ASTNode> AnnotateDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> AnnotateDecl::cloneDecl() const {
    return std::make_unique<AnnotateDecl>(
        name,
        type->cloneType(),
        defaultValue ? defaultValue->cloneExpr() : nullptr,
        location
    );
}

std::unique_ptr<ASTNode> ProcessorDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> ProcessorDecl::cloneDecl() const {
    std::vector<std::unique_ptr<AccessSection>> clonedSections;
    for (const auto& section : sections) {
        clonedSections.push_back(cloneAccessSection(*section));
    }
    return std::make_unique<ProcessorDecl>(std::move(clonedSections), location);
}

std::unique_ptr<ASTNode> AnnotationDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> AnnotationDecl::cloneDecl() const {
    std::vector<std::unique_ptr<AnnotateDecl>> clonedParams;
    for (const auto& param : parameters) {
        clonedParams.push_back(
            std::unique_ptr<AnnotateDecl>(
                static_cast<AnnotateDecl*>(param->cloneDecl().release())
            )
        );
    }

    std::unique_ptr<ProcessorDecl> clonedProcessor;
    if (processor) {
        clonedProcessor = std::unique_ptr<ProcessorDecl>(
            static_cast<ProcessorDecl*>(processor->cloneDecl().release())
        );
    }

    return std::make_unique<AnnotationDecl>(
        name,
        allowedTargets,  // vector copy
        std::move(clonedParams),
        std::move(clonedProcessor),
        retainAtRuntime,
        location
    );
}

std::unique_ptr<ASTNode> AnnotationUsage::clone() const {
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> clonedArgs;
    for (const auto& arg : arguments) {
        clonedArgs.push_back({arg.first, arg.second->cloneExpr()});
    }
    return std::make_unique<AnnotationUsage>(annotationName, std::move(clonedArgs), location);
}

std::unique_ptr<ASTNode> DeriveDecl::clone() const {
    return cloneDecl();
}

std::unique_ptr<Declaration> DeriveDecl::cloneDecl() const {
    std::vector<std::unique_ptr<AccessSection>> clonedSections;
    for (const auto& section : sections) {
        clonedSections.push_back(cloneAccessSection(*section));
    }

    auto cloned = std::make_unique<DeriveDecl>(
        name,
        std::move(clonedSections),
        location
    );

    // Note: generateMethod and canDeriveMethod are non-owning pointers
    // They will need to be re-resolved after cloning by walking the sections
    // For now, leave them as nullptr - they can be resolved during semantic analysis

    return cloned;
}

// ============================================================================
// Program Clone
// ============================================================================

std::unique_ptr<ASTNode> Program::clone() const {
    auto cloned = std::make_unique<Program>(location);

    for (const auto& decl : declarations) {
        cloned->declarations.push_back(decl->cloneDecl());
    }

    return cloned;
}

} // namespace Parser
} // namespace XXML
