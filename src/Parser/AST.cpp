#include "../../include/Parser/AST.h"

namespace XXML {
namespace Parser {

// Forward declaration for expression cloning
std::unique_ptr<Expression> cloneExpression(const Expression* expr);

// TemplateArgument implementations
TemplateArgument::TemplateArgument(const TemplateArgument& other)
    : kind(other.kind), typeArg(other.typeArg), location(other.location) {
    if (other.valueArg) {
        valueArg = cloneExpression(other.valueArg.get());
    }
}

TemplateArgument& TemplateArgument::operator=(const TemplateArgument& other) {
    if (this != &other) {
        kind = other.kind;
        typeArg = other.typeArg;
        location = other.location;
        if (other.valueArg) {
            valueArg = cloneExpression(other.valueArg.get());
        } else {
            valueArg.reset();
        }
    }
    return *this;
}

// Expression cloning helper (for AST copying)
std::unique_ptr<Expression> cloneExpression(const Expression* expr) {
    if (!expr) return nullptr;

    if (auto* intLit = dynamic_cast<const IntegerLiteralExpr*>(expr)) {
        return std::make_unique<IntegerLiteralExpr>(intLit->value, intLit->location);
    }
    if (auto* strLit = dynamic_cast<const StringLiteralExpr*>(expr)) {
        return std::make_unique<StringLiteralExpr>(strLit->value, strLit->location);
    }
    if (auto* boolLit = dynamic_cast<const BoolLiteralExpr*>(expr)) {
        return std::make_unique<BoolLiteralExpr>(boolLit->value, boolLit->location);
    }
    if (auto* ident = dynamic_cast<const IdentifierExpr*>(expr)) {
        return std::make_unique<IdentifierExpr>(ident->name, ident->location);
    }
    if (auto* binExpr = dynamic_cast<const BinaryExpr*>(expr)) {
        return std::make_unique<BinaryExpr>(
            cloneExpression(binExpr->left.get()),
            binExpr->op,
            cloneExpression(binExpr->right.get()),
            binExpr->location
        );
    }
    // Add more expression types as needed
    return nullptr;
}

// TypeRef
void TypeRef::accept(ASTVisitor& visitor) { visitor.visit(*this); }

// Expressions
void IntegerLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void StringLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void BoolLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ThisExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void IdentifierExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ReferenceExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void MemberAccessExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void CallExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void BinaryExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }

// Statements
void InstantiateStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void RunStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ForStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ExitStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ReturnStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void IfStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void WhileStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void BreakStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ContinueStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void AssignmentStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }

// Declarations
void ParameterDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void PropertyDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ConstructorDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void MethodDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void AccessSection::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ClassDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void NamespaceDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ImportDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void EntrypointDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void Program::accept(ASTVisitor& visitor) { visitor.visit(*this); }

} // namespace Parser
} // namespace XXML
