#pragma once
#include <string>
#include <vector>
#include <memory>
#include "../Common/SourceLocation.h"

namespace XXML {
namespace Parser {

// Forward declarations
class ASTVisitor;
class Statement;
class ParameterDecl;
class AnnotationUsage;
class AnnotationDecl;
class AnnotateDecl;
class ProcessorDecl;
class StructureDecl;
class NativeStructureDecl;
class CallbackTypeDecl;
class EnumerationDecl;
class EnumValueDecl;
struct TemplateParameter;  // Forward declare for LambdaExpr

// Base AST node
class ASTNode {
public:
    Common::SourceLocation location;

    ASTNode(const Common::SourceLocation& loc) : location(loc) {}
    virtual ~ASTNode() = default;
    virtual void accept(ASTVisitor& visitor) = 0;
    virtual std::unique_ptr<ASTNode> clone() const = 0;
};

// Ownership type specifier
enum class OwnershipType {
    None,       // (no qualifier) - only valid in template parameters
    Owned,      // ^ - owned value, requires Mem::move to transfer
    Reference,  // & - borrow/reference
    Copy        // % - explicit copy (only in parameters/returns)
};

// FFI calling conventions
enum class CallingConvention {
    Auto,       // "*" - auto-detect
    CDecl,      // "cdecl" - C calling convention
    StdCall,    // "stdcall" - Windows standard calling convention
    FastCall    // "fastcall" - Fast calling convention
};

// Forward declare Expression for TemplateArgument
class Expression;

// Template argument (for template instantiations) - MUST be declared before TypeRef
struct TemplateArgument {
    enum class Kind { Type, Value, Wildcard };

    Kind kind;
    std::string typeArg;                      // For type arguments (e.g., "Integer")
    std::unique_ptr<Expression> valueArg;     // For value arguments (e.g., 5+3)
    std::string valueStr;                     // String representation of value argument (e.g., "89")
    Common::SourceLocation location;

    // Constructor for type arguments (defined in AST.cpp)
    TemplateArgument(const std::string& type, const Common::SourceLocation& loc);

    // Constructor for value arguments (defined in AST.cpp)
    TemplateArgument(std::unique_ptr<Expression> value, const Common::SourceLocation& loc);

    // Constructor for value arguments with string representation (defined in AST.cpp)
    TemplateArgument(std::unique_ptr<Expression> value, const std::string& valStr, const Common::SourceLocation& loc);

    // Factory for wildcard (?) (defined in AST.cpp)
    static TemplateArgument Wildcard(const Common::SourceLocation& loc);

    // Destructor (defined in AST.cpp where Expression is complete)
    ~TemplateArgument();

    // Copy constructor (needed for AST cloning)
    TemplateArgument(const TemplateArgument& other);

    // Move constructor (defined in AST.cpp)
    TemplateArgument(TemplateArgument&& other) noexcept;

    // Assignment operators (defined in AST.cpp)
    TemplateArgument& operator=(const TemplateArgument& other);
    TemplateArgument& operator=(TemplateArgument&& other) noexcept;
};

// Type reference
class TypeRef : public ASTNode {
public:
    std::string typeName;  // Could be qualified like "RenderStar::Default::MyClass"
    std::vector<TemplateArgument> templateArgs;  // Template arguments for instantiation
    OwnershipType ownership;
    bool isTemplateParameter;  // True if this type is a template parameter (allows bare types)

    TypeRef(const std::string& name, OwnershipType own, const Common::SourceLocation& loc, bool isTemplate = false)
        : ASTNode(loc), typeName(name), ownership(own), isTemplateParameter(isTemplate) {}

    TypeRef(const std::string& name, std::vector<TemplateArgument> args,
            OwnershipType own, const Common::SourceLocation& loc, bool isTemplate = false)
        : ASTNode(loc), typeName(name), templateArgs(std::move(args)), ownership(own), isTemplateParameter(isTemplate) {}

    // Legacy constructor for backward compatibility with string args
    TypeRef(const std::string& name, const std::vector<std::string>& args,
            OwnershipType own, const Common::SourceLocation& loc, bool isTemplate = false)
        : ASTNode(loc), typeName(name), ownership(own), isTemplateParameter(isTemplate) {
        for (const auto& arg : args) {
            templateArgs.emplace_back(arg, loc);
        }
    }

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<TypeRef> cloneType() const;

    // Convert to string representation (for nested template support)
    std::string toString() const {
        std::string result = typeName;

        // Add template arguments if any
        if (!templateArgs.empty()) {
            result += "<";
            for (size_t i = 0; i < templateArgs.size(); ++i) {
                if (i > 0) result += ", ";
                if (templateArgs[i].kind == TemplateArgument::Kind::Type) {
                    result += templateArgs[i].typeArg;
                } else if (templateArgs[i].kind == TemplateArgument::Kind::Wildcard) {
                    result += "?";
                } else if (templateArgs[i].kind == TemplateArgument::Kind::Value) {
                    // Value argument - use stored string representation
                    if (!templateArgs[i].valueStr.empty()) {
                        result += templateArgs[i].valueStr;
                    } else {
                        result += "/*expr*/";
                    }
                }
            }
            result += ">";
        }

        // Add ownership suffix
        switch (ownership) {
            case OwnershipType::Owned: result += "^"; break;
            case OwnershipType::Reference: result += "&"; break;
            case OwnershipType::Copy: result += "%"; break;
            default: break;
        }

        return result;
    }
};

// Function type reference - for function reference types like F(Integer^)(name)(Integer&, String&)
class FunctionTypeRef : public TypeRef {
public:
    std::unique_ptr<TypeRef> returnType;
    std::vector<std::unique_ptr<TypeRef>> paramTypes;

    FunctionTypeRef(std::unique_ptr<TypeRef> retType,
                    std::vector<std::unique_ptr<TypeRef>> params,
                    OwnershipType own,
                    const Common::SourceLocation& loc)
        : TypeRef("__function", own, loc), returnType(std::move(retType)),
          paramTypes(std::move(params)) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<TypeRef> cloneType() const;
};

// Expressions
class Expression : public ASTNode {
public:
    Expression(const Common::SourceLocation& loc) : ASTNode(loc) {}
    std::unique_ptr<ASTNode> clone() const override = 0;
    // Convenience method that returns properly typed pointer
    virtual std::unique_ptr<Expression> cloneExpr() const = 0;
};

class IntegerLiteralExpr : public Expression {
public:
    int64_t value;

    IntegerLiteralExpr(int64_t val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class FloatLiteralExpr : public Expression {
public:
    float value;

    FloatLiteralExpr(float val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class DoubleLiteralExpr : public Expression {
public:
    double value;

    DoubleLiteralExpr(double val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class StringLiteralExpr : public Expression {
public:
    std::string value;

    StringLiteralExpr(const std::string& val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class BoolLiteralExpr : public Expression {
public:
    bool value;

    BoolLiteralExpr(bool val, const Common::SourceLocation& loc)
        : Expression(loc), value(val) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class ThisExpr : public Expression {
public:
    ThisExpr(const Common::SourceLocation& loc)
        : Expression(loc) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class IdentifierExpr : public Expression {
public:
    std::string name;

    IdentifierExpr(const std::string& n, const Common::SourceLocation& loc)
        : Expression(loc), name(n) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class ReferenceExpr : public Expression {
public:
    std::unique_ptr<Expression> expr;  // Expression being referenced (&expr)

    ReferenceExpr(std::unique_ptr<Expression> e, const Common::SourceLocation& loc)
        : Expression(loc), expr(std::move(e)) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class MemberAccessExpr : public Expression {
public:
    std::unique_ptr<Expression> object;
    std::string member;

    MemberAccessExpr(std::unique_ptr<Expression> obj, const std::string& mem,
                     const Common::SourceLocation& loc)
        : Expression(loc), object(std::move(obj)), member(mem) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class CallExpr : public Expression {
public:
    std::unique_ptr<Expression> callee;
    std::vector<std::unique_ptr<Expression>> arguments;

    CallExpr(std::unique_ptr<Expression> call, std::vector<std::unique_ptr<Expression>> args,
             const Common::SourceLocation& loc)
        : Expression(loc), callee(std::move(call)), arguments(std::move(args)) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
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
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

class TypeOfExpr : public Expression {
public:
    std::unique_ptr<TypeRef> type;  // TypeOf<T>()

    TypeOfExpr(std::unique_ptr<TypeRef> t, const Common::SourceLocation& loc)
        : Expression(loc), type(std::move(t)) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

// Lambda expression - [ Lambda [captures] Returns Type Parameters (...) { body } ]
class LambdaExpr : public Expression {
public:
    // Capture ownership modes (matches XXML ownership semantics)
    enum class CaptureMode {
        Reference,  // &var - borrow reference to variable
        Owned,      // ^var - move ownership into lambda
        Copy        // %var - copy the value
    };

    // Capture specification
    struct CaptureSpec {
        std::string varName;
        CaptureMode mode;

        CaptureSpec(const std::string& name, CaptureMode m)
            : varName(name), mode(m) {}

        // Helper methods
        bool isReference() const { return mode == CaptureMode::Reference; }
        bool isOwned() const { return mode == CaptureMode::Owned; }
        bool isCopy() const { return mode == CaptureMode::Copy; }
    };

    std::vector<CaptureSpec> captures;
    std::vector<std::unique_ptr<ParameterDecl>> parameters;
    std::unique_ptr<TypeRef> returnType;
    std::vector<std::unique_ptr<Statement>> body;
    bool isCompiletime = false;  // Compile-time lambda for Truth conditions
    std::vector<TemplateParameter> templateParams;  // Template parameters for template lambdas

    LambdaExpr(std::vector<CaptureSpec> caps,
               std::vector<std::unique_ptr<ParameterDecl>> params,
               std::unique_ptr<TypeRef> retType,
               std::vector<std::unique_ptr<Statement>> bodyStmts,
               const Common::SourceLocation& loc);
    ~LambdaExpr();

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Expression> cloneExpr() const override;
};

// Statements
class Statement : public ASTNode {
public:
    Statement(const Common::SourceLocation& loc) : ASTNode(loc) {}
    std::unique_ptr<ASTNode> clone() const override = 0;
    // Convenience method that returns properly typed pointer
    virtual std::unique_ptr<Statement> cloneStmt() const = 0;
};

class InstantiateStmt : public Statement {
public:
    std::unique_ptr<TypeRef> type;
    std::string variableName;
    std::unique_ptr<Expression> initializer;
    std::vector<std::unique_ptr<AnnotationUsage>> annotations;  // Applied annotations
    bool isCompiletime = false;  // Compile-time variable instantiation

    InstantiateStmt(std::unique_ptr<TypeRef> t, const std::string& name,
                    std::unique_ptr<Expression> init, const Common::SourceLocation& loc);
    ~InstantiateStmt();

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

class RunStmt : public Statement {
public:
    std::unique_ptr<Expression> expression;

    RunStmt(std::unique_ptr<Expression> expr, const Common::SourceLocation& loc)
        : Statement(loc), expression(std::move(expr)) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

class ForStmt : public Statement {
public:
    // Range-based for loop fields (For x = 0 .. 10)
    std::unique_ptr<TypeRef> iteratorType;
    std::string iteratorName;
    std::unique_ptr<Expression> rangeStart;
    std::unique_ptr<Expression> rangeEnd;

    // C-style for loop fields (For (Type <name> = init; condition; increment))
    std::unique_ptr<Expression> condition;
    std::unique_ptr<Expression> increment;
    bool isCStyleLoop;

    std::vector<std::unique_ptr<Statement>> body;

    // Range-based constructor
    ForStmt(std::unique_ptr<TypeRef> type, const std::string& name,
            std::unique_ptr<Expression> start, std::unique_ptr<Expression> end,
            std::vector<std::unique_ptr<Statement>> bodyStmts, const Common::SourceLocation& loc)
        : Statement(loc), iteratorType(std::move(type)), iteratorName(name),
          rangeStart(std::move(start)), rangeEnd(std::move(end)),
          isCStyleLoop(false), body(std::move(bodyStmts)) {}

    // C-style constructor
    ForStmt(std::unique_ptr<TypeRef> type, const std::string& name,
            std::unique_ptr<Expression> init, std::unique_ptr<Expression> cond,
            std::unique_ptr<Expression> incr,
            std::vector<std::unique_ptr<Statement>> bodyStmts, const Common::SourceLocation& loc)
        : Statement(loc), iteratorType(std::move(type)), iteratorName(name),
          rangeStart(std::move(init)), condition(std::move(cond)), increment(std::move(incr)),
          isCStyleLoop(true), body(std::move(bodyStmts)) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

class ExitStmt : public Statement {
public:
    std::unique_ptr<Expression> exitCode;

    ExitStmt(std::unique_ptr<Expression> code, const Common::SourceLocation& loc)
        : Statement(loc), exitCode(std::move(code)) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

class ReturnStmt : public Statement {
public:
    std::unique_ptr<Expression> value;

    ReturnStmt(std::unique_ptr<Expression> val, const Common::SourceLocation& loc)
        : Statement(loc), value(std::move(val)) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
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

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
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

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

class BreakStmt : public Statement {
public:
    BreakStmt(const Common::SourceLocation& loc)
        : Statement(loc) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

class ContinueStmt : public Statement {
public:
    ContinueStmt(const Common::SourceLocation& loc)
        : Statement(loc) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

class AssignmentStmt : public Statement {
public:
    std::unique_ptr<Expression> target;  // The lvalue (can be IdentifierExpr, MemberAccessExpr, ThisExpr, etc.)
    std::unique_ptr<Expression> value;

    AssignmentStmt(std::unique_ptr<Expression> tgt, std::unique_ptr<Expression> val, const Common::SourceLocation& loc)
        : Statement(loc), target(std::move(tgt)), value(std::move(val)) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Statement> cloneStmt() const override;
};

// Declarations
class Declaration : public ASTNode {
public:
    Declaration(const Common::SourceLocation& loc) : ASTNode(loc) {}
    std::unique_ptr<ASTNode> clone() const override = 0;
    // Convenience method that returns properly typed pointer
    virtual std::unique_ptr<Declaration> cloneDecl() const = 0;
};

// Constraint reference with optional template arguments (e.g., Hashable<T> or Hashable@T)
struct ConstraintRef {
    std::string name;                          // "Hashable"
    std::vector<std::string> templateArgs;     // ["T"] for Hashable<T>
    Common::SourceLocation location;

    ConstraintRef(const std::string& n, const std::vector<std::string>& args,
                  const Common::SourceLocation& loc)
        : name(n), templateArgs(args), location(loc) {}

    // For backward compatibility - simple constraint name
    ConstraintRef(const std::string& n, const Common::SourceLocation& loc)
        : name(n), templateArgs(), location(loc) {}

    // Default constructor for containers
    ConstraintRef() : name(), templateArgs(), location() {}

    // Convert to string representation for existing code
    std::string toString() const {
        if (templateArgs.empty()) return name;
        std::string result = name + "<";
        for (size_t i = 0; i < templateArgs.size(); ++i) {
            if (i > 0) result += ", ";
            result += templateArgs[i];
        }
        return result + ">";
    }
};

// Template parameter (for template class and method definitions)
struct TemplateParameter {
    enum class Kind { Type, Value };  // Type parameter (T) or non-type parameter (N)

    std::string name;
    Kind kind;
    std::vector<ConstraintRef> constraints;  // Empty means no constraints (any type)
    bool constraintsAreAnd = true;           // true = AND semantics (parenthesized), false = OR (pipe)
    std::string valueType;  // For non-type parameters: the type (e.g., "int64")
    Common::SourceLocation location;

    // New constructor with ConstraintRef
    TemplateParameter(const std::string& n, const std::vector<ConstraintRef>& c, bool andSemantics,
                     const Common::SourceLocation& loc,
                     Kind k = Kind::Type, const std::string& vType = "")
        : name(n), kind(k), constraints(c), constraintsAreAnd(andSemantics), valueType(vType), location(loc) {}

    // Backward compatibility constructor - converts strings to ConstraintRefs
    TemplateParameter(const std::string& n, const std::vector<std::string>& c,
                     const Common::SourceLocation& loc,
                     Kind k = Kind::Type, const std::string& vType = "")
        : name(n), kind(k), constraintsAreAnd(c.size() <= 1), valueType(vType), location(loc) {
        for (const auto& s : c) {
            constraints.emplace_back(s, loc);
        }
    }
};

class ParameterDecl : public Declaration {
public:
    std::string name;
    std::unique_ptr<TypeRef> type;

    ParameterDecl(const std::string& n, std::unique_ptr<TypeRef> t, const Common::SourceLocation& loc)
        : Declaration(loc), name(n), type(std::move(t)) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
};

class PropertyDecl : public Declaration {
public:
    std::string name;
    std::unique_ptr<TypeRef> type;
    std::vector<std::unique_ptr<AnnotationUsage>> annotations;  // Applied annotations
    bool isCompiletime = false;  // Compile-time property

    PropertyDecl(const std::string& n, std::unique_ptr<TypeRef> t, const Common::SourceLocation& loc);
    ~PropertyDecl();

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
};

class ConstructorDecl : public Declaration {
public:
    bool isDefault;
    std::vector<std::unique_ptr<ParameterDecl>> parameters;
    std::vector<std::unique_ptr<Statement>> body;
    bool isCompiletime = false;  // Compile-time constructor

    ConstructorDecl(bool isDef, std::vector<std::unique_ptr<ParameterDecl>> params,
                    std::vector<std::unique_ptr<Statement>> bodyStmts,
                    const Common::SourceLocation& loc);
    ~ConstructorDecl();

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
};

class DestructorDecl : public Declaration {
public:
    std::vector<std::unique_ptr<Statement>> body;

    DestructorDecl(std::vector<std::unique_ptr<Statement>> bodyStmts,
                   const Common::SourceLocation& loc);
    ~DestructorDecl();

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

class MethodDecl : public Declaration {
public:
    std::string name;
    std::vector<TemplateParameter> templateParams;  // Template parameters if this is a template method
    std::unique_ptr<TypeRef> returnType;
    std::vector<std::unique_ptr<ParameterDecl>> parameters;
    std::vector<std::unique_ptr<Statement>> body;
    std::vector<std::unique_ptr<AnnotationUsage>> annotations;  // Applied annotations
    bool isCompiletime = false;  // Compile-time method

    // FFI fields (for @NativeFunction annotated methods)
    bool isNative = false;              // True if this is a native FFI method (semicolon-terminated)
    std::string nativePath;             // DLL path from @NativeFunction
    std::string nativeSymbol;           // Symbol name from @NativeFunction
    CallingConvention callingConvention = CallingConvention::Auto;

    MethodDecl(const std::string& n, std::unique_ptr<TypeRef> retType,
               std::vector<std::unique_ptr<ParameterDecl>> params,
               std::vector<std::unique_ptr<Statement>> bodyStmts,
               const Common::SourceLocation& loc);

    MethodDecl(const std::string& n, const std::vector<TemplateParameter>& tparams,
               std::unique_ptr<TypeRef> retType,
               std::vector<std::unique_ptr<ParameterDecl>> params,
               std::vector<std::unique_ptr<Statement>> bodyStmts,
               const Common::SourceLocation& loc);
    ~MethodDecl();

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
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

    AccessSection(AccessModifier mod, const Common::SourceLocation& loc);
    ~AccessSection();

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
};

class ClassDecl : public Declaration {
public:
    std::string name;
    std::vector<TemplateParameter> templateParams;  // Template parameters if this is a template class
    bool isFinal;
    std::string baseClass;  // Empty string if no base class (Extends None)
    std::vector<std::unique_ptr<AccessSection>> sections;
    std::vector<std::unique_ptr<AnnotationUsage>> annotations;  // Applied annotations
    bool isCompiletime = false;  // Compile-time class

    ClassDecl(const std::string& n, const std::vector<TemplateParameter>& tparams,
              bool final, const std::string& base,
              const Common::SourceLocation& loc);
    ~ClassDecl();

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Structure declaration - stack-allocated value type with methods and constructors
// Unlike Class, Structure types are allocated on the stack, not heap
// They don't support inheritance but can have methods, constructors, and properties
class StructureDecl : public Declaration {
public:
    std::string name;
    std::vector<TemplateParameter> templateParams;  // Template parameters if this is a template structure
    std::vector<std::unique_ptr<AccessSection>> sections;
    std::vector<std::unique_ptr<AnnotationUsage>> annotations;  // Applied annotations
    bool isCompiletime = false;  // Compile-time structure

    StructureDecl(const std::string& n, const std::vector<TemplateParameter>& tparams,
                  const Common::SourceLocation& loc);
    ~StructureDecl();

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Native structure for FFI (C-compatible struct with fixed alignment)
class NativeStructureDecl : public Declaration {
public:
    std::string name;
    std::vector<std::unique_ptr<PropertyDecl>> properties;  // Only NativeType properties allowed
    size_t alignment = 8;  // Default alignment in bytes

    NativeStructureDecl(const std::string& n, size_t align, const Common::SourceLocation& loc);
    ~NativeStructureDecl();

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Callback type declaration for FFI callbacks
// [ CallbackType <Name> Convention(cdecl) Returns Type Parameters (...) ]
class CallbackTypeDecl : public Declaration {
public:
    std::string name;
    CallingConvention convention;
    std::unique_ptr<TypeRef> returnType;
    std::vector<std::unique_ptr<ParameterDecl>> parameters;

    CallbackTypeDecl(const std::string& n, CallingConvention conv,
                     std::unique_ptr<TypeRef> retType,
                     std::vector<std::unique_ptr<ParameterDecl>> params,
                     const Common::SourceLocation& loc);
    ~CallbackTypeDecl();

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Enumeration value declaration (Value <NAME> = value;)
class EnumValueDecl : public Declaration {
public:
    std::string name;
    bool hasExplicitValue;
    int64_t value;

    EnumValueDecl(const std::string& n, bool hasValue, int64_t val, const Common::SourceLocation& loc)
        : Declaration(loc), name(n), hasExplicitValue(hasValue), value(val) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Enumeration declaration ([ Enumeration <Name> ... ])
class EnumerationDecl : public Declaration {
public:
    std::string name;
    std::vector<std::unique_ptr<EnumValueDecl>> values;

    EnumerationDecl(const std::string& n, const Common::SourceLocation& loc)
        : Declaration(loc), name(n) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

class NamespaceDecl : public Declaration {
public:
    std::string name;  // Could be qualified like "RenderStar::Default"
    std::vector<std::unique_ptr<Declaration>> declarations;

    NamespaceDecl(const std::string& n, const Common::SourceLocation& loc)
        : Declaration(loc), name(n) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
};

class ImportDecl : public Declaration {
public:
    std::string modulePath;  // e.g., "Language::Core"

    ImportDecl(const std::string& path, const Common::SourceLocation& loc)
        : Declaration(loc), modulePath(path) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
};

class EntrypointDecl : public Declaration {
public:
    std::vector<std::unique_ptr<Statement>> body;

    EntrypointDecl(std::vector<std::unique_ptr<Statement>> bodyStmts,
                   const Common::SourceLocation& loc)
        : Declaration(loc), body(std::move(bodyStmts)) {}

    void accept(ASTVisitor& visitor) override;    std::unique_ptr<ASTNode> clone() const override;    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Constraint requirement types
enum class RequirementKind {
    Method,                 // Method requirement: F(Integer^)(run)(*) On a
    Constructor,            // Constructor requirement: C(None) On a
    Truth,                  // Truth assertion: Truth(expr)
    CompiletimeMethod,      // Compile-time method requirement: FC(Integer^)(run)(*) On a
    CompiletimeConstructor  // Compile-time constructor requirement: CC(None) On a
};

class RequireStmt : public Statement {
public:
    RequirementKind kind;

    // For Method requirements: F(ReturnType)(methodName)(*) On targetParam
    std::unique_ptr<TypeRef> methodReturnType;     // Return type (e.g., Integer^)
    std::string methodName;                        // Method name (e.g., "run")
    std::string targetParam;                       // Target parameter name (e.g., "a")

    // For Constructor requirements: C(ParamType1, ParamType2) On targetParam
    std::vector<std::unique_ptr<TypeRef>> constructorParamTypes;  // Parameter types

    // For Truth requirements: Truth(expression)
    std::unique_ptr<Expression> truthCondition;    // TypeOf<T>() == TypeOf<SomeClass>()

    Common::SourceLocation location;

    RequireStmt(RequirementKind k, const Common::SourceLocation& loc)
        : Statement(loc), kind(k), location(loc) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Statement> cloneStmt() const override;
};

// Parameter binding for constraints (e.g., (T a) means "a" binds to template param T)
struct ConstraintParamBinding {
    std::string templateParamName;  // e.g., "T"
    std::string bindingName;        // e.g., "a"
    Common::SourceLocation location;

    ConstraintParamBinding(const std::string& tpn, const std::string& bn,
                          const Common::SourceLocation& loc)
        : templateParamName(tpn), bindingName(bn), location(loc) {}
};

class ConstraintDecl : public Declaration {
public:
    std::string name;                                      // "MyConstraint"
    std::vector<TemplateParameter> templateParams;         // <T Constrains None>
    std::vector<ConstraintParamBinding> paramBindings;     // (T a)
    std::vector<std::unique_ptr<RequireStmt>> requirements; // List of Require statements

    ConstraintDecl(const std::string& n,
                   const std::vector<TemplateParameter>& tparams,
                   const std::vector<ConstraintParamBinding>& bindings,
                   const Common::SourceLocation& loc)
        : Declaration(loc), name(n), templateParams(tparams), paramBindings(bindings) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Annotation target types
enum class AnnotationTarget {
    Properties,
    Variables,
    Classes,
    Methods
};

// Annotation parameter definition (Annotate statement inside annotation definition)
class AnnotateDecl : public Declaration {
public:
    std::string name;
    std::unique_ptr<TypeRef> type;
    std::unique_ptr<Expression> defaultValue;  // nullable - optional default

    AnnotateDecl(const std::string& n, std::unique_ptr<TypeRef> t,
                 std::unique_ptr<Expression> def, const Common::SourceLocation& loc)
        : Declaration(loc), name(n), type(std::move(t)), defaultValue(std::move(def)) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Processor class definition (nested inside annotation)
class ProcessorDecl : public Declaration {
public:
    std::vector<std::unique_ptr<AccessSection>> sections;

    ProcessorDecl(std::vector<std::unique_ptr<AccessSection>> s, const Common::SourceLocation& loc)
        : Declaration(loc), sections(std::move(s)) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Annotation definition
class AnnotationDecl : public Declaration {
public:
    std::string name;
    std::vector<AnnotationTarget> allowedTargets;
    std::vector<std::unique_ptr<AnnotateDecl>> parameters;
    std::unique_ptr<ProcessorDecl> processor;  // nullable
    bool retainAtRuntime;

    AnnotationDecl(const std::string& n, std::vector<AnnotationTarget> targets,
                   std::vector<std::unique_ptr<AnnotateDecl>> params,
                   std::unique_ptr<ProcessorDecl> proc, bool retain,
                   const Common::SourceLocation& loc)
        : Declaration(loc), name(n), allowedTargets(std::move(targets)),
          parameters(std::move(params)), processor(std::move(proc)), retainAtRuntime(retain) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
    std::unique_ptr<Declaration> cloneDecl() const override;
};

// Annotation usage (@Name(...))
class AnnotationUsage : public ASTNode {
public:
    std::string annotationName;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> arguments;

    AnnotationUsage(const std::string& name,
                    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> args,
                    const Common::SourceLocation& loc)
        : ASTNode(loc), annotationName(name), arguments(std::move(args)) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
};

// Root node
class Program : public ASTNode {
public:
    std::vector<std::unique_ptr<Declaration>> declarations;

    Program(const Common::SourceLocation& loc) : ASTNode(loc) {}

    void accept(ASTVisitor& visitor) override;
    std::unique_ptr<ASTNode> clone() const override;
};

// Visitor interface for AST traversal
class ASTVisitor {
public:
    virtual ~ASTVisitor() = default;

    virtual void visit(Program& node) = 0;
    virtual void visit(ImportDecl& node) = 0;
    virtual void visit(NamespaceDecl& node) = 0;
    virtual void visit(ClassDecl& node) = 0;
    virtual void visit(StructureDecl& node) = 0;
    virtual void visit(NativeStructureDecl& node) = 0;
    virtual void visit(CallbackTypeDecl& node) = 0;
    virtual void visit(EnumValueDecl& node) = 0;
    virtual void visit(EnumerationDecl& node) = 0;
    virtual void visit(AccessSection& node) = 0;
    virtual void visit(PropertyDecl& node) = 0;
    virtual void visit(ConstructorDecl& node) = 0;
    virtual void visit(DestructorDecl& node) = 0;
    virtual void visit(MethodDecl& node) = 0;
    virtual void visit(ParameterDecl& node) = 0;
    virtual void visit(EntrypointDecl& node) = 0;
    virtual void visit(ConstraintDecl& node) = 0;
    virtual void visit(AnnotateDecl& node) = 0;
    virtual void visit(ProcessorDecl& node) = 0;
    virtual void visit(AnnotationDecl& node) = 0;
    virtual void visit(AnnotationUsage& node) = 0;

    virtual void visit(InstantiateStmt& node) = 0;
    virtual void visit(RequireStmt& node) = 0;
    virtual void visit(RunStmt& node) = 0;
    virtual void visit(ForStmt& node) = 0;
    virtual void visit(ExitStmt& node) = 0;
    virtual void visit(ReturnStmt& node) = 0;
    virtual void visit(IfStmt& node) = 0;
    virtual void visit(WhileStmt& node) = 0;
    virtual void visit(BreakStmt& node) = 0;
    virtual void visit(ContinueStmt& node) = 0;
    virtual void visit(AssignmentStmt& node) = 0;

    virtual void visit(IntegerLiteralExpr& node) = 0;
    virtual void visit(FloatLiteralExpr& node) = 0;
    virtual void visit(DoubleLiteralExpr& node) = 0;
    virtual void visit(StringLiteralExpr& node) = 0;
    virtual void visit(BoolLiteralExpr& node) = 0;
    virtual void visit(ThisExpr& node) = 0;
    virtual void visit(IdentifierExpr& node) = 0;
    virtual void visit(ReferenceExpr& node) = 0;
    virtual void visit(MemberAccessExpr& node) = 0;
    virtual void visit(CallExpr& node) = 0;
    virtual void visit(BinaryExpr& node) = 0;
    virtual void visit(TypeOfExpr& node) = 0;
    virtual void visit(LambdaExpr& node) = 0;

    virtual void visit(TypeRef& node) = 0;
    virtual void visit(FunctionTypeRef& node) = 0;
};

} // namespace Parser
} // namespace XXML
