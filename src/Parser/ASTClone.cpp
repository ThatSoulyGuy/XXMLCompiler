// AST cloning implementations for template instantiation
#include "../../include/Parser/AST.h"
#include <stdexcept>

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

// ============================================================================
// Statement Clones
// ============================================================================

std::unique_ptr<ASTNode> InstantiateStmt::clone() const {
    return cloneStmt();
}

std::unique_ptr<Statement> InstantiateStmt::cloneStmt() const {
    return std::make_unique<InstantiateStmt>(
        type->cloneType(),
        variableName,
        initializer->cloneExpr(),
        location
    );
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
    return std::make_unique<ReturnStmt>(value->cloneExpr(), location);
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
        variableName,
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
    return std::make_unique<PropertyDecl>(name, type->cloneType(), location);
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

    return std::make_unique<ConstructorDecl>(
        isDefault,
        std::move(clonedParams),
        std::move(clonedBody),
        location
    );
}

std::unique_ptr<ASTNode> MethodDecl::clone() const {
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

    return std::make_unique<MethodDecl>(
        name,
        templateParams,  // Copy template parameters
        returnType->cloneType(),
        std::move(clonedParams),
        std::move(clonedBody),
        location
    );
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
