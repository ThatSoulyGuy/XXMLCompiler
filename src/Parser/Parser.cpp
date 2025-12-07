#include "../../include/Parser/Parser.h"
#include "../../include/Common/Debug.h"
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

    // Parse any annotation usages before the declaration
    auto annotations = parseAnnotationUsages();

    // Check for top-level Method declaration (for FFI)
    if (check(Lexer::TokenType::Method)) {
        auto method = parseMethod();
        if (method) {
            method->annotations = std::move(annotations);
        }
        return method;
    }

    if (match(Lexer::TokenType::LeftBracket)) {
        if (check(Lexer::TokenType::Namespace)) {
            return parseNamespace();
        } else if (check(Lexer::TokenType::Class)) {
            auto classDecl = parseClass();
            if (classDecl) {
                classDecl->annotations = std::move(annotations);
            }
            return classDecl;
        } else if (check(Lexer::TokenType::NativeStructure)) {
            return parseNativeStructure();
        } else if (check(Lexer::TokenType::CallbackType)) {
            return parseCallbackType();
        } else if (check(Lexer::TokenType::Enumeration)) {
            return parseEnumeration();
        } else if (check(Lexer::TokenType::Constraint)) {
            return parseConstraint();
        } else if (check(Lexer::TokenType::Annotation)) {
            return parseAnnotationDecl();
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

    // Parse Compiletime and Final modifiers (allow either order)
    bool isCompiletime = match(Lexer::TokenType::Compiletime);
    bool isFinal = match(Lexer::TokenType::Final);
    if (!isCompiletime) isCompiletime = match(Lexer::TokenType::Compiletime);

    std::string baseClass;
    if (match(Lexer::TokenType::Extends)) {
        if (match(Lexer::TokenType::None)) {
            baseClass = "";
        } else {
            baseClass = parseQualifiedIdentifier();
        }
    }

    auto classDecl = std::make_unique<ClassDecl>(className, templateParams, isFinal, baseClass, loc);
    classDecl->isCompiletime = isCompiletime;

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

std::unique_ptr<NativeStructureDecl> Parser::parseNativeStructure() {
    auto loc = peek().location;
    consume(Lexer::TokenType::NativeStructure, "Expected 'NativeStructure'");

    std::string structName = parseAngleBracketIdentifier();

    // Parse optional Aligns clause
    size_t alignment = 8;  // Default alignment
    if (match(Lexer::TokenType::Aligns)) {
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Aligns'");
        if (check(Lexer::TokenType::IntegerLiteral)) {
            alignment = static_cast<size_t>(peek().intValue);
            advance();
        } else {
            error("Expected integer alignment value");
        }
        consume(Lexer::TokenType::RightParen, "Expected ')' after alignment value");
    }

    auto nativeStruct = std::make_unique<NativeStructureDecl>(structName, alignment, loc);

    // Parse the public section with properties
    while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
        if (match(Lexer::TokenType::LeftBracket)) {
            // Expect Public <> section
            if (match(Lexer::TokenType::Public)) {
                consume(Lexer::TokenType::LeftAngle, "Expected '<' after 'Public'");
                consume(Lexer::TokenType::RightAngle, "Expected '>' after '<'");

                // Parse properties
                while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
                    if (check(Lexer::TokenType::Property)) {
                        nativeStruct->properties.push_back(parseProperty());
                    } else {
                        error("NativeStructure can only contain Property declarations");
                        synchronize();
                        break;
                    }
                }

                consume(Lexer::TokenType::RightBracket, "Expected ']' after properties");
            } else {
                error("NativeStructure only allows Public access section");
                synchronize();
            }
        } else {
            error("Expected '[' for access section in NativeStructure");
            synchronize();
            break;
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after NativeStructure");

    return nativeStruct;
}

std::unique_ptr<CallbackTypeDecl> Parser::parseCallbackType() {
    auto loc = peek().location;
    consume(Lexer::TokenType::CallbackType, "Expected 'CallbackType'");

    std::string callbackName = parseAngleBracketIdentifier();

    // Parse optional Convention clause
    CallingConvention convention = CallingConvention::CDecl;  // Default for callbacks
    if (match(Lexer::TokenType::Convention)) {
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Convention'");
        if (check(Lexer::TokenType::Identifier)) {
            std::string convName = peek().lexeme;
            advance();
            if (convName == "cdecl") {
                convention = CallingConvention::CDecl;
            } else if (convName == "stdcall") {
                convention = CallingConvention::StdCall;
            } else if (convName == "fastcall") {
                convention = CallingConvention::FastCall;
            } else {
                error("Unknown calling convention: " + convName);
            }
        } else {
            error("Expected calling convention name");
        }
        consume(Lexer::TokenType::RightParen, "Expected ')' after convention name");
    }

    // Parse Returns clause
    consume(Lexer::TokenType::Returns, "Expected 'Returns' in CallbackType");
    auto returnType = parseTypeRef();

    // Parse Parameters clause
    std::vector<std::unique_ptr<ParameterDecl>> parameters;
    consume(Lexer::TokenType::Parameters, "Expected 'Parameters' in CallbackType");
    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Parameters'");

    if (!check(Lexer::TokenType::RightParen)) {
        do {
            parameters.push_back(parseParameter());
        } while (match(Lexer::TokenType::Comma));
    }

    consume(Lexer::TokenType::RightParen, "Expected ')' after parameters");
    consume(Lexer::TokenType::RightBracket, "Expected ']' after CallbackType");

    return std::make_unique<CallbackTypeDecl>(
        callbackName,
        convention,
        std::move(returnType),
        std::move(parameters),
        loc
    );
}

std::unique_ptr<EnumerationDecl> Parser::parseEnumeration() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Enumeration, "Expected 'Enumeration'");

    std::string enumName = parseAngleBracketIdentifier();

    auto enumDecl = std::make_unique<EnumerationDecl>(enumName, loc);

    int64_t nextValue = 0;  // Auto-increment counter

    // Parse Value declarations until ']'
    while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
        if (match(Lexer::TokenType::Value)) {
            auto valueLoc = previous().location;
            std::string valueName = parseAngleBracketIdentifier();

            bool hasExplicitValue = false;
            int64_t value = nextValue;

            // Check for explicit value assignment
            if (match(Lexer::TokenType::Equals)) {
                hasExplicitValue = true;
                // Parse the value (can be negative)
                bool isNegative = false;
                if (match(Lexer::TokenType::Minus)) {
                    isNegative = true;
                }
                if (check(Lexer::TokenType::IntegerLiteral)) {
                    value = peek().intValue;
                    if (isNegative) value = -value;
                    advance();
                } else {
                    error("Expected integer value for enum constant");
                }
            }

            consume(Lexer::TokenType::Semicolon, "Expected ';' after enum value");

            enumDecl->values.push_back(
                std::make_unique<EnumValueDecl>(valueName, hasExplicitValue, value, valueLoc)
            );

            // Update auto-increment counter
            nextValue = value + 1;
        } else {
            error("Expected 'Value' declaration in Enumeration");
            synchronize();
            break;
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after Enumeration");

    return enumDecl;
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
        // Parse any annotations before the member declaration
        auto memberAnnotations = parseAnnotationUsages();

        if (check(Lexer::TokenType::Property)) {
            auto prop = parseProperty();
            if (prop) {
                prop->annotations = std::move(memberAnnotations);
            }
            section->declarations.push_back(std::move(prop));
        } else if (check(Lexer::TokenType::Constructor)) {
            section->declarations.push_back(parseConstructor());
        } else if (check(Lexer::TokenType::Destructor)) {
            section->declarations.push_back(parseDestructor());
        } else if (check(Lexer::TokenType::Method)) {
            auto method = parseMethod();
            if (method) {
                method->annotations = std::move(memberAnnotations);
            }
            section->declarations.push_back(std::move(method));
        } else {
            error("Expected Property, Constructor, Destructor, or Method in access section");
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

    // Parse optional Compiletime modifier
    bool isCompiletime = match(Lexer::TokenType::Compiletime);

    consume(Lexer::TokenType::Semicolon, "Expected ';' after property declaration");

    auto prop = std::make_unique<PropertyDecl>(propertyName, std::move(type), loc);
    prop->isCompiletime = isCompiletime;
    return prop;
}

std::unique_ptr<ConstructorDecl> Parser::parseConstructor() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Constructor, "Expected 'Constructor'");

    bool isDefault = false;
    bool isCompiletime = false;
    std::vector<std::unique_ptr<ParameterDecl>> params;
    std::vector<std::unique_ptr<Statement>> body;

    if (match(Lexer::TokenType::Equals)) {
        consume(Lexer::TokenType::Default, "Expected 'default' after '='");
        isDefault = true;
        consume(Lexer::TokenType::Semicolon, "Expected ';' after default constructor");
    } else if (match(Lexer::TokenType::Parameters)) {
        // New syntax: Constructor Parameters(...) Compiletime -> { body }
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Parameters'");

        // Parse parameters
        if (!check(Lexer::TokenType::RightParen)) {
            do {
                params.push_back(parseParameter());
            } while (match(Lexer::TokenType::Comma));
        }

        consume(Lexer::TokenType::RightParen, "Expected ')' after constructor parameters");

        // Parse optional Compiletime modifier
        isCompiletime = match(Lexer::TokenType::Compiletime);

        consume(Lexer::TokenType::Arrow, "Expected '->' before constructor body");
        consume(Lexer::TokenType::LeftBrace, "Expected '{' to start constructor body");

        // Parse constructor body
        while (!check(Lexer::TokenType::RightBrace) && !isAtEnd()) {
            body.push_back(parseStatement());
        }

        consume(Lexer::TokenType::RightBrace, "Expected '}' after constructor body");
    } else if (match(Lexer::TokenType::LeftParen)) {
        // Old syntax: Constructor (params) Compiletime -> { body }

        // Parse parameters
        if (!check(Lexer::TokenType::RightParen)) {
            do {
                params.push_back(parseParameter());
            } while (match(Lexer::TokenType::Comma));
        }

        consume(Lexer::TokenType::RightParen, "Expected ')' after constructor parameters");

        // Parse optional Compiletime modifier
        isCompiletime = match(Lexer::TokenType::Compiletime);

        consume(Lexer::TokenType::Arrow, "Expected '->' before constructor body");
        consume(Lexer::TokenType::LeftBrace, "Expected '{' to start constructor body");

        // Parse constructor body
        while (!check(Lexer::TokenType::RightBrace) && !isAtEnd()) {
            body.push_back(parseStatement());
        }

        consume(Lexer::TokenType::RightBrace, "Expected '}' after constructor body");
    } else {
        consume(Lexer::TokenType::Semicolon, "Expected ';' after constructor");
    }

    auto ctor = std::make_unique<ConstructorDecl>(isDefault, std::move(params), std::move(body), loc);
    ctor->isCompiletime = isCompiletime;
    return ctor;
}

std::unique_ptr<DestructorDecl> Parser::parseDestructor() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Destructor, "Expected 'Destructor'");

    std::vector<std::unique_ptr<Statement>> body;

    // Destructors must use: Destructor Parameters() -> { body }
    consume(Lexer::TokenType::Parameters, "Expected 'Parameters' after 'Destructor'");
    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Parameters'");

    // Destructors cannot have parameters - verify empty parameter list
    if (!check(Lexer::TokenType::RightParen)) {
        error("Destructors cannot have parameters");
        // Skip to closing paren
        while (!check(Lexer::TokenType::RightParen) && !isAtEnd()) {
            advance();
        }
    }

    consume(Lexer::TokenType::RightParen, "Expected ')' after destructor parameters");
    consume(Lexer::TokenType::Arrow, "Expected '->' before destructor body");
    consume(Lexer::TokenType::LeftBrace, "Expected '{' to start destructor body");

    // Parse destructor body
    while (!check(Lexer::TokenType::RightBrace) && !isAtEnd()) {
        body.push_back(parseStatement());
    }

    consume(Lexer::TokenType::RightBrace, "Expected '}' after destructor body");

    return std::make_unique<DestructorDecl>(std::move(body), loc);
}

std::unique_ptr<MethodDecl> Parser::parseMethod() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Method, "Expected 'Method'");

    std::string methodName = parseAngleBracketIdentifier();

    // Parse template parameters if present (using 'Templates' keyword)
    std::vector<TemplateParameter> templateParams;
    if (match(Lexer::TokenType::Templates)) {
        templateParams = parseTemplateParameters();
    }

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

    // Parse optional Compiletime modifier
    bool isCompiletime = match(Lexer::TokenType::Compiletime);

    // Check for semicolon termination (native/FFI method declaration)
    if (match(Lexer::TokenType::Semicolon)) {
        // Native method - no body, just declaration
        std::vector<std::unique_ptr<Statement>> emptyBody;
        auto method = std::make_unique<MethodDecl>(methodName, templateParams, std::move(returnType),
                                                   std::move(params), std::move(emptyBody), loc);
        method->isNative = true;
        method->isCompiletime = isCompiletime;
        return method;
    }

    // Accept either '->' or 'Do' before method body
    if (!match(Lexer::TokenType::Arrow) && !match(Lexer::TokenType::Do)) {
        error("Expected '->' or 'Do' before method body, or ';' for native method declaration");
    }

    auto body = parseBlock();

    auto method = std::make_unique<MethodDecl>(methodName, templateParams, std::move(returnType),
                                        std::move(params), std::move(body), loc);
    method->isCompiletime = isCompiletime;
    return method;
}

// Static counter for generating unique anonymous parameter names
static int anonymousParamCounter = 0;

std::unique_ptr<ParameterDecl> Parser::parseParameter() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Parameter, "Expected 'Parameter'");

    std::string paramName;

    // Check for empty <> (anonymous parameter for FFI)
    if (check(Lexer::TokenType::LeftAngle)) {
        advance();  // consume '<'
        if (match(Lexer::TokenType::RightAngle)) {
            // Empty <> - generate anonymous name
            paramName = "_arg" + std::to_string(anonymousParamCounter++);
        } else {
            error("Expected '>' after '<' for anonymous parameter");
        }
    } else {
        paramName = parseAngleBracketIdentifier();
    }

    // If the paramName is empty (from an empty AngleBracketId), generate one
    if (paramName.empty()) {
        paramName = "_arg" + std::to_string(anonymousParamCounter++);
    }

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

std::unique_ptr<ConstraintDecl> Parser::parseConstraint() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Constraint, "Expected 'Constraint'");

    // Parse constraint name: <MyConstraint>
    std::string constraintName = parseAngleBracketIdentifier();

    // Parse template parameters if present: <T Constrains None>
    std::vector<TemplateParameter> templateParams;
    if (check(Lexer::TokenType::LeftAngle)) {
        templateParams = parseTemplateParameters();
    }

    // Parse parameter bindings: (T a, U b)
    std::vector<ConstraintParamBinding> paramBindings;
    if (match(Lexer::TokenType::LeftParen)) {
        do {
            auto bindingLoc = peek().location;

            // Parse template parameter name (e.g., "T")
            if (!check(Lexer::TokenType::Identifier)) {
                error("Expected template parameter name in binding");
                break;
            }
            std::string templateParamName = advance().lexeme;

            // Parse binding name (e.g., "a")
            if (!check(Lexer::TokenType::Identifier)) {
                error("Expected binding name after template parameter");
                break;
            }
            std::string bindingName = advance().lexeme;

            paramBindings.emplace_back(templateParamName, bindingName, bindingLoc);

            // Check for more bindings
            if (!match(Lexer::TokenType::Comma)) {
                break;
            }
        } while (true);

        consume(Lexer::TokenType::RightParen, "Expected ')' after parameter bindings");
    }

    auto constraintDecl = std::make_unique<ConstraintDecl>(
        constraintName, templateParams, paramBindings, loc
    );

    // Parse Require statements until ]
    while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
        if (check(Lexer::TokenType::Require)) {
            auto requireStmt = parseRequireStatement();
            if (requireStmt) {
                constraintDecl->requirements.push_back(std::move(requireStmt));
            }
        } else {
            error("Expected 'Require' statement in constraint");
            synchronize();
            break;
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after constraint");

    return constraintDecl;
}

std::unique_ptr<RequireStmt> Parser::parseRequireStatement() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Require, "Expected 'Require'");

    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Require'");

    // Check what kind of requirement this is
    // Check FC (compile-time method) before F
    if (check(Lexer::TokenType::Identifier) && peek().lexeme == "FC") {
        // Compile-time method requirement: FC(ReturnType)(methodName)(*) On param
        advance(); // consume 'FC'

        auto requireStmt = std::make_unique<RequireStmt>(RequirementKind::CompiletimeMethod, loc);

        // Parse (ReturnType)
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'FC'");
        requireStmt->methodReturnType = parseTypeRef();
        consume(Lexer::TokenType::RightParen, "Expected ')' after return type");

        // Parse (methodName)
        consume(Lexer::TokenType::LeftParen, "Expected '(' for method name");
        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected method name");
        }
        requireStmt->methodName = advance().lexeme;
        consume(Lexer::TokenType::RightParen, "Expected ')' after method name");

        // Parse parameters: either (*) for wildcard or (Type1, Type2, ...) for specific types
        consume(Lexer::TokenType::LeftParen, "Expected '(' for parameters");

        if (check(Lexer::TokenType::Star)) {
            advance(); // consume '*'
        } else if (!check(Lexer::TokenType::RightParen)) {
            do {
                parseTypeRef();
            } while (match(Lexer::TokenType::Comma));
        }

        consume(Lexer::TokenType::RightParen, "Expected ')' after parameters");

        // Parse On param
        consume(Lexer::TokenType::On, "Expected 'On' after method signature");
        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected parameter name after 'On'");
        }
        requireStmt->targetParam = advance().lexeme;

        consume(Lexer::TokenType::RightParen, "Expected ')' after compile-time method requirement");

        return requireStmt;

    } else if (check(Lexer::TokenType::Identifier) && peek().lexeme == "F") {
        // Method requirement: F(ReturnType)(methodName)(*) On param
        advance(); // consume 'F'

        auto requireStmt = std::make_unique<RequireStmt>(RequirementKind::Method, loc);

        // Parse (ReturnType)
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'F'");
        requireStmt->methodReturnType = parseTypeRef();
        consume(Lexer::TokenType::RightParen, "Expected ')' after return type");

        // Parse (methodName)
        consume(Lexer::TokenType::LeftParen, "Expected '(' for method name");
        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected method name");
        }
        requireStmt->methodName = advance().lexeme;
        consume(Lexer::TokenType::RightParen, "Expected ')' after method name");

        // Parse parameters: either (*) for wildcard or (Type1, Type2, ...) for specific types
        consume(Lexer::TokenType::LeftParen, "Expected '(' for parameters");

        if (check(Lexer::TokenType::Star)) {
            // Wildcard - accept any parameters
            advance(); // consume '*'
        } else if (!check(Lexer::TokenType::RightParen)) {
            // Specific parameter types - parse them but we'll ignore for now (just validate method exists)
            // This allows syntax like (None), (Integer^, String^), etc.
            do {
                parseTypeRef(); // Parse and discard parameter type
            } while (match(Lexer::TokenType::Comma));
        }
        // Empty () is also allowed - no parameters

        consume(Lexer::TokenType::RightParen, "Expected ')' after parameters");

        // Parse On param
        consume(Lexer::TokenType::On, "Expected 'On' after method signature");
        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected parameter name after 'On'");
        }
        requireStmt->targetParam = advance().lexeme;

        consume(Lexer::TokenType::RightParen, "Expected ')' after method requirement");

        return requireStmt;

    } else if (check(Lexer::TokenType::Identifier) && peek().lexeme == "CC") {
        // Compile-time constructor requirement: CC(ParamTypes...) On param
        advance(); // consume 'CC'

        auto requireStmt = std::make_unique<RequireStmt>(RequirementKind::CompiletimeConstructor, loc);

        // Parse (ParamTypes...)
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'CC'");

        // Parse parameter types (could be None or actual types)
        if (!check(Lexer::TokenType::None) && !check(Lexer::TokenType::RightParen)) {
            do {
                requireStmt->constructorParamTypes.push_back(parseTypeRef());
            } while (match(Lexer::TokenType::Comma));
        } else if (match(Lexer::TokenType::None)) {
            // No parameters - leave constructorParamTypes empty
        }

        consume(Lexer::TokenType::RightParen, "Expected ')' after constructor parameters");

        // Parse On param
        consume(Lexer::TokenType::On, "Expected 'On' after constructor signature");
        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected parameter name after 'On'");
        }
        requireStmt->targetParam = advance().lexeme;

        consume(Lexer::TokenType::RightParen, "Expected ')' after compile-time constructor requirement");

        return requireStmt;

    } else if (check(Lexer::TokenType::Identifier) && peek().lexeme == "C") {
        // Constructor requirement: C(ParamTypes...) On param
        advance(); // consume 'C'

        auto requireStmt = std::make_unique<RequireStmt>(RequirementKind::Constructor, loc);

        // Parse (ParamTypes...)
        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'C'");

        // Parse parameter types (could be None or actual types)
        if (!check(Lexer::TokenType::None) && !check(Lexer::TokenType::RightParen)) {
            do {
                requireStmt->constructorParamTypes.push_back(parseTypeRef());
            } while (match(Lexer::TokenType::Comma));
        } else if (match(Lexer::TokenType::None)) {
            // No parameters - leave constructorParamTypes empty
        }

        consume(Lexer::TokenType::RightParen, "Expected ')' after constructor parameters");

        // Parse On param
        consume(Lexer::TokenType::On, "Expected 'On' after constructor signature");
        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected parameter name after 'On'");
        }
        requireStmt->targetParam = advance().lexeme;

        consume(Lexer::TokenType::RightParen, "Expected ')' after constructor requirement");

        return requireStmt;

    } else if (check(Lexer::TokenType::Truth)) {
        // Truth requirement: Truth(expression)
        advance(); // consume 'Truth'

        auto requireStmt = std::make_unique<RequireStmt>(RequirementKind::Truth, loc);

        consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Truth'");
        requireStmt->truthCondition = parseExpression();
        consume(Lexer::TokenType::RightParen, "Expected ')' after truth condition");

        consume(Lexer::TokenType::RightParen, "Expected ')' after truth requirement");

        return requireStmt;

    } else {
        error("Expected 'F', 'FC', 'C', 'CC', or 'Truth' in Require statement");
        return nullptr;
    }
}

// ============================================================================
// Annotation Parsing
// ============================================================================

std::vector<std::unique_ptr<AnnotationUsage>> Parser::parseAnnotationUsages() {
    std::vector<std::unique_ptr<AnnotationUsage>> annotations;
    while (check(Lexer::TokenType::At)) {
        annotations.push_back(parseAnnotationUsage());
    }
    return annotations;
}

std::unique_ptr<AnnotationUsage> Parser::parseAnnotationUsage() {
    auto loc = peek().location;
    consume(Lexer::TokenType::At, "Expected '@'");

    std::string name = parseQualifiedIdentifier();

    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> args;
    if (match(Lexer::TokenType::LeftParen)) {
        // Parse named arguments: name = expression
        if (!check(Lexer::TokenType::RightParen)) {
            do {
                // Argument name can be angle bracket identifier <name> or bare identifier
                std::string argName;
                if (check(Lexer::TokenType::AngleBracketId)) {
                    argName = parseAngleBracketIdentifier();
                } else if (check(Lexer::TokenType::Identifier)) {
                    argName = advance().lexeme;
                } else {
                    error("Expected argument name");
                    break;
                }
                consume(Lexer::TokenType::Equals, "Expected '=' after argument name");
                auto value = parseExpression();
                args.push_back({argName, std::move(value)});
            } while (match(Lexer::TokenType::Comma));
        }
        consume(Lexer::TokenType::RightParen, "Expected ')' after annotation arguments");
    }

    return std::make_unique<AnnotationUsage>(name, std::move(args), loc);
}

std::vector<AnnotationTarget> Parser::parseAnnotationAllows() {
    std::vector<AnnotationTarget> targets;

    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'Allows'");

    do {
        // Parse AnnotationAllow::Target
        consume(Lexer::TokenType::AnnotationAllow, "Expected 'AnnotationAllow'");
        consume(Lexer::TokenType::DoubleColon, "Expected '::'");

        if (!check(Lexer::TokenType::Identifier)) {
            error("Expected annotation target (Properties, Variables, Classes, or Methods)");
            break;
        }

        std::string targetName = advance().lexeme;
        if (targetName == "Properties") {
            targets.push_back(AnnotationTarget::Properties);
        } else if (targetName == "Variables") {
            targets.push_back(AnnotationTarget::Variables);
        } else if (targetName == "Classes") {
            targets.push_back(AnnotationTarget::Classes);
        } else if (targetName == "Methods") {
            targets.push_back(AnnotationTarget::Methods);
        } else {
            error("Unknown annotation target: " + targetName);
        }
    } while (match(Lexer::TokenType::Comma));

    consume(Lexer::TokenType::RightParen, "Expected ')' after allowed targets");

    return targets;
}

std::unique_ptr<AnnotateDecl> Parser::parseAnnotateDecl() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Annotate, "Expected 'Annotate'");

    // Parse (Type^)
    consume(Lexer::TokenType::LeftParen, "Expected '(' for annotate type");
    auto type = parseTypeRef();
    consume(Lexer::TokenType::RightParen, "Expected ')' after annotate type");

    // Parse (name) - can be <name> or bare identifier
    consume(Lexer::TokenType::LeftParen, "Expected '(' for annotate name");
    std::string name;
    if (check(Lexer::TokenType::AngleBracketId)) {
        name = parseAngleBracketIdentifier();
    } else if (check(Lexer::TokenType::Identifier)) {
        name = advance().lexeme;
    } else {
        error("Expected parameter name in annotate declaration");
    }
    consume(Lexer::TokenType::RightParen, "Expected ')' after annotate name");

    // Parse optional default value: = Expression
    std::unique_ptr<Expression> defaultValue;
    if (match(Lexer::TokenType::Equals)) {
        defaultValue = parseExpression();
    }

    consume(Lexer::TokenType::Semicolon, "Expected ';' after annotate declaration");

    return std::make_unique<AnnotateDecl>(name, std::move(type), std::move(defaultValue), loc);
}

std::unique_ptr<ProcessorDecl> Parser::parseProcessorDecl() {
    auto loc = peek().location;
    consume(Lexer::TokenType::LeftBracket, "Expected '[' before 'Processor'");
    consume(Lexer::TokenType::Processor, "Expected 'Processor'");

    std::vector<std::unique_ptr<AccessSection>> sections;

    // Parse access sections (similar to class)
    while (!check(Lexer::TokenType::RightBracket) && !isAtEnd()) {
        if (check(Lexer::TokenType::LeftBracket)) {
            auto accessSection = parseAccessSection();
            if (accessSection) {
                sections.push_back(std::move(accessSection));
            }
        } else {
            error("Expected access section in processor");
            synchronize();
            break;
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after processor");

    return std::make_unique<ProcessorDecl>(std::move(sections), loc);
}

std::unique_ptr<AnnotationDecl> Parser::parseAnnotationDecl() {
    auto loc = peek().location;
    consume(Lexer::TokenType::Annotation, "Expected 'Annotation'");

    std::string name = parseAngleBracketIdentifier();

    // Parse Allows clause
    consume(Lexer::TokenType::Allows, "Expected 'Allows' after annotation name");
    auto targets = parseAnnotationAllows();

    // Parse optional Retain
    bool retain = match(Lexer::TokenType::Retain);

    // Parse Annotate declarations
    std::vector<std::unique_ptr<AnnotateDecl>> parameters;
    while (check(Lexer::TokenType::Annotate)) {
        parameters.push_back(parseAnnotateDecl());
    }

    // Parse optional Processor
    std::unique_ptr<ProcessorDecl> processor;
    if (check(Lexer::TokenType::LeftBracket)) {
        // Peek ahead to check if it's a Processor
        size_t savedPos = current;
        advance(); // consume '['
        if (check(Lexer::TokenType::Processor)) {
            current = savedPos; // restore position
            processor = parseProcessorDecl();
        } else {
            current = savedPos; // restore position
        }
    }

    consume(Lexer::TokenType::RightBracket, "Expected ']' after annotation definition");

    return std::make_unique<AnnotationDecl>(name, std::move(targets), std::move(parameters),
                                            std::move(processor), retain, loc);
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
    // Check for annotations before Instantiate
    auto annotations = parseAnnotationUsages();

    if (match(Lexer::TokenType::Instantiate)) {
        auto inst = parseInstantiate();
        if (inst) {
            inst->annotations = std::move(annotations);
        }
        return inst;
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

    // Parse optional Compiletime modifier
    bool isCompiletime = match(Lexer::TokenType::Compiletime);

    auto type = parseTypeRef();

    consume(Lexer::TokenType::As, "Expected 'As' after type");

    std::string varName = parseAngleBracketIdentifier();

    consume(Lexer::TokenType::Equals, "Expected '=' after variable name");

    auto initializer = parseExpression();

    consume(Lexer::TokenType::Semicolon, "Expected ';' after instantiate statement");

    auto stmt = std::make_unique<InstantiateStmt>(std::move(type), varName,
                                             std::move(initializer), loc);
    stmt->isCompiletime = isCompiletime;
    return stmt;
}

std::unique_ptr<AssignmentStmt> Parser::parseAssignment() {
    auto loc = previous().location;

    // Parse lvalue expression (can be identifier, this, or member access like this.x)
    std::unique_ptr<Expression> target;

    if (check(Lexer::TokenType::AngleBracketId)) {
        std::string name = parseAngleBracketIdentifier();
        target = std::make_unique<IdentifierExpr>(name, loc);
    } else if (check(Lexer::TokenType::This)) {
        advance();
        target = std::make_unique<ThisExpr>(loc);
    } else if (check(Lexer::TokenType::Identifier)) {
        std::string name = advance().lexeme;
        target = std::make_unique<IdentifierExpr>(name, loc);
    } else {
        error("Expected variable name or 'this' after 'Set'");
        return nullptr;
    }

    // Check for member access (. or ::)
    while (check(Lexer::TokenType::Dot) || check(Lexer::TokenType::DoubleColon)) {
        bool isStatic = check(Lexer::TokenType::DoubleColon);
        advance();  // consume "." or "::"

        if (check(Lexer::TokenType::Identifier)) {
            std::string memberName = advance().lexeme;
            // For :: access, include prefix for namespace/static distinction
            // For . access, just use the member name
            std::string member = isStatic ? ("::" + memberName) : memberName;
            target = std::make_unique<MemberAccessExpr>(
                std::move(target),
                member,
                loc
            );
        } else if (check(Lexer::TokenType::AngleBracketId)) {
            std::string memberName = parseAngleBracketIdentifier();
            std::string member = isStatic ? ("::" + memberName) : memberName;
            target = std::make_unique<MemberAccessExpr>(
                std::move(target),
                member,
                loc
            );
        } else {
            error("Expected member name after accessor");
            return nullptr;
        }
    }

    consume(Lexer::TokenType::Equals, "Expected '=' after lvalue");

    auto value = parseExpression();

    consume(Lexer::TokenType::Semicolon, "Expected ';' after assignment");

    return std::make_unique<AssignmentStmt>(std::move(target), std::move(value), loc);
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

    auto initExpr = parseExpression();

    // Check if this is a range-based loop (.. syntax) or C-style loop (; syntax)
    if (match(Lexer::TokenType::Range)) {
        // Range-based for loop: For (Type <name> = start .. end)
        auto rangeEnd = parseExpression();

        consume(Lexer::TokenType::RightParen, "Expected ')' after for loop range");
        consume(Lexer::TokenType::Arrow, "Expected '->' before for loop body");

        auto body = parseBlock();

        return std::make_unique<ForStmt>(std::move(iteratorType), iteratorName,
                                         std::move(initExpr), std::move(rangeEnd),
                                         std::move(body), loc);
    } else if (match(Lexer::TokenType::Semicolon)) {
        // C-style for loop: For (Type <name> = init; condition; increment)
        auto condition = parseExpression();

        consume(Lexer::TokenType::Semicolon, "Expected ';' after condition in C-style for loop");

        auto increment = parseExpression();

        consume(Lexer::TokenType::RightParen, "Expected ')' after increment expression");
        consume(Lexer::TokenType::Arrow, "Expected '->' before for loop body");

        auto body = parseBlock();

        return std::make_unique<ForStmt>(std::move(iteratorType), iteratorName,
                                         std::move(initExpr), std::move(condition),
                                         std::move(increment),
                                         std::move(body), loc);
    } else {
        error("Expected '..' for range-based loop or ';' for C-style loop");
        return nullptr;
    }
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
                auto memberLoc = previous().location;

                // Check for template arguments after member name: obj.method<T>(args)
                // Handle AngleBracketId token (lexer tokenized <Identifier> as a single token)
                if (check(Lexer::TokenType::AngleBracketId)) {
                    // The lexer has already tokenized <Type> as AngleBracketId
                    std::string templateArg = advance().stringValue;
                    member += "<" + templateArg + ">";
                }
                else if (check(Lexer::TokenType::LeftAngle)) {
                    // Lookahead to distinguish template from comparison
                    size_t savedPos = current;
                    advance(); // consume <

                    bool looksLikeTemplate = false;
                    if (check(Lexer::TokenType::Identifier) || check(Lexer::TokenType::Question)) {
                        advance();
                        // Handle qualified names like Namespace::Type
                        while (check(Lexer::TokenType::DoubleColon)) {
                            advance(); // consume ::
                            if (check(Lexer::TokenType::Identifier)) {
                                advance(); // consume identifier
                            }
                        }
                        if (check(Lexer::TokenType::RightAngle) || check(Lexer::TokenType::Comma)) {
                            looksLikeTemplate = true;
                        }
                    } else if (check(Lexer::TokenType::IntegerLiteral)) {
                        advance();
                        if (check(Lexer::TokenType::RightAngle) || check(Lexer::TokenType::Comma)) {
                            looksLikeTemplate = true;
                        }
                    }

                    // Restore position
                    current = savedPos;

                    if (looksLikeTemplate) {
                        advance(); // consume <
                        member += "<";

                        // Parse template arguments
                        bool first = true;
                        do {
                            if (!first) {
                                member += ", ";
                            }
                            first = false;

                            // Check for wildcard
                            if (match(Lexer::TokenType::Question)) {
                                member += "?";
                            }
                            // Parse type or value argument (could be qualified name)
                            else if (check(Lexer::TokenType::Identifier)) {
                                std::string argName = parseQualifiedIdentifier();
                                member += argName;
                            } else if (check(Lexer::TokenType::IntegerLiteral)) {
                                member += std::to_string(advance().intValue);
                            } else {
                                error("Expected type or value in template arguments");
                                break;
                            }
                        } while (match(Lexer::TokenType::Comma));

                        consume(Lexer::TokenType::RightAngle, "Expected '>' after template arguments");
                        member += ">";
                    }
                }

                expr = std::make_unique<MemberAccessExpr>(std::move(expr), member, memberLoc);
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

                // ✅ Check for template arguments using < > syntax: Collections::List<Integer>
                if (check(Lexer::TokenType::LeftAngle)) {
                    // Lookahead to distinguish template from comparison
                    size_t savedPos = current;
                    advance(); // consume <

                    bool looksLikeTemplate = false;
                    if (check(Lexer::TokenType::Identifier) || check(Lexer::TokenType::Question)) {
                        advance();
                        // Handle qualified names like Namespace::Type
                        while (check(Lexer::TokenType::DoubleColon)) {
                            advance(); // consume ::
                            if (check(Lexer::TokenType::Identifier)) {
                                advance(); // consume identifier
                            }
                        }
                        if (check(Lexer::TokenType::RightAngle) || check(Lexer::TokenType::Comma)) {
                            looksLikeTemplate = true;
                        }
                    } else if (check(Lexer::TokenType::IntegerLiteral)) {
                        advance();
                        if (check(Lexer::TokenType::RightAngle) || check(Lexer::TokenType::Comma)) {
                            looksLikeTemplate = true;
                        }
                    }

                    // Restore position
                    current = savedPos;

                    if (looksLikeTemplate) {
                        advance(); // consume <
                        member += "<";

                        // Parse template arguments
                        bool first = true;
                        do {
                            if (!first) {
                                member += ", ";
                            }
                            first = false;

                            // Check for wildcard
                            if (match(Lexer::TokenType::Question)) {
                                member += "?";
                            }
                            // Parse type or value argument (could be qualified like MyNs::Item)
                            else if (check(Lexer::TokenType::Identifier) || check(Lexer::TokenType::Constructor)) {
                                // Use parseQualifiedIdentifier to handle :: properly
                                std::string argName = (check(Lexer::TokenType::Constructor)) ?
                                    advance().lexeme : parseQualifiedIdentifier();
                                DEBUG_OUT("DEBUG parseCall: template arg parsed as '" << argName << "'" << std::endl);
                                DEBUG_OUT("DEBUG parseCall: next token is " << peek().lexeme << " (type " << static_cast<int>(peek().type) << ")" << std::endl);
                                member += argName;
                            } else if (check(Lexer::TokenType::IntegerLiteral)) {
                                member += std::to_string(advance().intValue);
                            } else {
                                error("Expected type or value in template arguments");
                                break;
                            }
                        } while (match(Lexer::TokenType::Comma));

                        consume(Lexer::TokenType::RightAngle, "Expected '>' after template arguments");
                        member += ">";
                    }
                }

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
                DEBUG_OUT("DEBUG Parser: Creating MemberAccessExpr with member = '::"+member<<"'" << std::endl);
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

    if (match(Lexer::TokenType::FloatLiteral)) {
        return std::make_unique<FloatLiteralExpr>(previous().floatValue, previous().location);
    }

    if (match(Lexer::TokenType::DoubleLiteral)) {
        return std::make_unique<DoubleLiteralExpr>(previous().doubleValue, previous().location);
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

    // ✅ Handle TypeOf<T>() expressions
    if (match(Lexer::TokenType::TypeOf)) {
        return parseTypeOfExpression();
    }

    // ✅ Handle None keyword as identifier for None::Constructor() calls
    if (match(Lexer::TokenType::None)) {
        std::string name = "None";
        auto loc = previous().location;
        DEBUG_OUT("DEBUG Parser parsePrimary: got None keyword as identifier" << std::endl);
        return std::make_unique<IdentifierExpr>(name, loc);
    }

    if (match(Lexer::TokenType::Identifier)) {
        std::string name = previous().lexeme;
        auto loc = previous().location;
        DEBUG_OUT("DEBUG Parser parsePrimary: got identifier '" << name << "'" << std::endl);

        // ✅ Check for template arguments using < > syntax: List<Integer>
        // We need to be careful here - < could be a comparison operator
        // Heuristic: if we see Identifier followed by <, lookahead to see if it looks like a template
        if (check(Lexer::TokenType::LeftAngle)) {
            // Lookahead to distinguish template from comparison
            size_t savedPos = current;
            advance(); // consume <

            bool looksLikeTemplate = false;
            if (check(Lexer::TokenType::Identifier) || check(Lexer::TokenType::Question)) {
                // Lookahead further to see if we have Identifier followed by > or ,
                advance();
                if (check(Lexer::TokenType::RightAngle) || check(Lexer::TokenType::Comma)) {
                    looksLikeTemplate = true;
                }
            } else if (check(Lexer::TokenType::IntegerLiteral)) {
                // Could be a value template parameter like Array<Integer, 5>
                advance();
                if (check(Lexer::TokenType::RightAngle) || check(Lexer::TokenType::Comma)) {
                    looksLikeTemplate = true;
                }
            }

            // Restore position
            current = savedPos;

            if (looksLikeTemplate) {
                advance(); // consume <
                name += "<";
                DEBUG_OUT("DEBUG Parser parsePrimary: found <, parsing template args" << std::endl);

                // Parse template arguments
                bool first = true;
                do {
                    if (!first) {
                        name += ", ";
                    }
                    first = false;

                    // Check for wildcard
                    if (match(Lexer::TokenType::Question)) {
                        name += "?";
                    }
                    // Parse type or value argument (could be qualified name like Namespace::Type)
                    else if (check(Lexer::TokenType::Identifier)) {
                        // Use parseQualifiedIdentifier to handle :: properly
                        std::string argName = parseQualifiedIdentifier();
                        DEBUG_OUT("DEBUG Parser parsePrimary: template arg identifier = '" << argName << "'" << std::endl);
                        name += argName;
                    } else if (check(Lexer::TokenType::IntegerLiteral)) {
                        name += std::to_string(advance().intValue);
                    } else {
                        error("Expected type or value in template arguments");
                        break;
                    }
                } while (match(Lexer::TokenType::Comma));

                consume(Lexer::TokenType::RightAngle, "Expected '>' after template arguments");
                name += ">";
                DEBUG_OUT("DEBUG Parser parsePrimary: final name with template = '" << name << "'" << std::endl);
            }
        }

        // Check for AngleBracketId (lexer tokenized <Type> as single token): identity<Integer>
        if (check(Lexer::TokenType::AngleBracketId)) {
            std::string templateArg = advance().stringValue;
            name += "<" + templateArg + ">";
            DEBUG_OUT("DEBUG Parser parsePrimary: found AngleBracketId, name = '" << name << "'" << std::endl);
        }

        // Check for template arguments using @ syntax: List@Integer (alternative)
        // Supports nested templates like Box@Box<Integer> or HashMap@String, List<Integer>
        if (match(Lexer::TokenType::At)) {
            name += "<";
            DEBUG_OUT("DEBUG Parser parsePrimary: found @, parsing template args" << std::endl);

            // Parse template arguments (supports nested templates)
            bool first = true;
            do {
                if (!first) {
                    name += ", ";
                }
                first = false;

                // Parse type or value argument - now supports nested templates
                if (check(Lexer::TokenType::Identifier)) {
                    // Parse simple identifier (NOT qualified - :: after template args is for methods)
                    std::string argName = advance().lexeme;
                    DEBUG_OUT("DEBUG Parser parsePrimary: template arg identifier = '" << argName << "'" << std::endl);

                    // Check for nested template arguments using <...> syntax
                    if (check(Lexer::TokenType::LeftAngle)) {
                        advance(); // consume '<'
                        argName += "<";
                        int angleDepth = 1;

                        // Parse nested template arguments, handling arbitrary nesting
                        while (angleDepth > 0 && !isAtEnd()) {
                            if (check(Lexer::TokenType::LeftAngle)) {
                                angleDepth++;
                                argName += "<";
                                advance();
                            } else if (check(Lexer::TokenType::RightAngle)) {
                                angleDepth--;
                                argName += ">";
                                advance();
                            } else if (check(Lexer::TokenType::Comma) && angleDepth == 1) {
                                // Only treat as separator at top level of nested args
                                argName += ", ";
                                advance();
                            } else if (check(Lexer::TokenType::Identifier)) {
                                argName += advance().lexeme;
                            } else if (check(Lexer::TokenType::DoubleColon)) {
                                argName += "::";
                                advance();
                            } else if (check(Lexer::TokenType::Caret)) {
                                argName += "^";
                                advance();
                            } else if (check(Lexer::TokenType::Ampersand)) {
                                argName += "&";
                                advance();
                            } else if (check(Lexer::TokenType::Percent)) {
                                argName += "%";
                                advance();
                            } else if (check(Lexer::TokenType::IntegerLiteral)) {
                                argName += std::to_string(advance().intValue);
                            } else {
                                error("Unexpected token in nested template arguments");
                                break;
                            }
                        }
                    }

                    // Check for ownership modifier on the type argument
                    if (check(Lexer::TokenType::Caret)) {
                        argName += "^";
                        advance();
                    } else if (check(Lexer::TokenType::Ampersand)) {
                        argName += "&";
                        advance();
                    } else if (check(Lexer::TokenType::Percent)) {
                        argName += "%";
                        advance();
                    }

                    name += argName;
                } else if (check(Lexer::TokenType::IntegerLiteral)) {
                    name += std::to_string(advance().intValue);
                } else {
                    error("Expected type or value in template arguments");
                    break;
                }
            } while (match(Lexer::TokenType::Comma));

            name += ">";
            DEBUG_OUT("DEBUG Parser parsePrimary: final name with template = '" << name << "'" << std::endl);
        }

        DEBUG_OUT("DEBUG Parser parsePrimary: Creating IdentifierExpr with name = '" << name << "'" << std::endl);
        return std::make_unique<IdentifierExpr>(name, loc);
    }

    if (match(Lexer::TokenType::LeftParen)) {
        auto expr = parseExpression();
        consume(Lexer::TokenType::RightParen, "Expected ')' after expression");
        return expr;
    }

    // Check for lambda expression: [ Lambda ... ]
    if (match(Lexer::TokenType::LeftBracket)) {
        if (check(Lexer::TokenType::Lambda)) {
            return parseLambda();
        }
        // Not a lambda - restore position
        current--;  // Put back the '['
    }

    error("Expected expression");
    return std::make_unique<IntegerLiteralExpr>(0, peek().location); // Error recovery
}

// Helper methods
std::unique_ptr<TypeRef> Parser::parseTypeRef() {
    auto loc = peek().location;
    std::string typeName;

    // Check for function type: F(ReturnType)(varName)(ParamTypes...)
    if (check(Lexer::TokenType::Identifier) && peek().lexeme == "F") {
        // Lookahead to see if this is a function type (F followed by '(')
        size_t savedPos = current;
        advance(); // consume F
        if (check(Lexer::TokenType::LeftParen)) {
            return parseFunctionTypeRef();
        }
        // Not a function type, restore position
        current = savedPos;
    }

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

    // ✅ Phase 7: Try to parse ownership FIRST (supports MyClass^<Integer> ordering)
    OwnershipType ownership = parseOwnershipType();
    bool ownershipParsed = (ownership != OwnershipType::None);

    // Parse template arguments if present (e.g., List<Integer>, Array<Integer, 10>)
    // Supports both @ and < as template argument delimiters
    // Also supports nested templates like Box<Box<Integer>> or HashMap<String, List<Integer>>
    std::vector<TemplateArgument> templateArgs;
    if (check(Lexer::TokenType::LeftAngle) || check(Lexer::TokenType::At)) {
        bool usingAtSyntax = check(Lexer::TokenType::At);
        advance(); // consume '<' or '@'

        do {
            auto argLoc = peek().location;

            // Check for wildcard '?'
            if (match(Lexer::TokenType::Question)) {
                templateArgs.push_back(TemplateArgument::Wildcard(argLoc));
            } else {
                // Try to determine if this is a type or value argument
                // Heuristic: if it starts with an identifier, check what follows
                // - If followed by ',', '>', '<', '@', '^', '&', '%' -> it's a type (possibly nested template)
                // - Otherwise, parse as expression
                bool isType = false;
                if (check(Lexer::TokenType::Identifier) || check(Lexer::TokenType::NativeType)) {
                    // Lookahead to determine if this is a type reference
                    size_t savedPos = current;

                    // Parse potential type (qualified identifier with possible template args)
                    if (check(Lexer::TokenType::NativeType)) {
                        // NativeType is always a type
                        isType = true;
                    } else {
                        parseQualifiedIdentifier(); // consume the identifier chain

                        // Check what follows - could indicate a type or nested template
                        if (check(Lexer::TokenType::Comma) ||
                            check(Lexer::TokenType::RightAngle) ||
                            check(Lexer::TokenType::LeftAngle) ||  // Nested template: Box<Box<Integer>>
                            check(Lexer::TokenType::At) ||          // Nested template: Box@Box<Integer>
                            check(Lexer::TokenType::Caret) ||       // Ownership: Integer^
                            check(Lexer::TokenType::Ampersand) ||   // Reference: Integer&
                            check(Lexer::TokenType::Percent)) {     // Copy: Integer%
                            isType = true;
                        }
                    }

                    // Restore position
                    current = savedPos;
                }

                if (isType) {
                    // Parse as nested type argument using recursive parseTypeRef
                    // This properly handles nested templates like Box<Box<Integer>>
                    auto nestedType = parseTypeRef();
                    // Convert TypeRef to string representation for TemplateArgument
                    std::string typeStr = nestedType->toString();
                    templateArgs.emplace_back(typeStr, argLoc);
                } else if (check(Lexer::TokenType::IntegerLiteral)) {
                    // Handle integer literal directly to avoid > being parsed as comparison
                    int64_t intVal = advance().intValue;
                    auto valueExpr = std::make_unique<IntegerLiteralExpr>(intVal, argLoc);
                    templateArgs.emplace_back(std::move(valueExpr), std::to_string(intVal), argLoc);
                } else if (check(Lexer::TokenType::LeftParen)) {
                    // For complex expressions, require parentheses: Box<(N+1)>
                    advance(); // consume '('
                    auto valueExpr = parseExpression();
                    consume(Lexer::TokenType::RightParen, "Expected ')' after template value expression");
                    templateArgs.emplace_back(std::move(valueExpr), argLoc);
                } else {
                    error("Expected type, integer literal, or parenthesized expression in template arguments");
                    break;
                }
            }

            if (!match(Lexer::TokenType::Comma)) {
                break;
            }
        } while (true);

        if (usingAtSyntax) {
            // @ syntax doesn't require closing delimiter
        } else {
            consume(Lexer::TokenType::RightAngle, "Expected '>' after template arguments");
        }
    }

    // ✅ Phase 7: If ownership wasn't parsed before template args, try parsing it now
    // This supports the old MyClass<Integer>^ ordering
    if (!ownershipParsed) {
        ownership = parseOwnershipType();
    }

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

// Parse a constraint reference with optional template arguments
// Supports: Hashable, Hashable<T>, Hashable@T
ConstraintRef Parser::parseConstraintRef() {
    auto loc = peek().location;
    std::string constraintName = parseQualifiedIdentifier();
    std::vector<std::string> templateArgs;

    // Check for template arguments: <T> or @T
    if (check(Lexer::TokenType::LeftAngle)) {
        advance();  // consume '<'
        do {
            if (check(Lexer::TokenType::Identifier)) {
                templateArgs.push_back(advance().lexeme);
            } else if (check(Lexer::TokenType::AngleBracketId)) {
                // Handle nested angle bracket identifier
                templateArgs.push_back(advance().stringValue);
            } else {
                error("Expected template argument in constraint");
                break;
            }
        } while (match(Lexer::TokenType::Comma));
        consume(Lexer::TokenType::RightAngle, "Expected '>' after constraint template arguments");
    } else if (check(Lexer::TokenType::AngleBracketId)) {
        // Lexer already tokenized <T> as AngleBracketId
        templateArgs.push_back(advance().stringValue);
    } else if (check(Lexer::TokenType::At)) {
        advance();  // consume '@'
        if (check(Lexer::TokenType::Identifier)) {
            templateArgs.push_back(advance().lexeme);
        } else {
            error("Expected template argument after '@' in constraint");
        }
    }

    return ConstraintRef(constraintName, templateArgs, loc);
}

std::vector<TemplateParameter> Parser::parseTemplateParameters() {
    std::vector<TemplateParameter> params;

    consume(Lexer::TokenType::LeftAngle, "Expected '<' for template parameters");

    // Parse template parameter list: <T Constrains Type1 | Type2> or <T Constrains (Type1, Type2)>
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

        // Parse constraints (None, (Type1, Type2) for AND, or Type1 | Type2 for OR)
        std::vector<ConstraintRef> constraints;
        bool constraintsAreAnd = true;

        if (match(Lexer::TokenType::None)) {
            // No constraints - any type allowed
            // Leave constraints empty, constraintsAreAnd doesn't matter
        } else if (match(Lexer::TokenType::LeftParen)) {
            // AND constraints: (Hashable<T>, Equatable<T>)
            constraintsAreAnd = true;
            do {
                constraints.push_back(parseConstraintRef());
            } while (match(Lexer::TokenType::Comma));
            consume(Lexer::TokenType::RightParen, "Expected ')' after constraint list");
        } else {
            // Single constraint or OR constraints: Type1 | Type2
            // OR semantics when multiple, single is effectively either
            constraintsAreAnd = false;
            do {
                constraints.push_back(parseConstraintRef());
                if (!match(Lexer::TokenType::Pipe)) {
                    break;
                }
            } while (true);

            // If only one constraint, it doesn't matter if AND or OR
            if (constraints.size() == 1) {
                constraintsAreAnd = true;
            }
        }

        params.emplace_back(paramName, constraints, constraintsAreAnd, paramLoc);

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

std::unique_ptr<Expression> Parser::parseTypeOfExpression() {
    auto loc = previous().location; // TypeOf token

    // Parse <Type> - the lexer treats <Type> as a single AngleBracketId token
    if (!check(Lexer::TokenType::AngleBracketId)) {
        error("Expected angle bracket identifier after 'TypeOf'");
        // Create a dummy type ref to avoid crash
        return std::make_unique<TypeOfExpr>(
            std::make_unique<TypeRef>("", OwnershipType::None, loc),
            loc
        );
    }

    std::string typeName = advance().stringValue; // Get the type name from angle bracket id

    // Create a type reference without ownership modifier (just the type name)
    auto typeRef = std::make_unique<TypeRef>(typeName, OwnershipType::None, loc);

    // Parse () - function call syntax
    consume(Lexer::TokenType::LeftParen, "Expected '(' after TypeOf<T>");
    consume(Lexer::TokenType::RightParen, "Expected ')' after '(' in TypeOf");

    return std::make_unique<TypeOfExpr>(std::move(typeRef), loc);
}

std::unique_ptr<Expression> Parser::parseLambda() {
    // Called after [ was consumed, positioned at Lambda keyword
    auto loc = peek().location;
    consume(Lexer::TokenType::Lambda, "Expected 'Lambda' after '['");

    // Parse optional capture list: [&refVar, ^ownedVar, %copyVar]
    // Each capture must specify ownership: & (reference), ^ (owned/move), % (copy)
    std::vector<LambdaExpr::CaptureSpec> captures;
    if (match(Lexer::TokenType::LeftBracket)) {
        if (!check(Lexer::TokenType::RightBracket)) {
            do {
                LambdaExpr::CaptureMode mode;
                if (match(Lexer::TokenType::Ampersand)) {
                    mode = LambdaExpr::CaptureMode::Reference;
                } else if (match(Lexer::TokenType::Caret)) {
                    mode = LambdaExpr::CaptureMode::Owned;
                } else if (match(Lexer::TokenType::Percent)) {
                    mode = LambdaExpr::CaptureMode::Copy;
                } else {
                    error("Expected capture mode (& for reference, ^ for owned, % for copy) before variable name");
                    break;
                }
                if (!check(Lexer::TokenType::Identifier)) {
                    error("Expected identifier after capture mode");
                    break;
                }
                std::string varName = advance().lexeme;
                captures.emplace_back(varName, mode);
            } while (match(Lexer::TokenType::Comma));
        }
        consume(Lexer::TokenType::RightBracket, "Expected ']' after capture list");
    }

    // Parse template parameters if present (using 'Templates' keyword)
    std::vector<TemplateParameter> templateParams;
    if (match(Lexer::TokenType::Templates)) {
        templateParams = parseTemplateParameters();
    }

    // Parse Returns TypeRef
    consume(Lexer::TokenType::Returns, "Expected 'Returns' in lambda");
    auto returnType = parseTypeRef();

    // Parse Parameters (...)
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

    // Parse optional Compiletime modifier
    bool isCompiletime = match(Lexer::TokenType::Compiletime);

    // Parse body { ... }
    auto body = parseBlock();

    // Consume closing ]
    consume(Lexer::TokenType::RightBracket, "Expected ']' after lambda body");

    auto lambda = std::make_unique<LambdaExpr>(
        std::move(captures),
        std::move(params),
        std::move(returnType),
        std::move(body),
        loc
    );
    lambda->isCompiletime = isCompiletime;
    lambda->templateParams = std::move(templateParams);
    return lambda;
}

std::unique_ptr<TypeRef> Parser::parseFunctionTypeRef() {
    // Parse F(ReturnType)(ParamTypes...)
    // Caller has already matched 'F' identifier
    auto loc = previous().location;

    // Parse (ReturnType)
    consume(Lexer::TokenType::LeftParen, "Expected '(' after 'F' for function type");
    auto returnType = parseTypeRef();
    consume(Lexer::TokenType::RightParen, "Expected ')' after return type");

    // Parse (ParamTypes...)
    std::vector<std::unique_ptr<TypeRef>> paramTypes;
    consume(Lexer::TokenType::LeftParen, "Expected '(' for parameter types in function type");
    if (!check(Lexer::TokenType::RightParen)) {
        do {
            paramTypes.push_back(parseTypeRef());
        } while (match(Lexer::TokenType::Comma));
    }
    consume(Lexer::TokenType::RightParen, "Expected ')' after parameter types");

    // Parse optional ownership suffix
    OwnershipType ownership = parseOwnershipType();
    if (ownership == OwnershipType::None) {
        ownership = OwnershipType::Owned; // Default to owned for function types
    }

    return std::make_unique<FunctionTypeRef>(
        std::move(returnType),
        std::move(paramTypes),
        ownership,
        loc
    );
}

} // namespace Parser
} // namespace XXML
