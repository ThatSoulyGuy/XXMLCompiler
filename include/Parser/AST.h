#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../Common/SourceLocation.h"

namespace XXML {
namespace Parser {

// Forward declarations
class ASTVisitor;

// Base AST node
class ASTNode {
public:
    Common::SourceLocation location;

    ASTNode(const Common::SourceLocation& loc) : location(loc) {}
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
};

// Ownership type specifier
enum class OwnershipType {
    Owned,      // ^
    Reference,  // &
    Copy        // %
};

// Type reference
class TypeRef : public ASTNode {
public:
    std::string typeName;  // Could be qualified like "RenderStar::Default::MyClass"
    std::vector<std::string> templateArgs;  // Template arguments for instantiation (e.g., ["Integer", "String"])
    OwnershipType ownership;

    TypeRef(const std::string& name, OwnershipType own, const Common::SourceLocation& loc)
        : ASTNode(loc), typeName(name), ownership(own) {}

    TypeRef(const std::string& name, const std::vector<std::string>& args,
            OwnershipType own, const Common::SourceLocation& loc)
        : ASTNode(loc), typeName(name), templateArgs(args), ownership(own) {}

    void accept(ASTVisitor& visitor) override;
};

// Expressions
class Expression : public ASTNode {
public:
    Expression(const Common::SourceLocation& loc) : ASTNode(loc) {}
};

class IntegerLiteralExpr : public Expression {
public:
    int64_t value;

    IntegerLiteralExpr(int64_t val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
};

class StringLiteralExpr : public Expression {
public:
    std::string value;

    StringLiteralExpr(const std::string& val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
};

class BoolLiteralExpr : public Expression {
public:
    bool value;

    BoolLiteralExpr(bool val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
};

class ThisExpr : public Expression {
public:
    ThisExpr(const Common::SourceLocation& loc)
        : Expression(loc) {}

    void accept(ASTVisitor& visitor) override;
};

class IdentifierExpr : public Expression {
public:
    std::string name;

    IdentifierExpr(const std::string& n, const Common::SourceLocation& loc)
        : Expression(loc), name(n) {}

    void accept(ASTVisitor& visitor) override;
};

class ReferenceExpr : public Expression {
public:
    std::unique_ptr<Expression> expr;  // Expression being referenced (&expr)

    ReferenceExpr(std::unique_ptr<Expression> e, const Common::SourceLocation& loc)
        : Expression(loc), expr(std::move(e)) {}

    void accept(ASTVisitor& visitor) override;
};

class MemberAccessExpr : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string member;

    MemberAccessExpr(std::unique_ptr<Expression> obj, const std::string& mem,
                     const Common::SourceLocation& loc)
        : Expression(loc), object(std::move(obj)), member(mem) {}

    void accept(ASTVisitor& visitor) override;
};

class CallExpr : public Expression {
public:
    std::unique_ptr<Expression> callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    CallExpr(std::unique_ptr<Expression> call, std::vector<std::unique_ptr<Expression>> args,
             const Common::SourceLocation& loc)
        : Expression(loc), callee(std::move(call)), arguments(std::move(args)) {}

    void accept(ASTVisitor& visitor) override;
};

class BinaryExpr : public Expression {
public:
    std::unique_ptr<Expression> left;
    std::string op;
    std::unique_ptr<Expression> right;

    BinaryExpr(std::unique_ptr<Expression> l, const std::string& operation,
               std::unique_ptr<Expression> r, const Common::SourceLocation& loc)
        : Expression(loc), left(std::move(l)), op(operation), right(std::move(r)) {}

    void accept(ASTVisitor& visitor) override;
};

// Statements
class Statement : public ASTNode {
public:
    Statement(const Common::SourceLocation& loc) : ASTNode(loc) {}
};

class InstantiateStmt : public Statement {
public:
    std::unique_ptr<TypeRef> type;
    std::string variableName;
    std::unique_ptr<Expression> initializer;

    InstantiateStmt(std::unique_ptr<TypeRef> t, const std::string& name,
                    std::unique_ptr<Expression> init, const Common::SourceLocation& loc)
        : Statement(loc), type(std::move(t)), variableName(name), initializer(std::move(init)) {}

    void accept(ASTVisitor& visitor) override;
};

class RunStmt : public Statement {
public:
    std::unique_ptr<Expression> expression;

    RunStmt(std::unique_ptr<Expression> expr, const Common::SourceLocation& loc)
        : Statement(loc), expression(std::move(expr)) {}

    void accept(ASTVisitor& visitor) override;
};

class ForStmt : public Statement {
public:
    std::unique_ptr<TypeRef> iteratorType;
    std::string iteratorName;
    std::unique_ptr<Expression> rangeStart;
    std::unique_ptr<Expression> rangeEnd;
    std::vector<std::unique_ptr<Statement>> body;

    ForStmt(std::unique_ptr<TypeRef> type, const std::string& name,
            std::unique_ptr<Expression> start, std::unique_ptr<Expression> end,
            std::vector<std::unique_ptr<Statement>> bodyStmts, const Common::SourceLocation& loc)
        : Statement(loc), iteratorType(std::move(type)), iteratorName(name),
          rangeStart(std::move(start)), rangeEnd(std::move(end)), body(std::move(bodyStmts)) {}

    void accept(ASTVisitor& visitor) override;
};

class ExitStmt : public Statement {
public:
    std::unique_ptr<Expression> exitCode;

    ExitStmt(std::unique_ptr<Expression> code, const Common::SourceLocation& loc)
        : Statement(loc), exitCode(std::move(code)) {}

    void accept(ASTVisitor& visitor) override;
};

class ReturnStmt : public Statement {
public:
    std::unique_ptr<Expression> value;

    ReturnStmt(std::unique_ptr<Expression> val, const Common::SourceLocation& loc)
        : Statement(loc), value(std::move(val)) {}

    void accept(ASTVisitor& visitor) override;
};

class IfStmt : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<Statement>> thenBranch;
    std::vector<std::unique_ptr<Statement>> elseBranch;

    IfStmt(std::unique_ptr<Expression> cond,
           std::vector<std::unique_ptr<Statement>> thenStmts,
           std::vector<std::unique_ptr<Statement>> elseStmts,
           const Common::SourceLocation& loc)
        : Statement(loc), condition(std::move(cond)),
          thenBranch(std::move(thenStmts)),
          elseBranch(std::move(elseStmts)) {}

    void accept(ASTVisitor& visitor) override;
};

class WhileStmt : public Statement {
public:
    std::unique_ptr<Expression> condition;
    std::vector<std::unique_ptr<Statement>> body;

    WhileStmt(std::unique_ptr<Expression> cond,
              std::vector<std::unique_ptr<Statement>> bodyStmts,
              const Common::SourceLocation& loc)
        : Statement(loc), condition(std::move(cond)),
          body(std::move(bodyStmts)) {}

    void accept(ASTVisitor& visitor) override;
};

class BreakStmt : public Statement {
public:
    BreakStmt(const Common::SourceLocation& loc)
        : Statement(loc) {}

    void accept(ASTVisitor& visitor) override;
};

class ContinueStmt : public Statement {
public:
    ContinueStmt(const Common::SourceLocation& loc)
        : Statement(loc) {}

    void accept(ASTVisitor& visitor) override;
};

// Declarations
class Declaration : public ASTNode {
public:
    Declaration(const Common::SourceLocation& loc) : ASTNode(loc) {}
};

class ParameterDecl : public Declaration {
public:
    std::string name;
    std::unique_ptr<TypeRef> type;

    ParameterDecl(const std::string& n, std::unique_ptr<TypeRef> t, const Common::SourceLocation& loc)
        : Declaration(loc), name(n), type(std::move(t)) {}

    void accept(ASTVisitor& visitor) override;
};

class PropertyDecl : public Declaration {
public:
    std::string name;
    std::unique_ptr<TypeRef> type;

    PropertyDecl(const std::string& n, std::unique_ptr<TypeRef> t, const Common::SourceLocation& loc)
        : Declaration(loc), name(n), type(std::move(t)) {}

    void accept(ASTVisitor& visitor) override;
};

class ConstructorDecl : public Declaration {
public:
    bool isDefault;
    std::vector<std::unique_ptr<ParameterDecl>> parameters;
    std::vector<std::unique_ptr<Statement>> body;

    ConstructorDecl(bool isDef, std::vector<std::unique_ptr<ParameterDecl>> params,
                    std::vector<std::unique_ptr<Statement>> bodyStmts,
                    const Common::SourceLocation& loc)
        : Declaration(loc), isDefault(isDef), parameters(std::move(params)),
          body(std::move(bodyStmts)) {}

    void accept(ASTVisitor& visitor) override;
};

class MethodDecl : public Declaration {
public:
    std::string name;
    std::unique_ptr<TypeRef> returnType;
    std::vector<std::unique_ptr<ParameterDecl>> parameters;
    std::vector<std::unique_ptr<Statement>> body;

    MethodDecl(const std::string& n, std::unique_ptr<TypeRef> retType,
               std::vector<std::unique_ptr<ParameterDecl>> params,
               std::vector<std::unique_ptr<Statement>> bodyStmts,
               const Common::SourceLocation& loc)
        : Declaration(loc), name(n), returnType(std::move(retType)),
          parameters(std::move(params)), body(std::move(bodyStmts)) {}

    void accept(ASTVisitor& visitor) override;
};

// Template parameter
struct TemplateParameter {
    std::string name;
    std::vector<std::string> constraints;  // Empty means no constraints (any type)
    Common::SourceLocation location;

    TemplateParameter(const std::string& n, const std::vector<std::string>& c,
                     const Common::SourceLocation& loc)
        : name(n), constraints(c), location(loc) {}
};

enum class AccessModifier {
    Public,
    Private,
    Protected
};

class AccessSection : public ASTNode {
public:
    AccessModifier modifier;
    std::vector<std::unique_ptr<Declaration>> declarations;

    AccessSection(AccessModifier mod, const Common::SourceLocation& loc)
        : ASTNode(loc), modifier(mod) {}

    void accept(ASTVisitor& visitor) override;
};

class ClassDecl : public Declaration {
public:
    std::string name;
    std::vector<TemplateParameter> templateParams;  // Template parameters if this is a template class
    bool isFinal;
    std::string baseClass;  // Empty string if no base class (Extends None)
    std::vector<std::unique_ptr<AccessSection>> sections;

    ClassDecl(const std::string& n, const std::vector<TemplateParameter>& tparams,
              bool final, const std::string& base,
              const Common::SourceLocation& loc)
        : Declaration(loc), name(n), templateParams(tparams), isFinal(final), baseClass(base) {}

    void accept(ASTVisitor& visitor) override;
};

class NamespaceDecl : public Declaration {
public:
    std::string name;  // Could be qualified like "RenderStar::Default"
    std::vector<std::unique_ptr<Declaration>> declarations;

    NamespaceDecl(const std::string& n, const Common::SourceLocation& loc)
        : Declaration(loc), name(n) {}

    void accept(ASTVisitor& visitor) override;
};

class ImportDecl : public Declaration {
public:
    std::string modulePath;  // e.g., "Language::Core"

    ImportDecl(const std::string& path, const Common::SourceLocation& loc)
        : Declaration(loc), modulePath(path) {}

    void accept(ASTVisitor& visitor) override;
};

class EntrypointDecl : public Declaration {
public:
    std::vector<std::unique_ptr<Statement>> body;

    EntrypointDecl(std::vector<std::unique_ptr<Statement>> bodyStmts,
                   const Common::SourceLocation& loc)
        : Declaration(loc), body(std::move(bodyStmts)) {}

    void accept(ASTVisitor& visitor) override;
};

// Root node
class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Declaration>> declarations;

    Program(const Common::SourceLocation& loc) : ASTNode(loc) {}

    void accept(ASTVisitor& visitor) override;
};

// Visitor interface for AST traversal
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Program& node) = 0;
    virtual void visit(ImportDecl& node) = 0;
    virtual void visit(NamespaceDecl& node) = 0;
    virtual void visit(ClassDecl& node) = 0;
    virtual void visit(AccessSection& node) = 0;
    virtual void visit(PropertyDecl& node) = 0;
    virtual void visit(ConstructorDecl& node) = 0;
    virtual void visit(MethodDecl& node) = 0;
    virtual void visit(ParameterDecl& node) = 0;
    virtual void visit(EntrypointDecl& node) = 0;

    virtual void visit(InstantiateStmt& node) = 0;
    virtual void visit(RunStmt& node) = 0;
    virtual void visit(ForStmt& node) = 0;
    virtual void visit(ExitStmt& node) = 0;
    virtual void visit(ReturnStmt& node) = 0;
    virtual void visit(IfStmt& node) = 0;
    virtual void visit(WhileStmt& node) = 0;
    virtual void visit(BreakStmt& node) = 0;
    virtual void visit(ContinueStmt& node) = 0;

    virtual void visit(IntegerLiteralExpr& node) = 0;
    virtual void visit(StringLiteralExpr& node) = 0;
    virtual void visit(BoolLiteralExpr& node) = 0;
    virtual void visit(ThisExpr& node) = 0;
    virtual void visit(IdentifierExpr& node) = 0;
    virtual void visit(ReferenceExpr& node) = 0;
    virtual void visit(MemberAccessExpr& node) = 0;
    virtual void visit(CallExpr& node) = 0;
    virtual void visit(BinaryExpr& node) = 0;

    virtual void visit(TypeRef& node) = 0;
};

} // namespace Parser
} // namespace XXML
