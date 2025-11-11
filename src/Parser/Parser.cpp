#include "../../include/Parser/Parser.h"
#include <sstream>
#include <iostream>

namespace XXML {
namespace Parser {

Parser::Parser(const std::vector<Lexer::Token>& toks, Common::ErrorReporter& reporter)
    : tokens(toks), current(0), errorReporter(reporter) {}

// Token navigation
const Lexer::Token& Parser::peek() const {
    return tokens[current];
}

const Lexer::Token& Parser::previous() const {
    return tokens[current - 1];
}

const Lexer::Token& Parser::advance() {
    if (!isAtEnd()) current++;
    return previous();
}

bool Parser::isAtEnd() const {
    return peek().type == Lexer::TokenType::EndOfFile;
}

bool Parser::check(Lexer::TokenType type) const {
    if (isAtEnd()) return false;
    return peek().type == type;
}

bool Parser::match(Lexer::TokenType type) {
    if (check(type)) {
        advance();
        return true;
    }
    return false;
}

bool Parser::matchAny(std::initializer_list<Lexer::TokenType> types) {
    for (auto type : types) {
        if (match(type)) return true;
    }
    return false;
}

const Lexer::Token& Parser::consume(Lexer::TokenType type, const std::string& message) {
    if (check(type)) return advance();

    error(message + " (expected " + Lexer::tokenTypeToString(type) +
          ", got " + Lexer::tokenTypeToString(peek().type) + ")");
    return peek();
}

void Parser::error(const std::string& message) {
    errorReporter.reportError(
        Common::ErrorCode::UnexpectedToken,
        message,
        peek().location
    );
}

void Parser::synchronize() {
    advance();

    while (!isAtEnd()) {
        if (previous().type == Lexer::TokenType::Semicolon) return;

        switch (peek().type) {
            case Lexer::TokenType::Import:
            case Lexer::TokenType::Namespace:
            case Lexer::TokenType::Class:
            case Lexer::TokenType::Public:
            case Lexer::TokenType::Private:
            case Lexer::TokenType::Property:
            case Lexer::TokenType::Method:
            case Lexer::TokenType::Constructor:
            case Lexer::TokenType::Entrypoint:
                return;
            default:
                advance();
        }
    }
}

// Main parse entry point
std::unique_ptr<Program> Parser::parse() {
    return parseProgram();
}

std::unique_ptr<Program> Parser::parseProgram() {
    auto program = std::make_unique<Program>(peek().location);

    while (!isAtEnd()) {
        try {
            auto decl = parseDeclaration();
            if (decl) {
                program->declarations.push_back(std::move(decl));
            }
        } catch (...) {
            synchronize();
        }
    }

    return program;
}

std::unique_ptr<Declaration> Parser::parseDeclaration() {
    if (match(Lexer::TokenType::Hash)) {
        if (match(Lexer::TokenType::Import)) {
            return parseImport();
        }
    }

    if (match(Lexer::TokenType::LeftBracket)) {
        if (check(Lexer::TokenType::Namespace)) {
            return parseNamespace();
        } else if (check(Lexer::TokenType::Class)) {
            return parseClass();
        } else if (check(Lexer::TokenType::Entrypoint)) {
            return parseEntrypoint();
        } else if (check(Lexer::TokenType::Public) || check(Lexer::TokenType::Private) ||
                   check(Lexer::TokenType::Protected)) {
            error("Access sections must be inside a class");
            synchronize();
            return nullptr;
        } else {
            error("Expected declaration after '['");
            synchronize();
            return nullptr;
        }
    }

    error("Expected declaration");
    synchronize();
    return nullptr;
}

std::unique_ptr<ImportDecl> Parser::parseImport() {
    auto loc = previous().location;
    std::string modulePath = parseQualifiedIdentifier();
    consume(Lexer::TokenType::Semicolon, "Expected ';' after import");
    return std::make_unique<ImportDecl>(modulePath, loc);
}

std::unique_ptr<NamespaceDecl> Parser::parseNamespace() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Namespace, "Expected 'Namespace'");

    std::string name = parseAngleBracketIdentifier();

    auto namespaceDecl = std::make_unique<NamespaceDecl>(name, loc);

    // Parse nested declarations (classes, nested namespaces, etc.)
    while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
        auto decl = parseDeclaration();
        if (decl) {
            namespaceDecl->declarations.push_back(std::move(decl));
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after namespace");

    return namespaceDecl;
}

std::unique_ptr<ClassDecl> Parser::parseClass() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Class, "Expected 'Class'");

    std::string className = parseAngleBracketIdentifier();

    // Parse template parameters if present
    std::vector<TemplateParameter> templateParams;
    if (check(Lexer::TokenType::LeftAngle)) {
        templateParams = parseTemplateParameters();
    }

    bool isFinal = match(Lexer::TokenType::Final);

    std::string baseClass;
    if (match(Lexer::TokenType::Extends)) {
        if (match(Lexer::TokenType::None)) {
            baseClass = "";
        } else {
            baseClass = parseQualifiedIdentifier();
        }
    }

    auto classDecl = std::make_unique<ClassDecl>(className, templateParams, isFinal, baseClass, loc);

    // Parse access sections
    while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
        if (check(Lexer::TokenType::LeftBracket)) {
            auto accessSection = parseAccessSection();
            if (accessSection) {
                classDecl->sections.push_back(std::move(accessSection));
            }
        } else {
            error("Expected access section in class");
            synchronize();
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after class");

    return classDecl;
}

std::unique_ptr<AccessSection> Parser::parseAccessSection() {
    consume(Lexer::TokenType::LeftBracket, "Expected '['");

    auto loc = peek().location;
    AccessModifier modifier;

    if (match(Lexer::TokenType::Public)) {
        modifier = AccessModifier::Public;
    } else if (match(Lexer::TokenType::Private)) {
        modifier = AccessModifier::Private;
    } else if (match(Lexer::TokenType::Protected)) {
        modifier = AccessModifier::Protected;
    } else {
        error("Expected access modifier (Public, Private, or Protected)");
        return nullptr;
    }

    // Consume the empty angle brackets <>
    consume(Lexer::TokenType::LeftAngle, "Expected '<' after access modifier");
    consume(Lexer::TokenType::RightAngle, "Expected '>' after '<'");

    auto section = std::make_unique<AccessSection>(modifier, loc);

    // Parse declarations within the access section
    while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
        if (check(Lexer::TokenType::Property)) {
            section->declarations.push_back(parseProperty());
        } else if (check(Lexer::TokenType::Constructor)) {
            section->declarations.push_back(parseConstructor());
        } else if (check(Lexer::TokenType::Method)) {
            section->declarations.push_back(parseMethod());
        } else {
            error("Expected Property, Constructor, or Method in access section");
            synchronize();
            break;
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after access section");

    return section;
}

std::unique_ptr<PropertyDecl> Parser::parseProperty() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Property, "Expected 'Property'");

    std::string propertyName = parseAngleBracketIdentifier();

    consume(Lexer::TokenType::Types, "Expected 'Types' after property name");

    auto type = parseTypeRef();

    consume(Lexer::TokenType::Semicolon, "Expected ';' after property declaration");

    return std::make_unique<PropertyDecl>(propertyName, std::move(type), loc);
}

std::unique_ptr<ConstructorDecl> Parser::parseConstructor() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Constructor, "Expected 'Constructor'");

    bool isDefault = false;
    std::vector<std::unique_ptr<ParameterDecl>> params;
    std::vector<std::unique_ptr<Statement>> body;

    if (match(Lexer::TokenType::Equals)) {
        consume(Lexer::TokenType::Default, "Expected 'default' after '='");
        isDefault = true;
        consume(Lexer::TokenType::Semicolon, "Expected ';' after default constructor");
    } else {
        // TODO: Handle explicit constructors with parameters and body
        consume(Lexer::TokenType::Semicolon, "Expected ';' after constructor");
    }

    return std::make_unique<ConstructorDecl>(isDefault, std::move(params), std::move(body), loc);
}

std::unique_ptr<MethodDecl> Parser::parseMethod() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Method, "Expected 'Method'");

    std::string methodName = parseAngleBracketIdentifier();

    consume(Lexer::TokenType::Returns, "Expected 'Returns' after method name");

    auto returnType = parseTypeRef();

    std::vector<std::unique_ptr<ParameterDecl>> params;
    if (match(Lexer::TokenType::Parameters)) {
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Parameters'");

        if (!check(Lexer::TokenType::RightParen)) {
            do {
                params.push_back(parseParameter());
            } while (match(Lexer::TokenType::Comma));
        }

        consume(Lexer::TokenType::RightParen, "Expected ')' after parameters");
    }

    // Accept either '->' or 'Do' before method body
    if (!match(Lexer::TokenType::Arrow) && !match(Lexer::TokenType::Do)) {
        error("Expected '->' or 'Do' before method body");
    }

    auto body = parseBlock();

    return std::make_unique<MethodDecl>(methodName, std::move(returnType),
                                        std::move(params), std::move(body), loc);
}

std::unique_ptr<ParameterDecl> Parser::parseParameter() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Parameter, "Expected 'Parameter'");

    std::string paramName = parseAngleBracketIdentifier();

    consume(Lexer::TokenType::Types, "Expected 'Types' after parameter name");

    auto type = parseTypeRef();

    return std::make_unique<ParameterDecl>(paramName, std::move(type), loc);
}

std::unique_ptr<EntrypointDecl> Parser::parseEntrypoint() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Entrypoint, "Expected 'Entrypoint'");

    auto body = parseBlock();

    consume(Lexer::TokenType::RightBracket, "Expected ']' after entrypoint");

    return std::make_unique<EntrypointDecl>(std::move(body), loc);
}

std::vector<std::unique_ptr<Statement>> Parser::parseBlock() {
    consume(Lexer::TokenType::LeftBrace, "Expected '{' to start block");

    std::vector<std::unique_ptr<Statement>> statements;

    while (!check(Lexer::TokenType::RightBrace) && !isAtEnd()) {
        auto stmt = parseStatement();
        if (stmt) {
            statements.push_back(std::move(stmt));
        }
    }

    consume(Lexer::TokenType::RightBrace, "Expected '}' after block");

    return statements;
}

std::unique_ptr<Statement> Parser::parseStatement() {
    if (match(Lexer::TokenType::Instantiate)) {
        return parseInstantiate();
    } else if (match(Lexer::TokenType::Set)) {
        return parseAssignment();
    } else if (match(Lexer::TokenType::Run)) {
        return parseRun();
    } else if (match(Lexer::TokenType::For)) {
        return parseFor();
    } else if (match(Lexer::TokenType::Exit)) {
        return parseExit();
    } else if (match(Lexer::TokenType::Return)) {
        return parseReturn();
    } else if (match(Lexer::TokenType::If)) {
        return parseIf();
    } else if (match(Lexer::TokenType::While)) {
        return parseWhile();
    } else if (match(Lexer::TokenType::Break)) {
        return parseBreak();
    } else if (match(Lexer::TokenType::Continue)) {
        return parseContinue();
    } else {
        error("Expected statement");
        synchronize();
        return nullptr;
    }
}

std::unique_ptr<InstantiateStmt> Parser::parseInstantiate() {
    auto loc = previous().location;

    auto type = parseTypeRef();

    consume(Lexer::TokenType::As, "Expected 'As' after type");

    std::string varName = parseAngleBracketIdentifier();

    consume(Lexer::TokenType::Equals, "Expected '=' after variable name");

    auto initializer = parseExpression();

    consume(Lexer::TokenType::Semicolon, "Expected ';' after instantiate statement");

    return std::make_unique<InstantiateStmt>(std::move(type), varName,
                                             std::move(initializer), loc);
}

std::unique_ptr<AssignmentStmt> Parser::parseAssignment() {
    auto loc = previous().location;

    // Parse variable name (can be identifier or angle bracket id)
    std::string varName;
    if (check(Lexer::TokenType::AngleBracketId)) {
        varName = parseAngleBracketIdentifier();
    } else if (check(Lexer::TokenType::Identifier)) {
        varName = advance().lexeme;
    } else {
        error("Expected variable name after 'Set'");
        return nullptr;
    }

    consume(Lexer::TokenType::Equals, "Expected '=' after variable name");

    auto value = parseExpression();

    consume(Lexer::TokenType::Semicolon, "Expected ';' after assignment");

    return std::make_unique<AssignmentStmt>(varName, std::move(value), loc);
}

std::unique_ptr<RunStmt> Parser::parseRun() {
    auto loc = previous().location;

    auto expr = parseExpression();

    consume(Lexer::TokenType::Semicolon, "Expected ';' after run statement");

    return std::make_unique<RunStmt>(std::move(expr), loc);
}

std::unique_ptr<ForStmt> Parser::parseFor() {
    auto loc = previous().location;

    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'For'");

    auto iteratorType = parseTypeRef();

    std::string iteratorName = parseAngleBracketIdentifier();

    consume(Lexer::TokenType::Equals, "Expected '=' after iterator name");

    auto rangeStart = parseExpression();

    consume(Lexer::TokenType::Range, "Expected '..' in for loop range");

    auto rangeEnd = parseExpression();

    consume(Lexer::TokenType::RightParen, "Expected ')' after for loop range");

    consume(Lexer::TokenType::Arrow, "Expected '->' before for loop body");

    auto body = parseBlock();

    return std::make_unique<ForStmt>(std::move(iteratorType), iteratorName,
                                     std::move(rangeStart), std::move(rangeEnd),
                                     std::move(body), loc);
}

std::unique_ptr<ExitStmt> Parser::parseExit() {
    auto loc = previous().location;

    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Exit'");

    auto exitCode = parseExpression();

    consume(Lexer::TokenType::RightParen, "Expected ')' after exit code");

    consume(Lexer::TokenType::Semicolon, "Expected ';' after exit statement");

    return std::make_unique<ExitStmt>(std::move(exitCode), loc);
}

std::unique_ptr<ReturnStmt> Parser::parseReturn() {
    auto loc = previous().location;

    std::unique_ptr<Expression> value = nullptr;

    if (!check(Lexer::TokenType::Semicolon)) {
        value = parseExpression();
    }

    consume(Lexer::TokenType::Semicolon, "Expected ';' after return statement");

    return std::make_unique<ReturnStmt>(std::move(value), loc);
}

std::unique_ptr<IfStmt> Parser::parseIf() {
    auto loc = previous().location;

    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'If'");

    auto condition = parseExpression();

    consume(Lexer::TokenType::RightParen, "Expected ')' after condition");

    consume(Lexer::TokenType::Arrow, "Expected '->' before if body");

    auto thenBranch = parseBlock();

    std::vector<std::unique_ptr<Statement>> elseBranch;
    if (match(Lexer::TokenType::Else)) {
        consume(Lexer::TokenType::Arrow, "Expected '->' before else body");
        elseBranch = parseBlock();
    }

    return std::make_unique<IfStmt>(std::move(condition), std::move(thenBranch),
                                    std::move(elseBranch), loc);
}

std::unique_ptr<WhileStmt> Parser::parseWhile() {
    auto loc = previous().location;

    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'While'");

    auto condition = parseExpression();

    consume(Lexer::TokenType::RightParen, "Expected ')' after condition");

    consume(Lexer::TokenType::Arrow, "Expected '->' before while body");

    auto body = parseBlock();

    return std::make_unique<WhileStmt>(std::move(condition), std::move(body), loc);
}

std::unique_ptr<BreakStmt> Parser::parseBreak() {
    auto loc = previous().location;

    consume(Lexer::TokenType::Semicolon, "Expected ';' after break statement");

    return std::make_unique<BreakStmt>(loc);
}

std::unique_ptr<ContinueStmt> Parser::parseContinue() {
    auto loc = previous().location;

    consume(Lexer::TokenType::Semicolon, "Expected ';' after continue statement");

    return std::make_unique<ContinueStmt>(loc);
}

// Expression parsing with precedence climbing
std::unique_ptr<Expression> Parser::parseExpression() {
    return parseLogicalOr();
}

std::unique_ptr<Expression> Parser::parseLogicalOr() {
    auto expr = parseLogicalAnd();

    while (match(Lexer::TokenType::LogicalOr)) {
        auto op = previous().lexeme;
        auto right = parseLogicalAnd();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), previous().location);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseLogicalAnd() {
    auto expr = parseEquality();

    while (match(Lexer::TokenType::LogicalAnd)) {
        auto op = previous().lexeme;
        auto right = parseEquality();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), previous().location);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseEquality() {
    auto expr = parseComparison();

    while (matchAny({Lexer::TokenType::DoubleEquals, Lexer::TokenType::NotEquals})) {
        auto op = previous().lexeme;
        auto right = parseComparison();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), previous().location);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseComparison() {
    auto expr = parseAddition();

    while (matchAny({Lexer::TokenType::LessEquals, Lexer::TokenType::GreaterEquals,
                     Lexer::TokenType::LeftAngle, Lexer::TokenType::RightAngle})) {
        auto op = previous().lexeme;
        auto right = parseAddition();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), previous().location);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseAddition() {
    auto expr = parseMultiplication();

    while (matchAny({Lexer::TokenType::Plus, Lexer::TokenType::Minus})) {
        auto op = previous().lexeme;
        auto right = parseMultiplication();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), previous().location);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseMultiplication() {
    auto expr = parseUnary();

    while (matchAny({Lexer::TokenType::Star, Lexer::TokenType::Slash, Lexer::TokenType::Percent})) {
        auto op = previous().lexeme;
        auto right = parseUnary();
        expr = std::make_unique<BinaryExpr>(std::move(expr), op, std::move(right), previous().location);
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parseUnary() {
    // Handle reference operator &variable
    if (match(Lexer::TokenType::Ampersand)) {
        auto loc = previous().location;
        auto expr = parseUnary();
        return std::make_unique<ReferenceExpr>(std::move(expr), loc);
    }

    if (matchAny({Lexer::TokenType::Exclamation, Lexer::TokenType::Minus})) {
        auto op = previous().lexeme;
        auto loc = previous().location;
        auto right = parseUnary();
        // For simplicity, treating unary as binary with null left side
        // A better approach would be a separate UnaryExpr node
        return std::make_unique<BinaryExpr>(nullptr, op, std::move(right), loc);
    }

    return parsePostfix();
}

std::unique_ptr<Expression> Parser::parsePostfix() {
    auto expr = parsePrimary();

    return parseCallOrMemberAccess(std::move(expr));
}

std::unique_ptr<Expression> Parser::parseCallOrMemberAccess(std::unique_ptr<Expression> expr) {
    while (true) {
        if (match(Lexer::TokenType::Dot)) {
            // Member access
            if (check(Lexer::TokenType::Identifier)) {
                std::string member = advance().lexeme;
                expr = std::make_unique<MemberAccessExpr>(std::move(expr), member, previous().location);
            } else {
                error("Expected identifier after '.'");
                break;
            }
        } else if (match(Lexer::TokenType::LeftParen)) {
            // Function/method call
            std::vector<std::unique_ptr<Expression>> args;

            if (!check(Lexer::TokenType::RightParen)) {
                do {
                    args.push_back(parseExpression());
                } while (match(Lexer::TokenType::Comma));
            }

            consume(Lexer::TokenType::RightParen, "Expected ')' after arguments");

            expr = std::make_unique<CallExpr>(std::move(expr), std::move(args), previous().location);
        } else if (match(Lexer::TokenType::DoubleColon)) {
            // Namespace/class member access
            // Accept both identifiers and Constructor keyword
            if (check(Lexer::TokenType::Identifier) || check(Lexer::TokenType::Constructor)) {
                std::string member = advance().lexeme;

                // Check for template arguments using @ syntax: Collections::List@Integer
                if (match(Lexer::TokenType::At)) {
                    member += "<";

                    // Parse template arguments
                    bool first = true;
                    do {
                        if (!first) {
                            member += ", ";
                        }
                        first = false;

                        // Parse type or value argument (accept Constructor as type name)
                        if (check(Lexer::TokenType::Identifier) || check(Lexer::TokenType::Constructor)) {
                            member += advance().lexeme;
                        } else if (check(Lexer::TokenType::IntegerLiteral)) {
                            member += std::to_string(advance().intValue);
                        } else {
                            error("Expected type or value in template arguments");
                            break;
                        }
                    } while (match(Lexer::TokenType::Comma));

                    member += ">";
                }

                // Treat :: as special member access
                std::cerr << "DEBUG Parser: Creating MemberAccessExpr with member = '::"+member<<"'" << std::endl;
                expr = std::make_unique<MemberAccessExpr>(std::move(expr), "::" + member, previous().location);
            } else {
                error("Expected identifier after '::'");
                break;
            }
        } else {
            break;
        }
    }

    return expr;
}

std::unique_ptr<Expression> Parser::parsePrimary() {
    if (match(Lexer::TokenType::IntegerLiteral)) {
        return std::make_unique<IntegerLiteralExpr>(previous().intValue, previous().location);
    }

    if (match(Lexer::TokenType::StringLiteral)) {
        return std::make_unique<StringLiteralExpr>(previous().stringValue, previous().location);
    }

    if (match(Lexer::TokenType::BoolLiteral)) {
        return std::make_unique<BoolLiteralExpr>(previous().boolValue, previous().location);
    }

    if (match(Lexer::TokenType::This)) {
        return std::make_unique<ThisExpr>(previous().location);
    }

    if (match(Lexer::TokenType::Identifier)) {
        std::string name = previous().lexeme;
        auto loc = previous().location;
        std::cerr << "DEBUG Parser parsePrimary: got identifier '" << name << "'" << std::endl;

        // Check for template arguments using @ syntax: List@Integer
        if (match(Lexer::TokenType::At)) {
            name += "<";
            std::cerr << "DEBUG Parser parsePrimary: found @, parsing template args" << std::endl;

            // Parse template arguments
            bool first = true;
            do {
                if (!first) {
                    name += ", ";
                }
                first = false;

                // Parse type or value argument
                if (check(Lexer::TokenType::Identifier)) {
                    std::string argName = advance().lexeme;
                    std::cerr << "DEBUG Parser parsePrimary: template arg identifier = '" << argName << "'" << std::endl;
                    name += argName;
                } else if (check(Lexer::TokenType::IntegerLiteral)) {
                    name += std::to_string(advance().intValue);
                } else {
                    error("Expected type or value in template arguments");
                    break;
                }
            } while (match(Lexer::TokenType::Comma));

            name += ">";
            std::cerr << "DEBUG Parser parsePrimary: final name with template = '" << name << "'" << std::endl;
        }

        std::cerr << "DEBUG Parser parsePrimary: Creating IdentifierExpr with name = '" << name << "'" << std::endl;
        return std::make_unique<IdentifierExpr>(name, loc);
    }

    if (match(Lexer::TokenType::LeftParen)) {
        auto expr = parseExpression();
        consume(Lexer::TokenType::RightParen, "Expected ')' after expression");
        return expr;
    }

    error("Expected expression");
    return std::make_unique<IntegerLiteralExpr>(0, peek().location); // Error recovery
}

// Helper methods
std::unique_ptr<TypeRef> Parser::parseTypeRef() {
    auto loc = peek().location;
    std::string typeName;

    // Check for NativeType<"...">
    if (match(Lexer::TokenType::NativeType)) {
        consume(Lexer::TokenType::LeftAngle, "Expected '<' after NativeType");

        // Expect a string literal
        if (check(Lexer::TokenType::StringLiteral)) {
            std::string nativeTypeName = advance().stringValue;
            typeName = "NativeType<" + nativeTypeName + ">";
        } else {
            error("Expected string literal for NativeType parameter");
            typeName = "NativeType<unknown>";
        }

        consume(Lexer::TokenType::RightAngle, "Expected '>' after NativeType parameter");
    } else {
        typeName = parseQualifiedIdentifier();
    }

    // Parse template arguments if present (e.g., List<Integer>, Array<Integer, 10>)
    std::vector<TemplateArgument> templateArgs;
    if (check(Lexer::TokenType::LeftAngle)) {
        advance(); // consume '<'

        do {
            auto argLoc = peek().location;

            // Try to determine if this is a type or value argument
            // Simple heuristic: if it starts with an identifier and is followed by ',' or '>',
            // it's likely a type. Otherwise, parse as expression.
            bool isType = false;
            if (check(Lexer::TokenType::Identifier)) {
                // Lookahead to determine if this is a simple type reference
                size_t savedPos = current;
                parseQualifiedIdentifier(); // consume the identifier chain

                // Check what follows
                if (check(Lexer::TokenType::Comma) || check(Lexer::TokenType::RightAngle)) {
                    isType = true;
                }

                // Restore position
                current = savedPos;
            }

            if (isType) {
                // Parse as type argument
                std::string argType = parseQualifiedIdentifier();
                templateArgs.emplace_back(argType, argLoc);
            } else {
                // Parse as value argument (constant expression)
                auto valueExpr = parseExpression();
                templateArgs.emplace_back(std::move(valueExpr), argLoc);
            }

            if (!match(Lexer::TokenType::Comma)) {
                break;
            }
        } while (true);

        consume(Lexer::TokenType::RightAngle, "Expected '>' after template arguments");
    }

    OwnershipType ownership = parseOwnershipType();

    if (!templateArgs.empty()) {
        return std::make_unique<TypeRef>(typeName, std::move(templateArgs), ownership, loc);
    } else {
        return std::make_unique<TypeRef>(typeName, ownership, loc);
    }
}

OwnershipType Parser::parseOwnershipType() {
    if (match(Lexer::TokenType::Caret)) {
        return OwnershipType::Owned;
    } else if (match(Lexer::TokenType::Ampersand)) {
        return OwnershipType::Reference;
    } else if (match(Lexer::TokenType::Percent)) {
        return OwnershipType::Copy;
    } else {
        // No qualifier - returns None (bare type)
        // Validation happens later to ensure this is only allowed in templates
        return OwnershipType::None;
    }
}

std::vector<TemplateParameter> Parser::parseTemplateParameters() {
    std::vector<TemplateParameter> params;

    consume(Lexer::TokenType::LeftAngle, "Expected '<' for template parameters");

    // Parse template parameter list: <T Constrains Type1 | Type2>
    do {
        auto paramLoc = peek().location;

        // Parse parameter name (e.g., "T")
        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected template parameter name");
            break;
        }
        std::string paramName = advance().lexeme;

        // Parse Constrains keyword
        consume(Lexer::TokenType::Constrains, "Expected 'Constrains' after template parameter name");

        // Parse constraints (None or Type1 | Type2 | ...)
        std::vector<std::string> constraints;

        if (match(Lexer::TokenType::None)) {
            // No constraints - any type allowed
            // Leave constraints empty
        } else {
            // Parse constraint list separated by |
            do {
                std::string constraint = parseQualifiedIdentifier();
                constraints.push_back(constraint);

                if (!match(Lexer::TokenType::Pipe)) {
                    break;
                }
            } while (true);
        }

        params.emplace_back(paramName, constraints, paramLoc);

        // Check for more parameters
        if (!match(Lexer::TokenType::Comma)) {
            break;
        }
    } while (true);

    consume(Lexer::TokenType::RightAngle, "Expected '>' after template parameters");

    return params;
}

std::string Parser::parseQualifiedIdentifier() {
    std::string result;

    // Handle special case: "None" keyword can be used as a type name
    if (check(Lexer::TokenType::None)) {
        result = advance().lexeme;
        return result;
    }

    if (check(Lexer::TokenType::Identifier)) {
        result = advance().lexeme;

        while (match(Lexer::TokenType::DoubleColon)) {
            if (check(Lexer::TokenType::Identifier)) {
                result += "::" + advance().lexeme;
            } else {
                error("Expected identifier after '::'");
                break;
            }
        }
    } else {
        error("Expected identifier");
    }

    return result;
}

std::string Parser::parseAngleBracketIdentifier() {
    if (check(Lexer::TokenType::AngleBracketId)) {
        auto token = advance();
        return token.stringValue; // The identifier without brackets
    } else {
        error("Expected angle bracket identifier");
        return "";
    }
}

} // namespace Parser
} // namespace XXML
