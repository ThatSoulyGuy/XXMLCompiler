#pragma once
#include <memory>
#include <vector>
#include "AST.h"
#include "../Lexer/Token.h"
#include "../Common/Error.h"

namespace XXML {
namespace Parser {

class Parser {
private:
    std::vector<Lexer::Token> tokens;
    size_t current;
    Common::ErrorReporter& errorReporter;

    // Token navigation
    const Lexer::Token& peek() const;
    const Lexer::Token& previous() const;
    const Lexer::Token& advance();
    bool isAtEnd() const;
    bool check(Lexer::TokenType type) const;
    bool match(Lexer::TokenType type);
    bool matchAny(std::initializer_list<Lexer::TokenType> types);

    // Error handling
    const Lexer::Token& consume(Lexer::TokenType type, const std::string& message);
    void synchronize();
    void error(const std::string& message);

    // Parsing methods
    std::unique_ptr<Program> parseProgram();
    std::unique_ptr<Declaration> parseDeclaration();
    std::unique_ptr<ImportDecl> parseImport();
    std::unique_ptr<NamespaceDecl> parseNamespace();
    std::unique_ptr<ClassDecl> parseClass();
    std::unique_ptr<NativeStructureDecl> parseNativeStructure();
    std::unique_ptr<CallbackTypeDecl> parseCallbackType();
    std::unique_ptr<EnumerationDecl> parseEnumeration();
    std::unique_ptr<EntrypointDecl> parseEntrypoint();
    std::unique_ptr<ConstraintDecl> parseConstraint();
    std::unique_ptr<AnnotationDecl> parseAnnotationDecl();
    std::unique_ptr<AnnotateDecl> parseAnnotateDecl();
    std::unique_ptr<ProcessorDecl> parseProcessorDecl();
    std::unique_ptr<AnnotationUsage> parseAnnotationUsage();
    std::vector<std::unique_ptr<AnnotationUsage>> parseAnnotationUsages();
    std::vector<AnnotationTarget> parseAnnotationAllows();
    std::unique_ptr<AccessSection> parseAccessSection();
    std::unique_ptr<PropertyDecl> parseProperty();
    std::unique_ptr<ConstructorDecl> parseConstructor();
    std::unique_ptr<DestructorDecl> parseDestructor();
    std::unique_ptr<MethodDecl> parseMethod();
    std::unique_ptr<ParameterDecl> parseParameter();

    std::unique_ptr<Statement> parseStatement();
    std::unique_ptr<RequireStmt> parseRequireStatement();
    std::unique_ptr<InstantiateStmt> parseInstantiate();
    std::unique_ptr<AssignmentStmt> parseAssignment();
    std::unique_ptr<RunStmt> parseRun();
    std::unique_ptr<ForStmt> parseFor();
    std::unique_ptr<ExitStmt> parseExit();
    std::unique_ptr<ReturnStmt> parseReturn();
    std::unique_ptr<IfStmt> parseIf();
    std::unique_ptr<WhileStmt> parseWhile();
    std::unique_ptr<BreakStmt> parseBreak();
    std::unique_ptr<ContinueStmt> parseContinue();
    std::vector<std::unique_ptr<Statement>> parseBlock();

    std::unique_ptr<Expression> parseExpression();
    std::unique_ptr<Expression> parseLogicalOr();
    std::unique_ptr<Expression> parseLogicalAnd();
    std::unique_ptr<Expression> parseEquality();
    std::unique_ptr<Expression> parseComparison();
    std::unique_ptr<Expression> parseAddition();
    std::unique_ptr<Expression> parseMultiplication();
    std::unique_ptr<Expression> parseUnary();
    std::unique_ptr<Expression> parsePostfix();
    std::unique_ptr<Expression> parsePrimary();
    std::unique_ptr<Expression> parseCallOrMemberAccess(std::unique_ptr<Expression> expr);
    std::unique_ptr<Expression> parseTypeOfExpression();
    std::unique_ptr<Expression> parseLambda();

    std::unique_ptr<TypeRef> parseTypeRef();
    std::unique_ptr<TypeRef> parseFunctionTypeRef();
    OwnershipType parseOwnershipType();
    std::vector<TemplateParameter> parseTemplateParameters();
    std::string parseQualifiedIdentifier();
    std::string parseAngleBracketIdentifier();

public:
    Parser(const std::vector<Lexer::Token>& toks, Common::ErrorReporter& reporter);

    std::unique_ptr<Program> parse();
};

} // namespace Parser
} // namespace XXML
