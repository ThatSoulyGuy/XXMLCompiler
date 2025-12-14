#include "../../include/Parser/AST.h"

namespace XXML {
namespace Parser {

// Destructor definitions for classes with incomplete types in unique_ptr
// These must be defined here where the complete types are visible
TemplateArgument::~TemplateArgument() = default;
InstantiateStmt::~InstantiateStmt() = default;
PropertyDecl::~PropertyDecl() = default;
ConstructorDecl::~ConstructorDecl() = default;
DestructorDecl::~DestructorDecl() = default;
MethodDecl::~MethodDecl() = default;
AccessSection::~AccessSection() = default;
ClassDecl::~ClassDecl() = default;
StructureDecl::~StructureDecl() = default;
NativeStructureDecl::~NativeStructureDecl() = default;
CallbackTypeDecl::~CallbackTypeDecl() = default;

// TemplateArgument constructors (defined here where Expression is complete)
TemplateArgument::TemplateArgument(const std::string& type, const Common::SourceLocation& loc)
    : kind(Kind::Type), typeArg(type), valueArg(nullptr), location(loc) {}

TemplateArgument::TemplateArgument(std::unique_ptr<Expression> value, const Common::SourceLocation& loc)
    : kind(Kind::Value), typeArg(""), valueArg(std::move(value)), location(loc) {}

TemplateArgument::TemplateArgument(std::unique_ptr<Expression> value, const std::string& valStr, const Common::SourceLocation& loc)
    : kind(Kind::Value), typeArg(""), valueArg(std::move(value)), valueStr(valStr), location(loc) {}

TemplateArgument TemplateArgument::Wildcard(const Common::SourceLocation& loc) {
    TemplateArgument arg("", loc);
    arg.kind = Kind::Wildcard;
    return arg;
}

TemplateArgument::TemplateArgument(TemplateArgument&& other) noexcept = default;
TemplateArgument& TemplateArgument::operator=(TemplateArgument&& other) noexcept = default;

// LambdaExpr constructor and destructor (defined here where all types are complete)
LambdaExpr::LambdaExpr(std::vector<CaptureSpec> caps,
                       std::vector<std::unique_ptr<ParameterDecl>> params,
                       std::unique_ptr<TypeRef> retType,
                       std::vector<std::unique_ptr<Statement>> bodyStmts,
                       const Common::SourceLocation& loc)
    : Expression(loc), captures(std::move(caps)),
      parameters(std::move(params)), returnType(std::move(retType)),
      body(std::move(bodyStmts)) {}
LambdaExpr::~LambdaExpr() = default;

// Constructor definitions for classes with incomplete types in unique_ptr
InstantiateStmt::InstantiateStmt(std::unique_ptr<TypeRef> t, const std::string& name,
                                 std::unique_ptr<Expression> init, const Common::SourceLocation& loc)
    : Statement(loc), type(std::move(t)), variableName(name), initializer(std::move(init)) {}

PropertyDecl::PropertyDecl(const std::string& n, std::unique_ptr<TypeRef> t, const Common::SourceLocation& loc)
    : Declaration(loc), name(n), type(std::move(t)) {}

ConstructorDecl::ConstructorDecl(bool isDef, std::vector<std::unique_ptr<ParameterDecl>> params,
                                 std::vector<std::unique_ptr<Statement>> bodyStmts,
                                 const Common::SourceLocation& loc)
    : Declaration(loc), isDefault(isDef), parameters(std::move(params)), body(std::move(bodyStmts)) {}

DestructorDecl::DestructorDecl(std::vector<std::unique_ptr<Statement>> bodyStmts,
                               const Common::SourceLocation& loc)
    : Declaration(loc), body(std::move(bodyStmts)) {}

MethodDecl::MethodDecl(const std::string& n, std::unique_ptr<TypeRef> retType,
                       std::vector<std::unique_ptr<ParameterDecl>> params,
                       std::vector<std::unique_ptr<Statement>> bodyStmts,
                       const Common::SourceLocation& loc)
    : Declaration(loc), name(n), returnType(std::move(retType)),
      parameters(std::move(params)), body(std::move(bodyStmts)) {}

MethodDecl::MethodDecl(const std::string& n, const std::vector<TemplateParameter>& tparams,
                       std::unique_ptr<TypeRef> retType,
                       std::vector<std::unique_ptr<ParameterDecl>> params,
                       std::vector<std::unique_ptr<Statement>> bodyStmts,
                       const Common::SourceLocation& loc)
    : Declaration(loc), name(n), templateParams(tparams), returnType(std::move(retType)),
      parameters(std::move(params)), body(std::move(bodyStmts)) {}

AccessSection::AccessSection(AccessModifier mod, const Common::SourceLocation& loc)
    : ASTNode(loc), modifier(mod) {}

ClassDecl::ClassDecl(const std::string& n, const std::vector<TemplateParameter>& tparams,
                     bool final, const std::string& base,
                     const Common::SourceLocation& loc)
    : Declaration(loc), name(n), templateParams(tparams), isFinal(final), baseClass(base) {}

StructureDecl::StructureDecl(const std::string& n, const std::vector<TemplateParameter>& tparams,
                             const Common::SourceLocation& loc)
    : Declaration(loc), name(n), templateParams(tparams) {}

NativeStructureDecl::NativeStructureDecl(const std::string& n, size_t align, const Common::SourceLocation& loc)
    : Declaration(loc), name(n), alignment(align) {}

CallbackTypeDecl::CallbackTypeDecl(const std::string& n, CallingConvention conv,
                                   std::unique_ptr<TypeRef> retType,
                                   std::vector<std::unique_ptr<ParameterDecl>> params,
                                   const Common::SourceLocation& loc)
    : Declaration(loc), name(n), convention(conv),
      returnType(std::move(retType)), parameters(std::move(params)) {}

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
void StructureDecl::accept(ASTVisitor& visitor) { visitor.visit(*this); }
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
