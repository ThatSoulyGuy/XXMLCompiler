#include "../../include/Derive/ASTSerializer.h"
#include <sstream>
#include <stdexcept>
#include <cmath>
#include <iomanip>
#include <cctype>

namespace XXML {
namespace Derive {

// ============================================================================
// String Utilities
// ============================================================================

std::string ASTSerializer::escapeString(const std::string& s) {
    std::ostringstream oss;
    for (char c : s) {
        switch (c) {
            case '"': oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n"; break;
            case '\r': oss << "\\r"; break;
            case '\t': oss << "\\t"; break;
            default:
                if (c >= 0 && c < 32) {
                    oss << "\\u" << std::hex << std::setfill('0') << std::setw(4) << (int)c;
                } else {
                    oss << c;
                }
        }
    }
    return oss.str();
}

std::string ASTSerializer::unescapeString(const std::string& s) {
    std::ostringstream oss;
    for (size_t i = 0; i < s.length(); ++i) {
        if (s[i] == '\\' && i + 1 < s.length()) {
            switch (s[i + 1]) {
                case '"': oss << '"'; ++i; break;
                case '\\': oss << '\\'; ++i; break;
                case 'n': oss << '\n'; ++i; break;
                case 'r': oss << '\r'; ++i; break;
                case 't': oss << '\t'; ++i; break;
                default: oss << s[i]; break;
            }
        } else {
            oss << s[i];
        }
    }
    return oss.str();
}

// ============================================================================
// JSON Parsing Utilities
// ============================================================================

void ASTSerializer::skipWhitespace(const std::string& json, size_t& pos) {
    while (pos < json.length() && std::isspace(json[pos])) {
        ++pos;
    }
}

std::string ASTSerializer::parseString(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    if (pos >= json.length() || json[pos] != '"') {
        throw std::runtime_error("Expected string at position " + std::to_string(pos));
    }
    ++pos; // skip opening quote

    std::ostringstream oss;
    while (pos < json.length() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.length()) {
            ++pos;
            switch (json[pos]) {
                case '"': oss << '"'; break;
                case '\\': oss << '\\'; break;
                case 'n': oss << '\n'; break;
                case 'r': oss << '\r'; break;
                case 't': oss << '\t'; break;
                default: oss << json[pos]; break;
            }
        } else {
            oss << json[pos];
        }
        ++pos;
    }

    if (pos >= json.length()) {
        throw std::runtime_error("Unterminated string");
    }
    ++pos; // skip closing quote
    return oss.str();
}

int64_t ASTSerializer::parseInt(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    size_t start = pos;
    if (pos < json.length() && json[pos] == '-') ++pos;
    while (pos < json.length() && std::isdigit(json[pos])) ++pos;
    return std::stoll(json.substr(start, pos - start));
}

double ASTSerializer::parseDouble(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    size_t start = pos;
    if (pos < json.length() && json[pos] == '-') ++pos;
    while (pos < json.length() && std::isdigit(json[pos])) ++pos;
    if (pos < json.length() && json[pos] == '.') {
        ++pos;
        while (pos < json.length() && std::isdigit(json[pos])) ++pos;
    }
    if (pos < json.length() && (json[pos] == 'e' || json[pos] == 'E')) {
        ++pos;
        if (pos < json.length() && (json[pos] == '+' || json[pos] == '-')) ++pos;
        while (pos < json.length() && std::isdigit(json[pos])) ++pos;
    }
    return std::stod(json.substr(start, pos - start));
}

bool ASTSerializer::parseBool(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    if (json.substr(pos, 4) == "true") {
        pos += 4;
        return true;
    } else if (json.substr(pos, 5) == "false") {
        pos += 5;
        return false;
    }
    throw std::runtime_error("Expected boolean at position " + std::to_string(pos));
}

bool ASTSerializer::parseNull(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    if (json.substr(pos, 4) == "null") {
        pos += 4;
        return true;
    }
    return false;
}

// ============================================================================
// Expression Serialization
// ============================================================================

std::string ASTSerializer::serializeTypeRef(const Parser::TypeRef* type) {
    if (!type) return "null";

    std::ostringstream oss;
    oss << "{\"kind\":\"TypeRef\"";
    oss << ",\"typeName\":\"" << escapeString(type->typeName) << "\"";
    oss << ",\"ownership\":" << static_cast<int>(type->ownership);

    if (!type->templateArgs.empty()) {
        oss << ",\"templateArgs\":[";
        for (size_t i = 0; i < type->templateArgs.size(); ++i) {
            if (i > 0) oss << ",";
            const auto& arg = type->templateArgs[i];
            oss << "{\"kind\":" << static_cast<int>(arg.kind);
            if (arg.kind == Parser::TemplateArgument::Kind::Type) {
                oss << ",\"typeArg\":\"" << escapeString(arg.typeArg) << "\"";
            } else if (arg.kind == Parser::TemplateArgument::Kind::Value) {
                oss << ",\"valueStr\":\"" << escapeString(arg.valueStr) << "\"";
                if (arg.valueArg) {
                    oss << ",\"valueArg\":" << serializeExprImpl(arg.valueArg.get());
                }
            }
            oss << "}";
        }
        oss << "]";
    }

    oss << "}";
    return oss.str();
}

std::string ASTSerializer::serializeExprImpl(const Parser::Expression* expr) {
    if (!expr) return "null";

    std::ostringstream oss;

    // Integer literal
    if (auto* intLit = dynamic_cast<const Parser::IntegerLiteralExpr*>(expr)) {
        oss << "{\"kind\":\"IntegerLiteral\",\"value\":" << intLit->value << "}";
    }
    // Float literal
    else if (auto* floatLit = dynamic_cast<const Parser::FloatLiteralExpr*>(expr)) {
        oss << "{\"kind\":\"FloatLiteral\",\"value\":" << floatLit->value << "}";
    }
    // Double literal
    else if (auto* dblLit = dynamic_cast<const Parser::DoubleLiteralExpr*>(expr)) {
        oss << "{\"kind\":\"DoubleLiteral\",\"value\":" << dblLit->value << "}";
    }
    // String literal
    else if (auto* strLit = dynamic_cast<const Parser::StringLiteralExpr*>(expr)) {
        oss << "{\"kind\":\"StringLiteral\",\"value\":\"" << escapeString(strLit->value) << "\"}";
    }
    // Bool literal
    else if (auto* boolLit = dynamic_cast<const Parser::BoolLiteralExpr*>(expr)) {
        oss << "{\"kind\":\"BoolLiteral\",\"value\":" << (boolLit->value ? "true" : "false") << "}";
    }
    // Identifier
    else if (auto* ident = dynamic_cast<const Parser::IdentifierExpr*>(expr)) {
        oss << "{\"kind\":\"Identifier\",\"name\":\"" << escapeString(ident->name) << "\"}";
    }
    // This
    else if (dynamic_cast<const Parser::ThisExpr*>(expr)) {
        oss << "{\"kind\":\"This\"}";
    }
    // Reference
    else if (auto* ref = dynamic_cast<const Parser::ReferenceExpr*>(expr)) {
        oss << "{\"kind\":\"Reference\"";
        oss << ",\"expr\":" << serializeExprImpl(ref->expr.get());
        oss << "}";
    }
    // Member access
    else if (auto* member = dynamic_cast<const Parser::MemberAccessExpr*>(expr)) {
        oss << "{\"kind\":\"MemberAccess\"";
        oss << ",\"object\":" << serializeExprImpl(member->object.get());
        oss << ",\"member\":\"" << escapeString(member->member) << "\"";
        oss << "}";
    }
    // Spliced member access - member name comes from a splice variable
    else if (auto* spliced = dynamic_cast<const Parser::SplicedMemberAccessExpr*>(expr)) {
        oss << "{\"kind\":\"SplicedMemberAccess\"";
        oss << ",\"object\":" << serializeExprImpl(spliced->object.get());
        oss << ",\"member\":{\"__SPLICE__\":\"" << escapeString(spliced->spliceName) << "\"";
        if (spliced->isSpread) {
            oss << ",\"spread\":true";
        }
        oss << "}}";
    }
    // Call
    else if (auto* call = dynamic_cast<const Parser::CallExpr*>(expr)) {
        oss << "{\"kind\":\"Call\"";
        oss << ",\"callee\":" << serializeExprImpl(call->callee.get());
        oss << ",\"arguments\":[";
        for (size_t i = 0; i < call->arguments.size(); ++i) {
            if (i > 0) oss << ",";
            oss << serializeExprImpl(call->arguments[i].get());
        }
        oss << "]";
        oss << "}";
    }
    // Binary
    else if (auto* binary = dynamic_cast<const Parser::BinaryExpr*>(expr)) {
        oss << "{\"kind\":\"Binary\"";
        oss << ",\"op\":\"" << escapeString(binary->op) << "\"";
        oss << ",\"left\":" << serializeExprImpl(binary->left.get());
        oss << ",\"right\":" << serializeExprImpl(binary->right.get());
        oss << "}";
    }
    // Splice placeholder
    // For serialization within Quote blocks, we emit a special marker format
    // that can be replaced at runtime with the actual splice value.
    // The marker is: {"__SPLICE__":"variableName","spread":bool}
    // This format is easily detectable and replaceable while being valid JSON.
    else if (auto* splice = dynamic_cast<const Parser::SplicePlaceholder*>(expr)) {
        oss << "{\"__SPLICE__\":\"" << escapeString(splice->variableName) << "\"";
        if (splice->isSpread) {
            oss << ",\"spread\":true";
        }
        oss << "}";
    }
    // Quote expression
    else if (auto* quote = dynamic_cast<const Parser::QuoteExpr*>(expr)) {
        oss << "{\"kind\":\"Quote\"";
        oss << ",\"quoteKind\":" << static_cast<int>(quote->kind);
        oss << ",\"templateNodes\":[";
        for (size_t i = 0; i < quote->templateNodes.size(); ++i) {
            if (i > 0) oss << ",";
            oss << serialize(quote->templateNodes[i].get());
        }
        oss << "]";
        oss << "}";
    }
    // TypeOf
    else if (auto* typeOf = dynamic_cast<const Parser::TypeOfExpr*>(expr)) {
        oss << "{\"kind\":\"TypeOf\"";
        oss << ",\"type\":" << serializeTypeRef(typeOf->type.get());
        oss << "}";
    }
    // Lambda
    else if (auto* lambda = dynamic_cast<const Parser::LambdaExpr*>(expr)) {
        oss << "{\"kind\":\"Lambda\"";
        oss << ",\"parameters\":[";
        for (size_t i = 0; i < lambda->parameters.size(); ++i) {
            if (i > 0) oss << ",";
            const auto& param = lambda->parameters[i];
            oss << "{\"name\":\"" << escapeString(param->name) << "\"";
            oss << ",\"type\":" << serializeTypeRef(param->type.get());
            oss << "}";
        }
        oss << "]";
        if (lambda->returnType) {
            oss << ",\"returnType\":" << serializeTypeRef(lambda->returnType.get());
        }
        oss << ",\"body\":[";
        for (size_t i = 0; i < lambda->body.size(); ++i) {
            if (i > 0) oss << ",";
            oss << serializeStmtImpl(lambda->body[i].get());
        }
        oss << "]";
        oss << "}";
    }
    else {
        oss << "{\"kind\":\"Unknown\"}";
    }

    return oss.str();
}

// ============================================================================
// Statement Serialization
// ============================================================================

std::string ASTSerializer::serializeStmtImpl(const Parser::Statement* stmt) {
    if (!stmt) return "null";

    std::ostringstream oss;

    // Instantiate
    if (auto* inst = dynamic_cast<const Parser::InstantiateStmt*>(stmt)) {
        oss << "{\"kind\":\"Instantiate\"";
        oss << ",\"type\":" << serializeTypeRef(inst->type.get());
        oss << ",\"name\":\"" << escapeString(inst->variableName) << "\"";
        oss << ",\"initializer\":" << serializeExprImpl(inst->initializer.get());
        oss << "}";
    }
    // Assignment
    else if (auto* assign = dynamic_cast<const Parser::AssignmentStmt*>(stmt)) {
        oss << "{\"kind\":\"Assignment\"";
        oss << ",\"target\":" << serializeExprImpl(assign->target.get());
        oss << ",\"value\":" << serializeExprImpl(assign->value.get());
        oss << "}";
    }
    // Run
    else if (auto* run = dynamic_cast<const Parser::RunStmt*>(stmt)) {
        oss << "{\"kind\":\"Run\"";
        oss << ",\"expression\":" << serializeExprImpl(run->expression.get());
        oss << "}";
    }
    // Return
    else if (auto* ret = dynamic_cast<const Parser::ReturnStmt*>(stmt)) {
        oss << "{\"kind\":\"Return\"";
        if (ret->value) {
            oss << ",\"value\":" << serializeExprImpl(ret->value.get());
        }
        oss << "}";
    }
    // If
    else if (auto* ifStmt = dynamic_cast<const Parser::IfStmt*>(stmt)) {
        oss << "{\"kind\":\"If\"";
        oss << ",\"condition\":" << serializeExprImpl(ifStmt->condition.get());
        oss << ",\"thenBranch\":[";
        for (size_t i = 0; i < ifStmt->thenBranch.size(); ++i) {
            if (i > 0) oss << ",";
            oss << serializeStmtImpl(ifStmt->thenBranch[i].get());
        }
        oss << "]";
        if (!ifStmt->elseBranch.empty()) {
            oss << ",\"elseBranch\":[";
            for (size_t i = 0; i < ifStmt->elseBranch.size(); ++i) {
                if (i > 0) oss << ",";
                oss << serializeStmtImpl(ifStmt->elseBranch[i].get());
            }
            oss << "]";
        }
        oss << "}";
    }
    // While
    else if (auto* whileStmt = dynamic_cast<const Parser::WhileStmt*>(stmt)) {
        oss << "{\"kind\":\"While\"";
        oss << ",\"condition\":" << serializeExprImpl(whileStmt->condition.get());
        oss << ",\"body\":[";
        for (size_t i = 0; i < whileStmt->body.size(); ++i) {
            if (i > 0) oss << ",";
            oss << serializeStmtImpl(whileStmt->body[i].get());
        }
        oss << "]";
        oss << "}";
    }
    // For
    else if (auto* forStmt = dynamic_cast<const Parser::ForStmt*>(stmt)) {
        oss << "{\"kind\":\"For\"";
        oss << ",\"variable\":\"" << escapeString(forStmt->iteratorName) << "\"";
        oss << ",\"start\":" << serializeExprImpl(forStmt->rangeStart.get());
        oss << ",\"end\":" << serializeExprImpl(forStmt->rangeEnd.get());
        oss << ",\"isCStyle\":" << (forStmt->isCStyleLoop ? "true" : "false");
        if (forStmt->iteratorType) {
            oss << ",\"iteratorType\":" << serializeTypeRef(forStmt->iteratorType.get());
        }
        oss << ",\"body\":[";
        for (size_t i = 0; i < forStmt->body.size(); ++i) {
            if (i > 0) oss << ",";
            oss << serializeStmtImpl(forStmt->body[i].get());
        }
        oss << "]";
        oss << "}";
    }
    // Break
    else if (dynamic_cast<const Parser::BreakStmt*>(stmt)) {
        oss << "{\"kind\":\"Break\"}";
    }
    // Continue
    else if (dynamic_cast<const Parser::ContinueStmt*>(stmt)) {
        oss << "{\"kind\":\"Continue\"}";
    }
    // Exit
    else if (auto* exit = dynamic_cast<const Parser::ExitStmt*>(stmt)) {
        oss << "{\"kind\":\"Exit\"";
        oss << ",\"code\":" << serializeExprImpl(exit->exitCode.get());
        oss << "}";
    }
    else {
        oss << "{\"kind\":\"Unknown\"}";
    }

    return oss.str();
}

// ============================================================================
// Public API - Serialization
// ============================================================================

std::string ASTSerializer::serialize(const Parser::ASTNode* node) {
    if (!node) return "null";

    // Try as expression first
    if (auto* expr = dynamic_cast<const Parser::Expression*>(node)) {
        return serializeExprImpl(expr);
    }
    // Try as statement
    if (auto* stmt = dynamic_cast<const Parser::Statement*>(node)) {
        return serializeStmtImpl(stmt);
    }

    return "{\"kind\":\"Unknown\"}";
}

std::string ASTSerializer::serializeExpr(const Parser::Expression* expr) {
    return serializeExprImpl(expr);
}

std::string ASTSerializer::serializeStmt(const Parser::Statement* stmt) {
    return serializeStmtImpl(stmt);
}

std::string ASTSerializer::serializeStatements(const std::vector<std::unique_ptr<Parser::Statement>>& stmts) {
    std::ostringstream oss;
    oss << "[";
    for (size_t i = 0; i < stmts.size(); ++i) {
        if (i > 0) oss << ",";
        oss << serializeStmtImpl(stmts[i].get());
    }
    oss << "]";
    return oss.str();
}

// ============================================================================
// Type Reference Deserialization
// ============================================================================

std::unique_ptr<Parser::TypeRef> ASTSerializer::deserializeTypeRef(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    if (parseNull(json, pos)) return nullptr;

    if (json[pos] != '{') {
        throw std::runtime_error("Expected object for TypeRef");
    }
    ++pos;

    std::string typeName;
    Parser::OwnershipType ownership = Parser::OwnershipType::None;
    std::vector<Parser::TemplateArgument> templateArgs;
    Common::SourceLocation loc;

    while (pos < json.length() && json[pos] != '}') {
        skipWhitespace(json, pos);
        std::string key = parseString(json, pos);
        skipWhitespace(json, pos);
        if (json[pos] != ':') throw std::runtime_error("Expected ':'");
        ++pos;
        skipWhitespace(json, pos);

        if (key == "kind") {
            parseString(json, pos); // skip "TypeRef"
        } else if (key == "typeName") {
            typeName = parseString(json, pos);
        } else if (key == "ownership") {
            ownership = static_cast<Parser::OwnershipType>(parseInt(json, pos));
        } else if (key == "templateArgs") {
            if (json[pos] != '[') throw std::runtime_error("Expected array");
            ++pos;
            while (pos < json.length() && json[pos] != ']') {
                skipWhitespace(json, pos);
                // Parse TemplateArgument object
                if (json[pos] == '{') {
                    ++pos;
                    Parser::TemplateArgument::Kind argKind = Parser::TemplateArgument::Kind::Type;
                    std::string typeArg;
                    std::string valueStr;

                    while (pos < json.length() && json[pos] != '}') {
                        skipWhitespace(json, pos);
                        std::string argKey = parseString(json, pos);
                        skipWhitespace(json, pos);
                        if (json[pos] != ':') throw std::runtime_error("Expected ':'");
                        ++pos;
                        skipWhitespace(json, pos);

                        if (argKey == "kind") {
                            argKind = static_cast<Parser::TemplateArgument::Kind>(parseInt(json, pos));
                        } else if (argKey == "typeArg") {
                            typeArg = parseString(json, pos);
                        } else if (argKey == "valueStr") {
                            valueStr = parseString(json, pos);
                        } else if (argKey == "valueArg") {
                            // Skip for now - complex expression parsing
                            int depth = 0;
                            while (pos < json.length()) {
                                if (json[pos] == '{' || json[pos] == '[') ++depth;
                                else if (json[pos] == '}' || json[pos] == ']') {
                                    if (depth == 0) break;
                                    --depth;
                                }
                                else if (json[pos] == ',' && depth == 0) break;
                                ++pos;
                            }
                        }

                        skipWhitespace(json, pos);
                        if (json[pos] == ',') ++pos;
                    }
                    if (json[pos] == '}') ++pos;

                    if (argKind == Parser::TemplateArgument::Kind::Type) {
                        templateArgs.emplace_back(typeArg, loc);
                    } else if (argKind == Parser::TemplateArgument::Kind::Wildcard) {
                        templateArgs.push_back(Parser::TemplateArgument::Wildcard(loc));
                    }
                    // Value arguments are more complex - skip for now
                }
                skipWhitespace(json, pos);
                if (json[pos] == ',') ++pos;
            }
            if (json[pos] == ']') ++pos;
        }

        skipWhitespace(json, pos);
        if (json[pos] == ',') ++pos;
    }
    if (json[pos] == '}') ++pos;

    return std::make_unique<Parser::TypeRef>(typeName, std::move(templateArgs), ownership, loc);
}

// ============================================================================
// Expression Deserialization
// ============================================================================

std::unique_ptr<Parser::Expression> ASTSerializer::deserializeExprImpl(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    if (parseNull(json, pos)) return nullptr;

    if (json[pos] != '{') {
        throw std::runtime_error("Expected object for Expression at position " + std::to_string(pos));
    }
    ++pos;

    // First, find the kind or check for __SPLICE__ marker format
    std::string kind;
    size_t savedPos = pos;

    while (pos < json.length() && json[pos] != '}') {
        skipWhitespace(json, pos);
        std::string key = parseString(json, pos);
        skipWhitespace(json, pos);
        if (json[pos] != ':') throw std::runtime_error("Expected ':'");
        ++pos;
        skipWhitespace(json, pos);

        // Check for __SPLICE__ marker format: {"__SPLICE__":"varName","spread":bool}
        if (key == "__SPLICE__") {
            std::string variableName = parseString(json, pos);
            bool isSpread = false;

            // Check for optional "spread" field
            skipWhitespace(json, pos);
            if (json[pos] == ',') {
                ++pos;
                skipWhitespace(json, pos);
                std::string spreadKey = parseString(json, pos);
                skipWhitespace(json, pos);
                if (json[pos] == ':') {
                    ++pos;
                    skipWhitespace(json, pos);
                    if (spreadKey == "spread") {
                        isSpread = parseBool(json, pos);
                    }
                }
            }

            skipWhitespace(json, pos);
            if (json[pos] == '}') ++pos;
            Common::SourceLocation loc;
            return std::make_unique<Parser::SplicePlaceholder>(variableName, isSpread, false, loc);
        }
        else if (key == "kind") {
            kind = parseString(json, pos);
            break;
        } else {
            // Skip value
            int depth = 0;
            bool inString = false;
            while (pos < json.length()) {
                if (!inString && json[pos] == '"') inString = true;
                else if (inString && json[pos] == '"' && json[pos-1] != '\\') inString = false;
                else if (!inString) {
                    if (json[pos] == '{' || json[pos] == '[') ++depth;
                    else if (json[pos] == '}' || json[pos] == ']') {
                        if (depth == 0) break;
                        --depth;
                    }
                    else if (json[pos] == ',' && depth == 0) break;
                }
                ++pos;
            }
        }

        skipWhitespace(json, pos);
        if (json[pos] == ',') ++pos;
    }

    // Reset and parse full object with knowledge of kind
    pos = savedPos;

    Common::SourceLocation loc;

    if (kind == "IntegerLiteral") {
        int64_t value = 0;
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "value") {
                value = parseInt(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::IntegerLiteralExpr>(value, loc);
    }
    else if (kind == "FloatLiteral") {
        float value = 0;
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "value") {
                value = static_cast<float>(parseDouble(json, pos));
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::FloatLiteralExpr>(value, loc);
    }
    else if (kind == "DoubleLiteral") {
        double value = 0;
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "value") {
                value = parseDouble(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::DoubleLiteralExpr>(value, loc);
    }
    else if (kind == "StringLiteral") {
        std::string value;
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "value") {
                value = parseString(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::StringLiteralExpr>(value, loc);
    }
    else if (kind == "BoolLiteral") {
        bool value = false;
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "value") {
                value = parseBool(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::BoolLiteralExpr>(value, loc);
    }
    else if (kind == "Identifier") {
        std::string name;
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "name") {
                name = parseString(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::IdentifierExpr>(name, loc);
    }
    else if (kind == "This") {
        // Skip to end of object
        while (pos < json.length() && json[pos] != '}') ++pos;
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::ThisExpr>(loc);
    }
    else if (kind == "Reference") {
        std::unique_ptr<Parser::Expression> expr;
        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "expr") {
                expr = deserializeExprImpl(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::ReferenceExpr>(std::move(expr), loc);
    }
    else if (kind == "MemberAccess") {
        std::unique_ptr<Parser::Expression> object;
        std::string member;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "object") {
                object = deserializeExprImpl(json, pos);
            } else if (key == "member") {
                member = parseString(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::MemberAccessExpr>(std::move(object), member, loc);
    }
    else if (kind == "SplicedMemberAccess") {
        std::unique_ptr<Parser::Expression> object;
        std::string spliceName;
        bool isSpread = false;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "object") {
                object = deserializeExprImpl(json, pos);
            } else if (key == "member") {
                // Member is a splice object: {"__SPLICE__":"name","spread":bool}
                skipWhitespace(json, pos);
                if (json[pos] != '{') throw std::runtime_error("Expected '{' for splice member");
                ++pos;
                while (pos < json.length() && json[pos] != '}') {
                    skipWhitespace(json, pos);
                    std::string spliceKey = parseString(json, pos);
                    skipWhitespace(json, pos);
                    if (json[pos] != ':') throw std::runtime_error("Expected ':'");
                    ++pos;
                    skipWhitespace(json, pos);
                    if (spliceKey == "__SPLICE__") {
                        spliceName = parseString(json, pos);
                    } else if (spliceKey == "spread") {
                        isSpread = parseBool(json, pos);
                    }
                    skipWhitespace(json, pos);
                    if (json[pos] == ',') ++pos;
                }
                if (json[pos] == '}') ++pos;
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::SplicedMemberAccessExpr>(std::move(object), spliceName, isSpread, loc);
    }
    else if (kind == "Call") {
        std::unique_ptr<Parser::Expression> callee;
        std::vector<std::unique_ptr<Parser::Expression>> arguments;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "callee") {
                callee = deserializeExprImpl(json, pos);
            } else if (key == "arguments") {
                if (json[pos] != '[') throw std::runtime_error("Expected array");
                ++pos;
                while (pos < json.length() && json[pos] != ']') {
                    skipWhitespace(json, pos);
                    arguments.push_back(deserializeExprImpl(json, pos));
                    skipWhitespace(json, pos);
                    if (json[pos] == ',') ++pos;
                }
                if (json[pos] == ']') ++pos;
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;

        return std::make_unique<Parser::CallExpr>(std::move(callee), std::move(arguments), loc);
    }
    else if (kind == "Binary") {
        std::string op;
        std::unique_ptr<Parser::Expression> left;
        std::unique_ptr<Parser::Expression> right;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "op") {
                op = parseString(json, pos);
            } else if (key == "left") {
                left = deserializeExprImpl(json, pos);
            } else if (key == "right") {
                right = deserializeExprImpl(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::BinaryExpr>(std::move(left), op, std::move(right), loc);
    }
    else if (kind == "SplicePlaceholder") {
        std::string variableName;
        bool isSpread = false;
        bool isBraced = false;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "variableName") {
                variableName = parseString(json, pos);
            } else if (key == "isSpread") {
                isSpread = parseBool(json, pos);
            } else if (key == "isBraced") {
                isBraced = parseBool(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::SplicePlaceholder>(variableName, isSpread, isBraced, loc);
    }

    // Skip unknown kind
    int depth = 1;
    while (pos < json.length() && depth > 0) {
        if (json[pos] == '{') ++depth;
        else if (json[pos] == '}') --depth;
        ++pos;
    }

    return nullptr;
}

// ============================================================================
// Statement Deserialization
// ============================================================================

std::unique_ptr<Parser::Statement> ASTSerializer::deserializeStmtImpl(const std::string& json, size_t& pos) {
    skipWhitespace(json, pos);
    if (parseNull(json, pos)) return nullptr;

    if (json[pos] != '{') {
        throw std::runtime_error("Expected object for Statement");
    }
    ++pos;

    // Find kind first
    std::string kind;
    size_t savedPos = pos;

    while (pos < json.length() && json[pos] != '}') {
        skipWhitespace(json, pos);
        std::string key = parseString(json, pos);
        skipWhitespace(json, pos);
        if (json[pos] != ':') throw std::runtime_error("Expected ':'");
        ++pos;
        skipWhitespace(json, pos);

        if (key == "kind") {
            kind = parseString(json, pos);
            break;
        } else {
            // Skip value
            int depth = 0;
            bool inString = false;
            while (pos < json.length()) {
                if (!inString && json[pos] == '"') inString = true;
                else if (inString && json[pos] == '"' && json[pos-1] != '\\') inString = false;
                else if (!inString) {
                    if (json[pos] == '{' || json[pos] == '[') ++depth;
                    else if (json[pos] == '}' || json[pos] == ']') {
                        if (depth == 0) break;
                        --depth;
                    }
                    else if (json[pos] == ',' && depth == 0) break;
                }
                ++pos;
            }
        }

        skipWhitespace(json, pos);
        if (json[pos] == ',') ++pos;
    }

    pos = savedPos;
    Common::SourceLocation loc;

    if (kind == "Return") {
        std::unique_ptr<Parser::Expression> value;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "value") {
                value = deserializeExprImpl(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::ReturnStmt>(std::move(value), loc);
    }
    else if (kind == "Assignment") {
        std::unique_ptr<Parser::Expression> target;
        std::unique_ptr<Parser::Expression> value;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "target") {
                target = deserializeExprImpl(json, pos);
            } else if (key == "value") {
                value = deserializeExprImpl(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::AssignmentStmt>(std::move(target), std::move(value), loc);
    }
    else if (kind == "Run") {
        std::unique_ptr<Parser::Expression> expression;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "expression") {
                expression = deserializeExprImpl(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::RunStmt>(std::move(expression), loc);
    }
    else if (kind == "Instantiate") {
        std::unique_ptr<Parser::TypeRef> type;
        std::string name;
        std::unique_ptr<Parser::Expression> initializer;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "type") {
                type = deserializeTypeRef(json, pos);
            } else if (key == "name") {
                name = parseString(json, pos);
            } else if (key == "initializer") {
                initializer = deserializeExprImpl(json, pos);
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::InstantiateStmt>(std::move(type), name, std::move(initializer), loc);
    }
    else if (kind == "If") {
        std::unique_ptr<Parser::Expression> condition;
        std::vector<std::unique_ptr<Parser::Statement>> thenBlock;
        std::vector<std::unique_ptr<Parser::Statement>> elseBlock;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "condition") {
                condition = deserializeExprImpl(json, pos);
            } else if (key == "thenBlock") {
                if (json[pos] != '[') throw std::runtime_error("Expected array");
                ++pos;
                while (pos < json.length() && json[pos] != ']') {
                    skipWhitespace(json, pos);
                    thenBlock.push_back(deserializeStmtImpl(json, pos));
                    skipWhitespace(json, pos);
                    if (json[pos] == ',') ++pos;
                }
                if (json[pos] == ']') ++pos;
            } else if (key == "elseBlock") {
                if (json[pos] != '[') throw std::runtime_error("Expected array");
                ++pos;
                while (pos < json.length() && json[pos] != ']') {
                    skipWhitespace(json, pos);
                    elseBlock.push_back(deserializeStmtImpl(json, pos));
                    skipWhitespace(json, pos);
                    if (json[pos] == ',') ++pos;
                }
                if (json[pos] == ']') ++pos;
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::IfStmt>(std::move(condition), std::move(thenBlock), std::move(elseBlock), loc);
    }
    else if (kind == "While") {
        std::unique_ptr<Parser::Expression> condition;
        std::vector<std::unique_ptr<Parser::Statement>> body;

        while (pos < json.length() && json[pos] != '}') {
            skipWhitespace(json, pos);
            std::string key = parseString(json, pos);
            skipWhitespace(json, pos);
            if (json[pos] != ':') throw std::runtime_error("Expected ':'");
            ++pos;
            skipWhitespace(json, pos);

            if (key == "condition") {
                condition = deserializeExprImpl(json, pos);
            } else if (key == "body") {
                if (json[pos] != '[') throw std::runtime_error("Expected array");
                ++pos;
                while (pos < json.length() && json[pos] != ']') {
                    skipWhitespace(json, pos);
                    body.push_back(deserializeStmtImpl(json, pos));
                    skipWhitespace(json, pos);
                    if (json[pos] == ',') ++pos;
                }
                if (json[pos] == ']') ++pos;
            } else if (key == "kind") {
                parseString(json, pos);
            }

            skipWhitespace(json, pos);
            if (json[pos] == ',') ++pos;
        }
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::WhileStmt>(std::move(condition), std::move(body), loc);
    }
    else if (kind == "Break") {
        while (pos < json.length() && json[pos] != '}') ++pos;
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::BreakStmt>(loc);
    }
    else if (kind == "Continue") {
        while (pos < json.length() && json[pos] != '}') ++pos;
        if (json[pos] == '}') ++pos;
        return std::make_unique<Parser::ContinueStmt>(loc);
    }

    // Skip unknown kind
    int depth = 1;
    while (pos < json.length() && depth > 0) {
        if (json[pos] == '{') ++depth;
        else if (json[pos] == '}') --depth;
        ++pos;
    }

    return nullptr;
}

// ============================================================================
// Public API - Deserialization
// ============================================================================

std::unique_ptr<Parser::ASTNode> ASTSerializer::deserialize(const std::string& json) {
    size_t pos = 0;
    // Try as expression first, then statement
    try {
        return deserializeExprImpl(json, pos);
    } catch (...) {
        pos = 0;
        return deserializeStmtImpl(json, pos);
    }
}

std::unique_ptr<Parser::Expression> ASTSerializer::deserializeExpr(const std::string& json) {
    size_t pos = 0;
    return deserializeExprImpl(json, pos);
}

std::unique_ptr<Parser::Statement> ASTSerializer::deserializeStmt(const std::string& json) {
    size_t pos = 0;
    return deserializeStmtImpl(json, pos);
}

std::vector<std::unique_ptr<Parser::Statement>> ASTSerializer::deserializeStatements(const std::string& json) {
    std::vector<std::unique_ptr<Parser::Statement>> result;
    size_t pos = 0;
    skipWhitespace(json, pos);

    if (pos >= json.length() || json[pos] != '[') {
        throw std::runtime_error("Expected array for statements");
    }
    ++pos;

    while (pos < json.length() && json[pos] != ']') {
        skipWhitespace(json, pos);
        result.push_back(deserializeStmtImpl(json, pos));
        skipWhitespace(json, pos);
        if (json[pos] == ',') ++pos;
    }

    return result;
}

} // namespace Derive
} // namespace XXML
