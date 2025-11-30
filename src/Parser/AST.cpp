#include "../../include/Parser/AST.h"

namespace XXML {
namespace Parser {

// TypeRef
void TypeRef::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void FunctionTypeRef::accept(ASTVisitor& visitor) { visitor.visit(*this); }

// Expressions
void IntegerLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void FloatLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void DoubleLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void StringLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void BoolLiteralExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ThisExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void IdentifierExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ReferenceExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void MemberAccessExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void CallExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void BinaryExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void TypeOfExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void LambdaExpr::accept(ASTVisitor& visitor) { visitor.visit(*this); }

// Statements
void InstantiateStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void RequireStmt::accept(ASTVisitor& visitor) { visitor.visit(*this); }
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
void DestructorDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void MethodDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void AccessSection::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ClassDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void NativeStructureDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void CallbackTypeDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void EnumValueDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void EnumerationDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void NamespaceDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ImportDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void EntrypointDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ConstraintDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void AnnotateDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void ProcessorDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void AnnotationDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void AnnotationUsage::accept(ASTVisitor& visitor) { visitor.visit(*this); }
void Program::accept(ASTVisitor& visitor) { visitor.visit(*this); }

} // namespace Parser
} // namespace XXML
