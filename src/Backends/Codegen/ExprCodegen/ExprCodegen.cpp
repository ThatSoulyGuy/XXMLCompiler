#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"
#include "Backends/TypeNormalizer.h"
#include "Backends/NameMangler.h"
#include "Semantic/SemanticError.h"
#include "Semantic/CompiletimeInterpreter.h"
#include "Derive/ASTSerializer.h"
#include <unordered_set>
#include <iostream>
#include <sstream>

namespace XXML {
namespace Backends {
namespace Codegen {

LLVMIR::AnyValue ExprCodegen::generate(Parser::Expression* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }

    // Try compile-time constant folding first
    if (auto foldedValue = tryCompiletimeFold(expr)) {
        return *foldedValue;
    }

    // Dispatch based on expression type
    if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) {
        return visitIntegerLiteral(intLit);
    }
    if (auto* floatLit = dynamic_cast<Parser::FloatLiteralExpr*>(expr)) {
        return visitFloatLiteral(floatLit);
    }
    if (auto* doubleLit = dynamic_cast<Parser::DoubleLiteralExpr*>(expr)) {
        return visitDoubleLiteral(doubleLit);
    }
    if (auto* stringLit = dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
        return visitStringLiteral(stringLit);
    }
    if (auto* boolLit = dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
        return visitBoolLiteral(boolLit);
    }
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        return visitIdentifier(ident);
    }
    if (auto* thisExpr = dynamic_cast<Parser::ThisExpr*>(expr)) {
        return visitThis(thisExpr);
    }
    if (auto* refExpr = dynamic_cast<Parser::ReferenceExpr*>(expr)) {
        return visitReference(refExpr);
    }
    if (auto* binExpr = dynamic_cast<Parser::BinaryExpr*>(expr)) {
        return visitBinary(binExpr);
    }
    if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(expr)) {
        return visitMemberAccess(memberExpr);
    }
    if (auto* splicedMember = dynamic_cast<Parser::SplicedMemberAccessExpr*>(expr)) {
        // SplicedMemberAccessExpr should only appear in Quote blocks where it's serialized,
        // not directly evaluated. If we get here, it's an error.
        throw Semantic::CodegenInvariantViolation("INVALID_SPLICED_MEMBER",
            "SplicedMemberAccessExpr encountered outside Quote context");
    }
    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        return visitCall(callExpr);
    }
    if (auto* lambdaExpr = dynamic_cast<Parser::LambdaExpr*>(expr)) {
        return visitLambda(lambdaExpr);
    }
    if (auto* typeofExpr = dynamic_cast<Parser::TypeOfExpr*>(expr)) {
        return visitTypeOf(typeofExpr);
    }
    if (auto* quoteExpr = dynamic_cast<Parser::QuoteExpr*>(expr)) {
        return visitQuote(quoteExpr);
    }
    if (auto* spliceExpr = dynamic_cast<Parser::SplicePlaceholder*>(expr)) {
        return visitSplicePlaceholder(spliceExpr);
    }

    // Unknown expression type - return null
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

// === Utility Methods ===

LLVMIR::AnyValue ExprCodegen::loadIfNeeded(LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca) {
    if (alloca && value.isPtr()) {
        auto loadedValue = ctx_.builder().createLoad(
            ctx_.module().getContext().getPtrTy(),
            value.asPtr(),
            "load"
        );
        return LLVMIR::AnyValue(loadedValue);
    }
    return value;
}

std::string ExprCodegen::getExpressionType(Parser::Expression* expr) const {
    if (!expr) return "Unknown";

    // First check if semantic analysis populated the type (preferred)
    // This is set by SemanticAnalyzer::registerExpressionType()
    if (!expr->resolvedType.empty()) {
        return expr->resolvedType;
    }

    // Fallback for literals (should already have resolvedType set, but just in case)
    if (dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) return "Integer";
    if (dynamic_cast<Parser::FloatLiteralExpr*>(expr)) return "Float";
    if (dynamic_cast<Parser::DoubleLiteralExpr*>(expr)) return "Double";
    if (dynamic_cast<Parser::StringLiteralExpr*>(expr)) return "String";
    if (dynamic_cast<Parser::BoolLiteralExpr*>(expr)) return "Bool";

    // Identifiers - look up in context
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr)) {
        if (auto* varInfo = ctx_.getVariable(ident->name)) {
            return varInfo->xxmlType;
        }
    }

    // No type information available
    return "Unknown";
}

bool ExprCodegen::isNumericType(std::string_view type) const {
    return isIntegerType(type) || isFloatType(type);
}

bool ExprCodegen::isIntegerType(std::string_view type) const {
    return type == "Integer" || type == "Int" || type == "Int64" ||
           type == "Int32" || type == "Int16" || type == "Int8" ||
           type == "Byte" || type == "Bool";
}

bool ExprCodegen::isFloatType(std::string_view type) const {
    return type == "Float" || type == "Double";
}

// === Literal Implementations ===

LLVMIR::AnyValue ExprCodegen::visitIntegerLiteral(Parser::IntegerLiteralExpr* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(ctx_.builder().getInt64(0));
    }
    auto intValue = ctx_.builder().getInt64(expr->value);
    ctx_.lastExprValue = LLVMIR::AnyValue(intValue);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::visitFloatLiteral(Parser::FloatLiteralExpr* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(ctx_.builder().getFloat(0.0f));
    }
    auto floatValue = ctx_.builder().getFloat(expr->value);
    ctx_.lastExprValue = LLVMIR::AnyValue(floatValue);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::visitDoubleLiteral(Parser::DoubleLiteralExpr* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(ctx_.builder().getDouble(0.0));
    }
    auto doubleValue = ctx_.builder().getDouble(expr->value);
    ctx_.lastExprValue = LLVMIR::AnyValue(doubleValue);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::visitStringLiteral(Parser::StringLiteralExpr* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }
    std::string label = ctx_.allocateStringLabel();
    ctx_.addStringLiteral(label, expr->value);
    auto* globalStr = ctx_.module().getOrCreateStringLiteral(expr->value);
    auto ptrValue = globalStr->toTypedValue();
    ctx_.lastExprValue = LLVMIR::AnyValue(ptrValue);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::visitBoolLiteral(Parser::BoolLiteralExpr* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(ctx_.builder().getInt1(false));
    }
    auto boolValue = ctx_.builder().getInt1(expr->value);
    ctx_.lastExprValue = LLVMIR::AnyValue(boolValue);
    return ctx_.lastExprValue;
}

// === Identifier Implementations ===

LLVMIR::AnyValue ExprCodegen::visitIdentifier(Parser::IdentifierExpr* expr) {
    if (!expr) {
        throw Semantic::CodegenInvariantViolation("NULL_IDENTIFIER",
            "IdentifierExpr is null");
    }

    const std::string& name = expr->name;

    // Check if it's a local variable
    if (auto* varInfo = ctx_.getVariable(name)) {
        if (varInfo->alloca) {
            auto* loadType = ctx_.mapType(varInfo->xxmlType);
            auto loaded = ctx_.builder().createLoad(
                loadType,
                LLVMIR::PtrValue(varInfo->alloca),
                name + ".load"
            );
            ctx_.lastExprValue = loaded;
            return ctx_.lastExprValue;
        }
        ctx_.lastExprValue = varInfo->value;
        return ctx_.lastExprValue;
    }

    // Check if it's an enum value
    if (ctx_.hasEnumValue(name)) {
        int64_t value = ctx_.getEnumValue(name);
        auto intValue = ctx_.builder().getInt64(value);
        ctx_.lastExprValue = LLVMIR::AnyValue(intValue);
        return ctx_.lastExprValue;
    }

    // Check if it's a property of the current class
    if (!ctx_.currentClassName().empty()) {
        auto* classInfo = ctx_.getClass(std::string(ctx_.currentClassName()));
        if (classInfo) {
            for (const auto& prop : classInfo->properties) {
                if (prop.name == name) {
                    return loadPropertyFromThis(prop);
                }
            }
        }
    }

    // Unknown identifier
    throw Semantic::UnresolvedIdentifierError(name);
}

LLVMIR::AnyValue ExprCodegen::visitThis(Parser::ThisExpr*) {
    auto* func = ctx_.currentFunction();
    if (!func) {
        throw Semantic::CodegenInvariantViolation("THIS_NO_FUNCTION",
            "'this' used outside of a function");
    }
    if (func->getNumParams() == 0) {
        throw Semantic::CodegenInvariantViolation("THIS_NO_PARAMS",
            "'this' used in function with no parameters (not a method?)");
    }

    auto* thisArg = func->getArg(0);
    if (!thisArg) {
        throw Semantic::CodegenInvariantViolation("THIS_NULL_ARG",
            "'this' argument is null");
    }

    auto ptrValue = LLVMIR::PtrValue(thisArg);
    ctx_.lastExprValue = LLVMIR::AnyValue(ptrValue);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::visitReference(Parser::ReferenceExpr* expr) {
    if (!expr || !expr->expr) {
        throw Semantic::CodegenInvariantViolation("NULL_REFERENCE",
            "ReferenceExpr is null or has null inner expression");
    }

    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr->expr.get())) {
        // First check if it's a local variable
        if (auto* varInfo = ctx_.getVariable(ident->name)) {
            if (varInfo->alloca) {
                auto ptrValue = LLVMIR::PtrValue(varInfo->alloca);
                ctx_.lastExprValue = LLVMIR::AnyValue(ptrValue);
                return ctx_.lastExprValue;
            }
        }

        // Check if it's a property on 'this' - return the address without loading
        auto* func = ctx_.currentFunction();
        if (func && func->getNumParams() > 0) {
            std::string currentClass = std::string(ctx_.currentClassName());
            auto* classInfo = ctx_.getClass(currentClass);
            if (classInfo && classInfo->structType) {
                for (const auto& prop : classInfo->properties) {
                    if (prop.name == ident->name) {
                        // Found the property - return its address (GEP) without loading
                        auto* thisArg = func->getArg(0);
                        auto thisPtrValue = LLVMIR::PtrValue(thisArg);
                        auto propPtr = ctx_.builder().createStructGEP(
                            classInfo->structType,
                            thisPtrValue,
                            static_cast<unsigned>(prop.index),
                            ident->name + ".ptr"
                        );
                        ctx_.lastExprValue = LLVMIR::AnyValue(propPtr);
                        return ctx_.lastExprValue;
                    }
                }
            }
        }
    }

    return generate(expr->expr.get());
}

// === Binary Expression Implementation ===

// Helper to unwrap boxed types (Integer, Float, etc.) to their raw values
// These wrapper types have their value field at offset 0, so we can directly load
LLVMIR::AnyValue ExprCodegen::unwrapBoxedType(LLVMIR::AnyValue value, const std::string& type) {
    std::string baseType = TypeNormalizer::stripOwnershipMarker(type);

    // Only unwrap if it's a pointer to a boxed type
    if (!value.isPtr()) {
        return value;  // Already unwrapped
    }

    // Integer -> i64 (value field at offset 0)
    if (baseType == "Integer" || baseType == "Language::Core::Integer") {
        auto* intTy = ctx_.builder().getInt64Ty();
        // The Integer struct has i64 as its first (and only) field
        // So we can directly load an i64 from the pointer
        auto loaded = ctx_.builder().createLoad(intTy, value.asPtr(), "int_value");
        return loaded;
    }

    // Float -> float (value field at offset 0)
    if (baseType == "Float" || baseType == "Language::Core::Float") {
        auto* floatTy = ctx_.builder().getFloatTy();
        auto loaded = ctx_.builder().createLoad(floatTy, value.asPtr(), "float_value");
        return loaded;
    }

    // Double -> double (value field at offset 0)
    if (baseType == "Double" || baseType == "Language::Core::Double") {
        auto* doubleTy = ctx_.builder().getDoubleTy();
        auto loaded = ctx_.builder().createLoad(doubleTy, value.asPtr(), "double_value");
        return loaded;
    }

    return value;  // Not a known boxed type
}

LLVMIR::AnyValue ExprCodegen::visitBinary(Parser::BinaryExpr* expr) {
    if (!expr || !expr->left || !expr->right) {
        return LLVMIR::AnyValue(ctx_.builder().getInt64(0));
    }

    // Evaluate left operand
    auto leftResult = generate(expr->left.get());

    // If left operand returns a reference (T&), load to get the actual value
    if (expr->left->resolvedOwnership == Parser::OwnershipType::Reference && leftResult.isPtr()) {
        leftResult = LLVMIR::AnyValue(ctx_.builder().createLoad(
            ctx_.builder().getPtrTy(), leftResult.asPtr(), "ref.load.left"));
    }

    // Short-circuit for logical operators
    if (expr->op == "&&" || expr->op == "and") {
        return generateLogicalAnd(expr, leftResult);
    }
    if (expr->op == "||" || expr->op == "or") {
        return generateLogicalOr(expr, leftResult);
    }

    // Evaluate right operand
    auto rightResult = generate(expr->right.get());

    // If right operand returns a reference (T&), load to get the actual value
    if (expr->right->resolvedOwnership == Parser::OwnershipType::Reference && rightResult.isPtr()) {
        rightResult = LLVMIR::AnyValue(ctx_.builder().createLoad(
            ctx_.builder().getPtrTy(), rightResult.asPtr(), "ref.load.right"));
    }

    // Determine operand types
    std::string leftType = getExpressionType(expr->left.get());
    std::string rightType = getExpressionType(expr->right.get());

    const std::string& op = expr->op;

    // For arithmetic and comparison operators, unwrap boxed types
    bool isArithmeticOp = (op == "+" || op == "-" || op == "*" || op == "/" || op == "%");
    bool isComparisonOp = (op == "==" || op == "is" || op == "!=" || op == "isnt" ||
                           op == "<" || op == "<=" || op == ">" || op == ">=");
    bool isBitwiseOp = (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>");

    if (isArithmeticOp || isComparisonOp || isBitwiseOp) {
        leftResult = unwrapBoxedType(leftResult, leftType);
        rightResult = unwrapBoxedType(rightResult, rightType);
    }

    // Arithmetic operators
    if (op == "+") return generateAdd(leftResult, rightResult, leftType);
    if (op == "-") return generateSub(leftResult, rightResult, leftType);
    if (op == "*") return generateMul(leftResult, rightResult, leftType);
    if (op == "/") return generateDiv(leftResult, rightResult, leftType);
    if (op == "%") return generateRem(leftResult, rightResult, leftType);

    // Comparison operators
    if (op == "==" || op == "is") return generateEq(leftResult, rightResult, leftType);
    if (op == "!=" || op == "isnt") return generateNe(leftResult, rightResult, leftType);
    if (op == "<") return generateLt(leftResult, rightResult, leftType);
    if (op == "<=") return generateLe(leftResult, rightResult, leftType);
    if (op == ">") return generateGt(leftResult, rightResult, leftType);
    if (op == ">=") return generateGe(leftResult, rightResult, leftType);

    // Bitwise operators
    if (op == "&") return generateBitAnd(leftResult, rightResult);
    if (op == "|") return generateBitOr(leftResult, rightResult);
    if (op == "^") return generateBitXor(leftResult, rightResult);
    if (op == "<<") return generateShl(leftResult, rightResult);
    if (op == ">>") return generateShr(leftResult, rightResult);

    ctx_.lastExprValue = leftResult;
    return ctx_.lastExprValue;
}

// === Member Access Implementation ===

LLVMIR::AnyValue ExprCodegen::visitMemberAccess(Parser::MemberAccessExpr* expr) {
    if (!expr || !expr->object) {
        throw Semantic::CodegenInvariantViolation("NULL_AST",
            "MemberAccessExpr is null or has null object");
    }

    // Handle 'this.property'
    if (dynamic_cast<Parser::ThisExpr*>(expr->object.get())) {
        return loadThisProperty(expr->member);
    }

    // Handle 'variable.property' or 'EnumName::VALUE'
    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr->object.get())) {
        // Check if this is an enum value access (member starts with "::")
        if (expr->member.size() > 2 && expr->member.substr(0, 2) == "::") {
            std::string enumValueName = ident->name + expr->member;  // e.g., "Color::RED"
            // Try to resolve the enum value, including searching by suffix for imported enums
            auto resolved = ctx_.resolveEnumValue(enumValueName);
            if (resolved) {
                int64_t value = resolved->second;
                auto intValue = ctx_.builder().getInt64(value);
                ctx_.lastExprValue = LLVMIR::AnyValue(intValue);
                return ctx_.lastExprValue;
            }
        }
        return loadObjectProperty(ident->name, expr->member);
    }

    // Evaluate object and access property
    auto objectValue = generate(expr->object.get());
    if (objectValue.isPtr()) {
        ctx_.lastExprValue = objectValue;
        return ctx_.lastExprValue;
    }

    throw Semantic::CodegenInvariantViolation("MEMBER_ACCESS",
        "Cannot access member on non-pointer value");
}

// === Call Expression Implementation ===

LLVMIR::AnyValue ExprCodegen::visitCall(Parser::CallExpr* expr) {
    if (!expr || !expr->callee) {
        throw Semantic::CodegenInvariantViolation("NULL_CALL",
            "CallExpr is null or has null callee");
    }

    std::string functionName;
    LLVMIR::PtrValue instancePtr;
    bool isInstanceMethod = false;

    // Determine call type
    if (auto* memberAccess = dynamic_cast<Parser::MemberAccessExpr*>(expr->callee.get())) {
        return handleMemberCall(expr, memberAccess);
    } else if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(expr->callee.get())) {
        functionName = ident->name;
    }

    if (functionName.empty()) {
        throw Semantic::CodegenInvariantViolation("EMPTY_FUNCTION_NAME",
            "CallExpr has no resolvable function name");
    }

    // Build arguments
    std::vector<LLVMIR::AnyValue> args;
    for (const auto& argExpr : expr->arguments) {
        auto argValue = generate(argExpr.get());
        args.push_back(argValue);
    }

    return emitCall(functionName, args, isInstanceMethod, instancePtr);
}

LLVMIR::AnyValue ExprCodegen::visitLambda(Parser::LambdaExpr*) {
    // Lambda implementation - stub for now
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitTypeOf(Parser::TypeOfExpr*) {
    // TypeOf implementation - stub for now
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitQuote(Parser::QuoteExpr* expr) {
    if (!expr) {
        throw Semantic::CodegenInvariantViolation("NULL_QUOTE",
            "QuoteExpr is null");
    }

    // The Quote expression captures code as an AST template and returns
    // a JSON string representation that can be passed to DeriveContext methods.
    //
    // Strategy:
    // 1. Serialize the template AST to JSON at compile-time
    // 2. The serializer emits {"__SPLICE__":"varName"} markers for splice placeholders
    // 3. Generate runtime code that:
    //    a. Starts with the template JSON string
    //    b. For each splice, evaluates the variable and converts to AST JSON
    //    c. Replaces the markers with the splice values
    //    d. Returns the final JSON string

    // First, serialize the template AST to JSON
    // The serializer will encounter SplicePlaceholder nodes and emit markers
    std::string templateJson;

    if (expr->kind == Parser::QuoteExpr::QuoteKind::Expression) {
        // Single expression quote
        if (!expr->templateNodes.empty()) {
            if (auto* exprNode = dynamic_cast<Parser::Expression*>(expr->templateNodes[0].get())) {
                templateJson = Derive::ASTSerializer::serializeExpr(exprNode);
            }
        }
    } else if (expr->kind == Parser::QuoteExpr::QuoteKind::Statement) {
        // Single statement quote
        if (!expr->templateNodes.empty()) {
            if (auto* stmtNode = dynamic_cast<Parser::Statement*>(expr->templateNodes[0].get())) {
                templateJson = Derive::ASTSerializer::serializeStmt(stmtNode);
            }
        }
    } else {
        // Statements (default) - serialize as array of statements
        std::vector<std::unique_ptr<Parser::Statement>> stmts;
        for (auto& node : expr->templateNodes) {
            if (auto* stmt = dynamic_cast<Parser::Statement*>(node.get())) {
                // We need to clone since we can't move from the original
                auto cloned = stmt->clone();
                if (auto* clonedStmt = dynamic_cast<Parser::Statement*>(cloned.release())) {
                    stmts.push_back(std::unique_ptr<Parser::Statement>(clonedStmt));
                }
            }
        }
        templateJson = Derive::ASTSerializer::serializeStatements(stmts);
    }

    // If there are no splices, just return the template JSON as a String
    if (expr->splices.empty()) {
        return emitStringConstant(templateJson);
    }

    // There are splices - we need to generate runtime substitution code
    // Start with the template JSON string
    auto result = emitStringConstant(templateJson);

    // For each splice, generate code to:
    // 1. Get the splice variable's value
    // 2. Convert it to JSON AST representation
    // 3. Replace the marker in the string

    for (const auto& splice : expr->splices) {
        // The marker in the JSON is: {"__SPLICE__":"variableName"}
        // We need to replace this entire JSON object with the AST JSON of the splice value
        std::string marker = "{\"__SPLICE__\":\"" + splice.variableName + "\"}";

        // Look up the splice variable
        auto* varInfo = ctx_.getVariable(splice.variableName);
        if (!varInfo) {
            throw Semantic::UnresolvedIdentifierError(splice.variableName);
        }

        // Get the variable's value
        LLVMIR::AnyValue spliceValue;
        if (varInfo->alloca) {
            auto* loadType = ctx_.mapType(varInfo->xxmlType);
            spliceValue = ctx_.builder().createLoad(
                loadType,
                LLVMIR::PtrValue(varInfo->alloca),
                splice.variableName + ".load"
            );
        } else {
            spliceValue = varInfo->value;
        }

        // For String types in derives, the value should be embedded as a StringLiteral AST node
        // For other types, we convert to string representation
        std::string baseType = TypeNormalizer::stripOwnershipMarker(varInfo->xxmlType);

        LLVMIR::AnyValue jsonValue;

        // Get or declare the splice helper function
        // This function takes a String^ and wraps it as: {"kind":"StringLiteral","value":"..."}
        auto* wrapStringFunc = ctx_.module().getFunction("xxml_splice_wrap_string");
        if (!wrapStringFunc) {
            std::vector<LLVMIR::Type*> params = { ctx_.builder().getPtrTy() };
            auto* funcType = ctx_.module().getContext().getFunctionTy(
                ctx_.builder().getPtrTy(), params, false);
            wrapStringFunc = ctx_.module().createFunction(funcType, "xxml_splice_wrap_string",
                LLVMIR::Function::Linkage::External);
        }

        if (baseType == "String" || baseType == "Language::Core::String") {
            // For String values, wrap as StringLiteral AST JSON
            std::vector<LLVMIR::AnyValue> wrapArgs = { spliceValue };
            jsonValue = ctx_.builder().createCall(wrapStringFunc, wrapArgs, "splice.json");
        } else {
            // For non-string types, first convert to string via toString()
            std::string toStringName = ctx_.mangleFunctionName(
                ctx_.resolveToQualifiedName(baseType), "toString");
            auto* toStringFunc = ctx_.module().getFunction(toStringName);
            if (toStringFunc && spliceValue.isPtr()) {
                std::vector<LLVMIR::AnyValue> args = { spliceValue };
                auto strValue = ctx_.builder().createCall(toStringFunc, args, "splice.str");
                // Then wrap as StringLiteral AST JSON
                std::vector<LLVMIR::AnyValue> wrapArgs = { strValue };
                jsonValue = ctx_.builder().createCall(wrapStringFunc, wrapArgs, "splice.json");
            } else {
                // Fallback - create empty string literal
                jsonValue = emitStringConstant("{\"kind\":\"StringLiteral\",\"value\":\"\"}");
            }
        }

        // Now call String_replace to substitute the marker with the splice value
        // result = result.replace(marker, jsonValue)
        auto* replaceFunc = ctx_.module().getFunction("String_replace");
        if (!replaceFunc) {
            // Declare String_replace(self, pattern, replacement) -> String^
            std::vector<LLVMIR::Type*> params = {
                ctx_.builder().getPtrTy(),  // self
                ctx_.builder().getPtrTy(),  // pattern
                ctx_.builder().getPtrTy()   // replacement
            };
            auto* funcType = ctx_.module().getContext().getFunctionTy(
                ctx_.builder().getPtrTy(), params, false);
            replaceFunc = ctx_.module().createFunction(funcType, "String_replace",
                LLVMIR::Function::Linkage::External);
        }

        // Create the marker string
        auto markerStr = emitStringConstant(marker);

        // Call replace
        std::vector<LLVMIR::AnyValue> replaceArgs = { result, markerStr, jsonValue };
        result = ctx_.builder().createCall(replaceFunc, replaceArgs, "quote.replaced");
    }

    ctx_.lastExprValue = result;
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::visitSplicePlaceholder(Parser::SplicePlaceholder* expr) {
    if (!expr) {
        throw Semantic::CodegenInvariantViolation("NULL_SPLICE",
            "SplicePlaceholder is null");
    }

    // SplicePlaceholder should only appear within Quote blocks.
    // When we encounter one during normal code generation (not inside Quote serialization),
    // it means the user wrote $var outside of a Quote block, which is an error.
    //
    // However, during Quote template serialization, the ASTSerializer handles these
    // by emitting markers. So if we get here during normal codegen, it's an error.

    throw Semantic::CodegenInvariantViolation("SPLICE_OUTSIDE_QUOTE",
        "Splice placeholder '$" + expr->variableName +
        "' can only be used inside Quote { } blocks");
}

// === Private Helper Methods ===

LLVMIR::AnyValue ExprCodegen::loadPropertyFromThis(const PropertyInfo& prop) {
    auto* func = ctx_.currentFunction();
    if (!func || func->getNumParams() == 0) {
        throw Semantic::CodegenInvariantViolation("PROPERTY_NO_FUNCTION",
            "Cannot load property '" + prop.name + "' - no current function or method");
    }

    auto* thisArg = func->getArg(0);
    if (!thisArg) {
        throw Semantic::CodegenInvariantViolation("PROPERTY_NO_THIS",
            "Cannot load property '" + prop.name + "' - 'this' argument is null");
    }

    std::string className = std::string(ctx_.currentClassName());
    auto* classInfo = ctx_.getClass(className);
    if (!classInfo) {
        throw Semantic::MissingClassError(className, "loading property '" + prop.name + "'");
    }
    if (!classInfo->structType) {
        throw Semantic::CodegenInvariantViolation("PROPERTY_NO_STRUCT",
            "Class '" + className + "' has no struct type for property '" + prop.name + "'");
    }

    auto thisPtrValue = LLVMIR::PtrValue(thisArg);
    auto propPtr = ctx_.builder().createStructGEP(
        classInfo->structType,
        thisPtrValue,
        static_cast<unsigned>(prop.index),
        prop.name + ".ptr"
    );

    // Always load the property value.
    // Reference semantics for return statements are handled separately -
    // just because a method returns T& doesn't mean every property access
    // in that method should return the address. Properties used in expressions
    // like `dataPtr + position` need the actual value, not the address.
    auto* propType = prop.isObjectType ? ctx_.module().getContext().getPtrTy() : ctx_.mapType(prop.xxmlType);
    auto loaded = ctx_.builder().createLoad(propType, propPtr, "");

    ctx_.lastExprValue = loaded;
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::loadThisProperty(const std::string& propName) {
    auto* func = ctx_.currentFunction();
    if (!func || func->getNumParams() == 0) {
        throw Semantic::CodegenInvariantViolation("THIS_ACCESS",
            "Cannot access 'this." + propName +
            "' - no current function or function has no parameters");
    }

    auto* thisArg = func->getArg(0);
    if (!thisArg) {
        throw Semantic::CodegenInvariantViolation("THIS_ACCESS",
            "Cannot access 'this." + propName + "' - 'this' argument is null");
    }

    std::string currentClass = std::string(ctx_.currentClassName());
    auto* classInfo = ctx_.getClass(currentClass);
    if (!classInfo) {
        throw Semantic::MissingClassError(currentClass,
            "accessing 'this." + propName + "'");
    }
    if (!classInfo->structType) {
        throw Semantic::CodegenInvariantViolation("MISSING_STRUCT_TYPE",
            "Class '" + currentClass + "' has no struct type for property access");
    }

    for (const auto& prop : classInfo->properties) {
        if (prop.name == propName) {
            auto thisPtrValue = LLVMIR::PtrValue(thisArg);
            auto propPtr = ctx_.builder().createStructGEP(
                classInfo->structType,
                thisPtrValue,
                static_cast<unsigned>(prop.index),
                propName + ".ptr"
            );

            // Always load the property value.
            // Reference semantics for return statements are handled separately -
            // just because a method returns T& doesn't mean every property access
            // in that method should return the address. Properties used in expressions
            // like `dataPtr + position` need the actual value, not the address.
            // Use ptr type for object types (owned/reference/copy), otherwise primitive type
            auto* propType = prop.isObjectType ? ctx_.module().getContext().getPtrTy() : ctx_.mapType(prop.xxmlType);
            auto loaded = ctx_.builder().createLoad(propType, propPtr, "");
            ctx_.lastExprValue = loaded;
            return ctx_.lastExprValue;
        }
    }

    throw Semantic::MissingPropertyError(currentClass, propName);
}

LLVMIR::AnyValue ExprCodegen::loadObjectProperty(const std::string& varName, const std::string& propName) {
    auto* varInfo = ctx_.getVariable(varName);
    if (!varInfo) {
        throw Semantic::UnresolvedIdentifierError(varName);
    }

    std::string cleanType = TypeNormalizer::stripOwnershipMarker(varInfo->xxmlType);

    std::string lookupType = cleanType;
    size_t angleBracket = cleanType.find('<');
    if (angleBracket != std::string::npos) {
        lookupType = cleanType.substr(0, angleBracket);
    }

    auto* classInfo = ctx_.getClass(lookupType);
    if (!classInfo) {
        throw Semantic::MissingClassError(cleanType,
            "accessing '" + varName + "." + propName + "'");
    }
    if (!classInfo->structType) {
        throw Semantic::CodegenInvariantViolation("MISSING_STRUCT_TYPE",
            "Class '" + cleanType + "' has no struct type for property access");
    }

    LLVMIR::PtrValue objPtr;
    if (varInfo->alloca) {
        auto loaded = ctx_.builder().createLoadPtr(LLVMIR::PtrValue(varInfo->alloca), "");
        objPtr = loaded;
    } else if (varInfo->value.isPtr()) {
        objPtr = varInfo->value.asPtr();
    } else {
        throw Semantic::CodegenInvariantViolation("NON_POINTER_ACCESS",
            "Cannot access '" + varName + "." + propName +
            "' - variable '" + varName + "' is not a pointer");
    }

    for (const auto& prop : classInfo->properties) {
        if (prop.name == propName) {
            auto propPtr = ctx_.builder().createStructGEP(
                classInfo->structType,
                objPtr,
                static_cast<unsigned>(prop.index),
                propName + ".ptr"
            );

            // Use ptr type for object types (owned/reference/copy), otherwise primitive type
            auto* propType = prop.isObjectType ? ctx_.module().getContext().getPtrTy() : ctx_.mapType(prop.xxmlType);
            auto loaded = ctx_.builder().createLoad(propType, propPtr, "");
            ctx_.lastExprValue = loaded;
            return ctx_.lastExprValue;
        }
    }

    throw Semantic::MissingPropertyError(cleanType, propName);
}

LLVMIR::AnyValue ExprCodegen::handleMemberCall(Parser::CallExpr* expr,
                                               Parser::MemberAccessExpr* memberAccess) {
    std::string functionName;
    LLVMIR::PtrValue instancePtr;
    bool isInstanceMethod = false;

    if (auto* ident = dynamic_cast<Parser::IdentifierExpr*>(memberAccess->object.get())) {
        if (auto* varInfo = ctx_.getVariable(ident->name)) {
            isInstanceMethod = true;

            std::string className = TypeNormalizer::stripOwnershipMarker(varInfo->xxmlType);
            className = ctx_.resolveToQualifiedName(className);

            if (varInfo->alloca) {
                // Check if this is a value type (Structure)
                auto* classInfo = ctx_.getClass(className);
                if (classInfo && classInfo->isValueType) {
                    // Value types: pass the alloca address directly
                    // (the struct lives in the alloca itself, not as a pointer to heap)
                    instancePtr = LLVMIR::PtrValue(varInfo->alloca);
                } else {
                    // Reference types: load the pointer stored in the alloca
                    // (the alloca stores a pointer to the heap-allocated object)
                    auto loaded = ctx_.builder().createLoadPtr(
                        LLVMIR::PtrValue(varInfo->alloca),
                        ""
                    );
                    instancePtr = loaded;
                }
            } else if (varInfo->value.isPtr()) {
                instancePtr = varInfo->value.asPtr();
            }

            functionName = ctx_.mangleFunctionName(className, memberAccess->member);
        } else {
            // Check if this is a property of the current class
            std::string currentClass = std::string(ctx_.currentClassName());
            auto* classInfo = ctx_.getClass(currentClass);
            bool isProperty = false;

            if (classInfo) {
                for (const auto& prop : classInfo->properties) {
                    if (prop.name == ident->name) {
                        // Load the property value and call method on it
                        isInstanceMethod = true;
                        auto propValue = loadThisProperty(ident->name);
                        if (propValue.isPtr()) {
                            instancePtr = propValue.asPtr();
                        }

                        // Get the type of the property to determine method name
                        std::string propTypeName = TypeNormalizer::stripOwnershipMarker(prop.xxmlType);
                        propTypeName = ctx_.resolveToQualifiedName(propTypeName);
                        functionName = ctx_.mangleFunctionName(propTypeName, memberAccess->member);
                        isProperty = true;
                        break;
                    }
                }
            }

            if (!isProperty) {
            std::string className = ctx_.substituteTemplateParams(ident->name);
            className = ctx_.resolveToQualifiedName(className);

            // Handle both "Constructor" and "::Constructor" (static call syntax)
            std::string methodName = memberAccess->member;
            if (methodName.substr(0, 2) == "::") {
                methodName = methodName.substr(2);  // Strip leading ::
            }

            functionName = ctx_.mangleFunctionName(className, methodName);

            // Add argument count suffix for constructors (legacy compatibility)
            // But NOT for native types (Integer, Float, etc.) which use simpler names
            if (methodName == "Constructor") {
                std::string baseClassName = className;
                // Strip namespace prefix for native type check
                size_t lastSep = baseClassName.rfind("::");
                if (lastSep != std::string::npos) {
                    baseClassName = baseClassName.substr(lastSep + 2);
                }
                // Native types don't use argument count suffix
                static const std::unordered_set<std::string> nativeTypes = {
                    "Integer", "Float", "Double", "Bool", "String", "Byte"
                };
                bool isNativeType = nativeTypes.find(baseClassName) != nativeTypes.end();

                if (!isNativeType) {
                    functionName += "_" + std::to_string(expr->arguments.size());

                    // All non-native type constructors need this pointer - allocate and pass it
                    {
                        // Calculate actual struct size from class info
                        // Classes are registered with qualified names using :: separators
                        // and template args use underscores (e.g., Language::Collections::HashMap_Integer_String)
                        // Normalize template syntax (<> or @) to underscore format for lookup
                        size_t allocSize = 24;  // Default fallback
                        std::string lookupName = NameMangler::normalizeTemplateForLookup(className);
                        auto* classInfo = ctx_.getClass(lookupName);
                        if (classInfo && classInfo->instanceSize > 0) {
                            allocSize = classInfo->instanceSize;
                        }

                        LLVMIR::AnyValue allocResult;

                        // Check if this is a value type (Structure) - use stack allocation
                        if (classInfo && classInfo->isValueType) {
                            // Value type: allocate on stack with alloca in entry block
                            // Use entry block alloca to prevent stack overflow in loops
                            auto allocaPtr = ctx_.builder().createEntryBlockAlloca(classInfo->structType, "struct.tmp");
                            allocResult = LLVMIR::AnyValue(allocaPtr);
                        } else {
                            // Reference type: allocate on heap with xxml_malloc
                            auto* mallocFunc = ctx_.module().getFunction("xxml_malloc");
                            if (!mallocFunc) {
                                std::vector<LLVMIR::Type*> mallocParams = { ctx_.module().getContext().getInt64Ty() };
                                auto* mallocType = ctx_.module().getContext().getFunctionTy(
                                    ctx_.builder().getPtrTy(), mallocParams, false);
                                mallocFunc = ctx_.module().createFunction(mallocType, "xxml_malloc",
                                    LLVMIR::Function::Linkage::External);
                            }
                            auto sizeVal = ctx_.builder().getInt64(allocSize);
                            std::vector<LLVMIR::AnyValue> mallocArgs = { LLVMIR::AnyValue(sizeVal) };
                            allocResult = ctx_.builder().createCall(mallocFunc, mallocArgs, "");
                        }

                        // Build constructor args with this pointer first
                        std::vector<LLVMIR::AnyValue> ctorArgs;
                        ctorArgs.push_back(allocResult);
                        for (const auto& argExpr : expr->arguments) {
                            ctorArgs.push_back(generate(argExpr.get()));
                        }

                        emitCall(functionName, ctorArgs, true, allocResult.asPtr());

                        // Register as temporary for cleanup after statement
                        // (only for heap-allocated objects, not value types)
                        if (!classInfo || !classInfo->isValueType) {
                            ctx_.registerTemporary(className + "^", allocResult);
                        }

                        ctx_.lastExprValue = allocResult;
                        return ctx_.lastExprValue;
                    }
                } else {
                    // Native type constructor (String, Integer, etc.)
                    // These allocate internally and return a pointer
                    // Build args and call the constructor
                    std::vector<LLVMIR::AnyValue> ctorArgs;
                    for (const auto& argExpr : expr->arguments) {
                        ctorArgs.push_back(generate(argExpr.get()));
                    }
                    auto result = emitCall(functionName, ctorArgs, false, LLVMIR::PtrValue(nullptr));
                    // Note: emitCall already handles temporary registration for heap types

                    ctx_.lastExprValue = result;
                    return ctx_.lastExprValue;
                }
            }
            }  // close if (!isProperty)
        }
    } else if (auto* thisExpr = dynamic_cast<Parser::ThisExpr*>(memberAccess->object.get())) {
        (void)thisExpr;
        isInstanceMethod = true;
        auto* func = ctx_.currentFunction();
        if (func && func->getNumParams() > 0) {
            instancePtr = LLVMIR::PtrValue(func->getArg(0));
        }
        std::string className = std::string(ctx_.currentClassName());
        className = ctx_.resolveToQualifiedName(className);
        functionName = ctx_.mangleFunctionName(className, memberAccess->member);
    } else if (auto* nestedAccess = dynamic_cast<Parser::MemberAccessExpr*>(memberAccess->object.get())) {
        // Handle nested member access for static calls like System::Console::printLine
        // Build the full qualified path by recursively collecting segments
        std::vector<std::string> segments;
        Parser::Expression* current = memberAccess->object.get();

        while (auto* innerAccess = dynamic_cast<Parser::MemberAccessExpr*>(current)) {
            segments.push_back(innerAccess->member);
            current = innerAccess->object.get();
        }

        // The final should be an identifier (the base namespace/class) or ThisExpr
        if (auto* baseThis = dynamic_cast<Parser::ThisExpr*>(current)) {
            // Handle this.property.method() pattern (e.g., this.x.toString())
            (void)baseThis;
            if (segments.size() == 1) {
                // Single property access: this.x.method()
                std::string propName = segments[0];
                if (propName.substr(0, 2) == "::") {
                    propName = propName.substr(2);
                }

                // Load the property value
                auto propValue = loadThisProperty(propName);
                if (propValue.isPtr()) {
                    instancePtr = propValue.asPtr();
                    isInstanceMethod = true;

                    // Get the property type to determine the method name
                    std::string currentClass = std::string(ctx_.currentClassName());
                    auto* classInfo = ctx_.getClass(currentClass);
                    if (classInfo) {
                        for (const auto& prop : classInfo->properties) {
                            if (prop.name == propName) {
                                std::string propTypeName = TypeNormalizer::stripOwnershipMarker(prop.xxmlType);
                                propTypeName = ctx_.resolveToQualifiedName(propTypeName);
                                std::string methodName = memberAccess->member;
                                if (methodName.substr(0, 2) == "::") {
                                    methodName = methodName.substr(2);
                                }
                                functionName = ctx_.mangleFunctionName(propTypeName, methodName);
                                break;
                            }
                        }
                    }
                }
            }
        } else if (auto* baseIdent = dynamic_cast<Parser::IdentifierExpr*>(current)) {
            // Reverse to get correct order: System, Console
            std::string qualifiedClass = baseIdent->name;
            for (auto it = segments.rbegin(); it != segments.rend(); ++it) {
                std::string seg = *it;
                // Strip leading :: from segment if present
                if (seg.substr(0, 2) == "::") {
                    seg = seg.substr(2);
                }
                qualifiedClass += "::" + seg;
            }

            // Resolve and mangle the function name
            qualifiedClass = ctx_.resolveToQualifiedName(qualifiedClass);

            // Strip leading :: from member if present
            std::string methodName = memberAccess->member;
            if (methodName.substr(0, 2) == "::") {
                methodName = methodName.substr(2);
            }
            functionName = ctx_.mangleFunctionName(qualifiedClass, methodName);

            // Handle constructor calls for nested member access (e.g., Namespace::Class::Constructor)
            if (methodName == "Constructor") {
                // Check if this is a native type (shouldn't add param count suffix)
                std::string baseClassName = qualifiedClass;
                size_t lastSep = baseClassName.rfind("::");
                if (lastSep != std::string::npos) {
                    baseClassName = baseClassName.substr(lastSep + 2);
                }
                static const std::unordered_set<std::string> nativeTypes = {
                    "Integer", "Float", "Double", "Bool", "String", "Byte"
                };
                bool isNativeType = nativeTypes.find(baseClassName) != nativeTypes.end();

                if (!isNativeType) {
                    functionName += "_" + std::to_string(expr->arguments.size());

                    // All non-native type constructors need this pointer - allocate and pass it
                    {
                        // Calculate actual struct size from class info
                        // Classes are registered with qualified names using :: separators
                        // and template args use underscores (e.g., Language::Collections::HashMap_Integer_String)
                        // Normalize template syntax (<> or @) to underscore format for lookup
                        size_t allocSize = 24;  // Default fallback
                        std::string lookupName = NameMangler::normalizeTemplateForLookup(qualifiedClass);
                        auto* classInfo = ctx_.getClass(lookupName);
                        if (classInfo && classInfo->instanceSize > 0) {
                            allocSize = classInfo->instanceSize;
                        }

                        LLVMIR::AnyValue allocResult;

                        // Check if this is a value type (Structure) - use stack allocation
                        if (classInfo && classInfo->isValueType) {
                            // Value type: allocate on stack with alloca in entry block
                            // Use entry block alloca to prevent stack overflow in loops
                            auto allocaPtr = ctx_.builder().createEntryBlockAlloca(classInfo->structType, "struct.tmp");
                            allocResult = LLVMIR::AnyValue(allocaPtr);
                        } else {
                            // Reference type: allocate on heap with xxml_malloc
                            auto* mallocFunc = ctx_.module().getFunction("xxml_malloc");
                            if (!mallocFunc) {
                                std::vector<LLVMIR::Type*> mallocParams = { ctx_.module().getContext().getInt64Ty() };
                                auto* mallocType = ctx_.module().getContext().getFunctionTy(
                                    ctx_.builder().getPtrTy(), mallocParams, false);
                                mallocFunc = ctx_.module().createFunction(mallocType, "xxml_malloc",
                                    LLVMIR::Function::Linkage::External);
                            }
                            auto sizeVal = ctx_.builder().getInt64(allocSize);
                            std::vector<LLVMIR::AnyValue> mallocArgs = { LLVMIR::AnyValue(sizeVal) };
                            allocResult = ctx_.builder().createCall(mallocFunc, mallocArgs, "");
                        }

                        // Build constructor args with this pointer first
                        std::vector<LLVMIR::AnyValue> ctorArgs;
                        ctorArgs.push_back(allocResult);
                        for (const auto& argExpr : expr->arguments) {
                            ctorArgs.push_back(generate(argExpr.get()));
                        }

                        emitCall(functionName, ctorArgs, true, allocResult.asPtr());

                        // Register as temporary for cleanup after statement
                        // (only for heap-allocated objects, not value types)
                        if (!classInfo || !classInfo->isValueType) {
                            ctx_.registerTemporary(qualifiedClass + "^", allocResult);
                        }

                        ctx_.lastExprValue = allocResult;
                        return ctx_.lastExprValue;
                    }
                }
            }
        }
    } else if (auto* innerCall = dynamic_cast<Parser::CallExpr*>(memberAccess->object.get())) {
        // Handle chained method calls like: String::Constructor("x").append(y)
        // First evaluate the inner call to get the instance
        auto innerResult = generate(innerCall);
        if (innerResult.isPtr()) {
            isInstanceMethod = true;

            // Use the resolved type from semantic analysis
            // This is set by SemanticAnalyzer::registerExpressionType()
            std::string returnType = innerCall->resolvedType;
            bool isReferenceReturn = (innerCall->resolvedOwnership == Parser::OwnershipType::Reference);

            // Strip ownership markers from return type if present
            if (!returnType.empty() && (returnType.back() == '^' ||
                returnType.back() == '&' || returnType.back() == '%')) {
                returnType.pop_back();
            }

            // If the inner method returns a reference type, the result is a pointer
            // to where the actual object pointer is stored - we need to load through it
            if (isReferenceReturn) {
                auto loadedObj = ctx_.builder().createLoad(
                    ctx_.builder().getPtrTy(),
                    innerResult.asPtr(),
                    "ref.deref"
                );
                instancePtr = loadedObj.asPtr();
            } else {
                instancePtr = innerResult.asPtr();
            }

            // Strip template args from type name for function lookup
            size_t templateStart = returnType.find('<');
            if (templateStart != std::string::npos) {
                returnType = returnType.substr(0, templateStart);
            }

            // Get the method being called on the result
            std::string outerMethodName = memberAccess->member;
            if (outerMethodName.substr(0, 2) == "::") {
                outerMethodName = outerMethodName.substr(2);
            }

            functionName = returnType + "_" + outerMethodName;
        }
    }

    if (functionName.empty()) {
        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }

    std::vector<LLVMIR::AnyValue> args;
    if (isInstanceMethod) {
        args.push_back(LLVMIR::AnyValue(instancePtr));
    }
    for (const auto& argExpr : expr->arguments) {
        auto argValue = generate(argExpr.get());
        args.push_back(argValue);
    }

    // Check for ownership transfer - if a parameter is owned (^), the callee takes ownership
    // and we should NOT free the argument after the call
    auto paramTypes = ctx_.lookupMethodParameterTypes(functionName);
    if (!paramTypes.empty()) {
        // paramTypes does not include 'this', so adjust index
        size_t paramOffset = isInstanceMethod ? 1 : 0;
        for (size_t i = 0; i < paramTypes.size() && (i + paramOffset) < args.size(); ++i) {
            const std::string& paramType = paramTypes[i];
            // Check if parameter is owned (ends with ^) - ownership transfers to callee
            if (!paramType.empty() && paramType.back() == '^') {
                // Unregister this argument as a temporary - callee now owns it
                ctx_.unregisterTemporary(args[i + paramOffset]);
            }
        }
    }

    return emitCall(functionName, args, isInstanceMethod, instancePtr);
}

LLVMIR::AnyValue ExprCodegen::emitCall(const std::string& functionName,
                                       const std::vector<LLVMIR::AnyValue>& args,
                                       bool /*isInstanceMethod*/,
                                       LLVMIR::PtrValue /*instancePtr*/) {
    // Map runtime function names to their actual implementations
    std::string resolvedName = functionName;

    // Map System::Console methods to Console runtime functions
    // System_Console_printLine -> Console_printLine
    if (resolvedName.find("System_Console_") == 0) {
        std::string consoleMethod = resolvedName.substr(15);  // Remove "System_Console_" prefix
        resolvedName = "Console_" + consoleMethod;
    }

    auto* func = ctx_.module().getFunction(resolvedName);
    if (!func) {
        // Infer parameter types from actual argument values
        // This is important for native methods where parameters may be i32, i64, etc.
        std::vector<LLVMIR::Type*> paramTypes;
        for (const auto& arg : args) {
            if (arg.isInt()) {
                // Use the actual integer type from the value
                auto* rawVal = arg.raw();
                if (rawVal && rawVal->getType()) {
                    paramTypes.push_back(rawVal->getType());
                } else {
                    paramTypes.push_back(ctx_.builder().getInt64Ty());  // Default to i64
                }
            } else if (arg.isFloat()) {
                // Check if it's float or double precision
                auto floatVal = arg.asFloat();
                if (floatVal.getPrecision() == LLVMIR::FloatType::Precision::Double) {
                    paramTypes.push_back(ctx_.builder().getDoubleTy());
                } else {
                    paramTypes.push_back(ctx_.builder().getFloatTy());
                }
            } else {
                paramTypes.push_back(ctx_.builder().getPtrTy());  // Default to ptr
            }
        }

        // Look up the actual return type from semantic analysis
        // This is critical for methods that return NativeType (which should be i64, not ptr)
        LLVMIR::Type* returnType = ctx_.builder().getPtrTy();  // Default to ptr
        std::string xxmlReturnType = ctx_.lookupMethodReturnType(resolvedName);
        if (!xxmlReturnType.empty()) {
            // Use mapType to get the correct LLVM type
            returnType = ctx_.mapType(xxmlReturnType);
        }

        auto* funcType = ctx_.module().getContext().getFunctionTy(
            returnType, paramTypes, false);
        func = ctx_.module().createFunction(funcType, resolvedName,
                                            LLVMIR::Function::Linkage::External);
    }

    if (!func) {
        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }

    // Adapt argument types to match expected parameter types
    // This is critical for Syscall functions like xxml_memset which expect specific int sizes
    std::vector<LLVMIR::AnyValue> adaptedArgs = args;
    auto* funcType = func->getFunctionType();
    size_t numParams = funcType->getParamTypes().size();

    for (size_t i = 0; i < adaptedArgs.size() && i < numParams; ++i) {
        auto& arg = adaptedArgs[i];
        auto* expectedType = funcType->getParamType(i);

        if (arg.isInt() && expectedType) {
            auto* argType = arg.raw()->getType();

            // Check if both are integer types but with different bit widths
            if (auto* expectedIntTy = dynamic_cast<LLVMIR::IntegerType*>(expectedType)) {
                if (auto* argIntTy = dynamic_cast<LLVMIR::IntegerType*>(argType)) {
                    unsigned expectedBits = expectedIntTy->getBitWidth();
                    unsigned argBits = argIntTy->getBitWidth();

                    if (argBits > expectedBits) {
                        // Truncate larger int to smaller (e.g., i64 -> i32)
                        auto truncated = ctx_.builder().createTrunc(arg.asInt(), expectedIntTy, "");
                        arg = LLVMIR::AnyValue(truncated);
                    } else if (argBits < expectedBits) {
                        // Zero-extend smaller int to larger (e.g., i32 -> i64)
                        auto extended = ctx_.builder().createZExt(arg.asInt(), expectedIntTy, "");
                        arg = LLVMIR::AnyValue(extended);
                    }
                }
            }
        }
    }

    // Don't set a fixed name - let the emitter assign unique temporaries
    auto result = ctx_.builder().createCall(func, adaptedArgs, "");

    // Register method return values as temporaries if they are OWNED heap-allocated objects
    // We use the xxmlReturnType we looked up earlier
    // IMPORTANT: References (&) should NOT be registered - they are borrowed, not owned
    std::string returnType = ctx_.lookupMethodReturnType(resolvedName);
    if (!returnType.empty() && result.isPtr()) {
        // Check ownership marker FIRST - references should never be freed
        char ownershipMarker = returnType.back();
        bool isReference = (ownershipMarker == '&');
        bool isCopy = (ownershipMarker == '%');

        // Skip references entirely - they are borrowed and should not be freed
        if (!isReference && !isCopy) {
            // Strip ownership markers to get base type
            std::string baseType = returnType;
            if (!baseType.empty() && (baseType.back() == '^' || baseType.back() == '&' || baseType.back() == '%')) {
                baseType = baseType.substr(0, baseType.size() - 1);
            }

            // Check if the return type indicates a heap-allocated object
            // Skip NativeType, None, void, and primitive return types
            bool isHeapObject = false;
            if (baseType.find("NativeType<") == std::string::npos &&
                baseType != "None" &&
                baseType != "none" &&
                baseType != "void" &&
                baseType != "") {
                // Known heap-allocated wrapper types
                static const std::unordered_set<std::string> heapTypes = {
                    "Integer", "String", "Bool", "Float", "Double", "Byte"
                };

                // Check if it's a known heap type or a class type
                if (heapTypes.count(baseType) > 0 ||
                    baseType.find("::") != std::string::npos ||  // Namespaced type
                    baseType.find("<") != std::string::npos ||   // Template type
                    std::isupper(baseType[0])) {                 // PascalCase class name
                    isHeapObject = true;
                }
            }

            if (isHeapObject) {
                // Use owned type for cleanup
                std::string typeForCleanup = baseType + "^";
                ctx_.registerTemporary(typeForCleanup, result);
            }
        }
    }

    ctx_.lastExprValue = result;
    return ctx_.lastExprValue;
}

// === Arithmetic Operations ===

LLVMIR::AnyValue ExprCodegen::generateAdd(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                          const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFAdd(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else if (left.isPtr() && right.isInt()) {
        // Pointer arithmetic: ptr + int (byte offset)
        auto& llvmCtx = ctx_.module().getContext();
        auto result = ctx_.builder().createGEP(
            llvmCtx.getInt8Ty(), left.asPtr(), {right.asInt()}, "ptr_add");
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else if (left.isInt() && right.isPtr()) {
        // Pointer arithmetic: int + ptr (byte offset)
        auto& llvmCtx = ctx_.module().getContext();
        auto result = ctx_.builder().createGEP(
            llvmCtx.getInt8Ty(), right.asPtr(), {left.asInt()}, "ptr_add");
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createAdd(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateSub(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                          const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFSub(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else if (left.isPtr() && right.isInt()) {
        // Pointer arithmetic: ptr - int (negative byte offset)
        auto& llvmCtx = ctx_.module().getContext();
        auto negOffset = ctx_.builder().createNeg(right.asInt(), "neg_offset");
        auto result = ctx_.builder().createGEP(
            llvmCtx.getInt8Ty(), left.asPtr(), {negOffset}, "ptr_sub");
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createSub(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateMul(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                          const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFMul(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createMul(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateDiv(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                          const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFDiv(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createSDiv(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateRem(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                          const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFRem(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createSRem(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

// === Comparison Operations ===

LLVMIR::AnyValue ExprCodegen::generateEq(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                         const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFCmpOEQ(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else if (left.isPtr() || right.isPtr()) {
        // Handle ptr == int (null comparison) by converting int to null ptr
        LLVMIR::PtrValue leftPtr = left.isPtr() ? left.asPtr() : ctx_.builder().getNullPtr();
        LLVMIR::PtrValue rightPtr = right.isPtr() ? right.asPtr() : ctx_.builder().getNullPtr();
        auto result = ctx_.builder().createPtrEQ(leftPtr, rightPtr);
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createICmpEQ(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateNe(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                         const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFCmpONE(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else if (left.isPtr() || right.isPtr()) {
        // Handle ptr != int (null comparison) by converting int to null ptr
        LLVMIR::PtrValue leftPtr = left.isPtr() ? left.asPtr() : ctx_.builder().getNullPtr();
        LLVMIR::PtrValue rightPtr = right.isPtr() ? right.asPtr() : ctx_.builder().getNullPtr();
        auto result = ctx_.builder().createPtrNE(leftPtr, rightPtr);
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createICmpNE(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateLt(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                         const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFCmpOLT(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createICmpSLT(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateLe(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                         const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFCmpOLE(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createICmpSLE(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateGt(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                         const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFCmpOGT(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createICmpSGT(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateGe(LLVMIR::AnyValue left, LLVMIR::AnyValue right,
                                         const std::string& type) {
    if (isFloatType(type)) {
        auto result = ctx_.builder().createFCmpOGE(left.asFloat(), right.asFloat());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    } else {
        auto result = ctx_.builder().createICmpSGE(left.asInt(), right.asInt());
        ctx_.lastExprValue = LLVMIR::AnyValue(result);
    }
    return ctx_.lastExprValue;
}

// === Bitwise Operations ===

LLVMIR::AnyValue ExprCodegen::generateBitAnd(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
    auto result = ctx_.builder().createAnd(left.asInt(), right.asInt());
    ctx_.lastExprValue = LLVMIR::AnyValue(result);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateBitOr(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
    auto result = ctx_.builder().createOr(left.asInt(), right.asInt());
    ctx_.lastExprValue = LLVMIR::AnyValue(result);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateBitXor(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
    auto result = ctx_.builder().createXor(left.asInt(), right.asInt());
    ctx_.lastExprValue = LLVMIR::AnyValue(result);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateShl(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
    auto result = ctx_.builder().createShl(left.asInt(), right.asInt());
    ctx_.lastExprValue = LLVMIR::AnyValue(result);
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateShr(LLVMIR::AnyValue left, LLVMIR::AnyValue right) {
    auto result = ctx_.builder().createAShr(left.asInt(), right.asInt());
    ctx_.lastExprValue = LLVMIR::AnyValue(result);
    return ctx_.lastExprValue;
}

// === Logical Operations (Short-Circuit) ===

LLVMIR::AnyValue ExprCodegen::generateLogicalAnd(Parser::BinaryExpr* expr, LLVMIR::AnyValue leftResult) {
    auto* func = ctx_.currentFunction();
    if (!func) {
        return LLVMIR::AnyValue(ctx_.builder().getInt1(false));
    }

    auto* rhsBlock = func->createBasicBlock("and.rhs");
    auto* mergeBlock = func->createBasicBlock("and.merge");

    ctx_.builder().createCondBr(leftResult.asInt(), rhsBlock, mergeBlock);

    ctx_.setInsertPoint(rhsBlock);
    auto rightResult = generate(expr->right.get());
    ctx_.builder().createBr(mergeBlock);
    auto* fromRhs = ctx_.currentBlock();

    ctx_.setInsertPoint(mergeBlock);
    auto* phiNode = ctx_.builder().createIntPHI(ctx_.module().getContext().getInt1Ty(), "and.result");
    phiNode->addIncoming(ctx_.builder().getInt1(false), ctx_.currentBlock());
    phiNode->addIncoming(rightResult.asInt(), fromRhs);

    ctx_.lastExprValue = LLVMIR::AnyValue(phiNode->result());
    return ctx_.lastExprValue;
}

LLVMIR::AnyValue ExprCodegen::generateLogicalOr(Parser::BinaryExpr* expr, LLVMIR::AnyValue leftResult) {
    auto* func = ctx_.currentFunction();
    if (!func) {
        return LLVMIR::AnyValue(ctx_.builder().getInt1(true));
    }

    auto* rhsBlock = func->createBasicBlock("or.rhs");
    auto* mergeBlock = func->createBasicBlock("or.merge");

    ctx_.builder().createCondBr(leftResult.asInt(), mergeBlock, rhsBlock);

    ctx_.setInsertPoint(rhsBlock);
    auto rightResult = generate(expr->right.get());
    ctx_.builder().createBr(mergeBlock);
    auto* fromRhs = ctx_.currentBlock();

    ctx_.setInsertPoint(mergeBlock);
    auto* phiNode = ctx_.builder().createIntPHI(ctx_.module().getContext().getInt1Ty(), "or.result");
    phiNode->addIncoming(ctx_.builder().getInt1(true), ctx_.currentBlock());
    phiNode->addIncoming(rightResult.asInt(), fromRhs);

    ctx_.lastExprValue = LLVMIR::AnyValue(phiNode->result());
    return ctx_.lastExprValue;
}

// === Compile-Time Constant Folding ===

std::optional<LLVMIR::AnyValue> ExprCodegen::tryCompiletimeFold(Parser::Expression* expr) {
    // Check if we have a compile-time interpreter
    if (!ctx_.hasCompiletimeInterpreter()) {
        return std::nullopt;
    }

    // Skip folding for simple literals - they're already handled efficiently
    // by their respective visitor methods (visitIntegerLiteral, etc.)
    // Folding them would cause double-wrapping issues when they're call arguments
    if (dynamic_cast<Parser::IntegerLiteralExpr*>(expr) ||
        dynamic_cast<Parser::FloatLiteralExpr*>(expr) ||
        dynamic_cast<Parser::DoubleLiteralExpr*>(expr) ||
        dynamic_cast<Parser::BoolLiteralExpr*>(expr) ||
        dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
        return std::nullopt;
    }

    auto* interp = ctx_.compiletimeInterpreter();

    // Check if expression is compile-time evaluable
    if (!interp->isCompiletimeEvaluable(expr)) {
        return std::nullopt;
    }

    // Evaluate at compile-time
    auto ctValue = interp->evaluate(expr);
    if (!ctValue) {
        return std::nullopt;
    }

    // Convert to LLVM IR
    return emitConstantValue(ctValue.get());
}

LLVMIR::AnyValue ExprCodegen::emitConstantValue(Semantic::CompiletimeValue* value) {
    if (!value) {
        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
    }

    auto& builder = ctx_.builder();
    auto& mod = ctx_.module();

    // For primitive compile-time values, emit wrapped objects via constructor calls
    // This is needed because XXML uses Integer^, Float^, etc. which are object types
    if (value->isInteger()) {
        int64_t val = static_cast<Semantic::CompiletimeInteger*>(value)->value;
        auto* ctorFunc = mod.getFunction("Integer_Constructor");
        if (!ctorFunc) {
            std::vector<LLVMIR::Type*> params = { mod.getContext().getInt64Ty() };
            auto* funcType = mod.getContext().getFunctionTy(builder.getPtrTy(), params, false);
            ctorFunc = mod.createFunction(funcType, "Integer_Constructor", LLVMIR::Function::Linkage::External);
        }
        std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(builder.getInt64(val)) };
        return builder.createCall(ctorFunc, args, "ct.int");
    }

    if (value->isFloat()) {
        float val = static_cast<Semantic::CompiletimeFloat*>(value)->value;
        auto* ctorFunc = mod.getFunction("Float_Constructor");
        if (!ctorFunc) {
            std::vector<LLVMIR::Type*> params = { mod.getContext().getFloatTy() };
            auto* funcType = mod.getContext().getFunctionTy(builder.getPtrTy(), params, false);
            ctorFunc = mod.createFunction(funcType, "Float_Constructor", LLVMIR::Function::Linkage::External);
        }
        std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(builder.getFloat(val)) };
        return builder.createCall(ctorFunc, args, "ct.float");
    }

    if (value->isDouble()) {
        double val = static_cast<Semantic::CompiletimeDouble*>(value)->value;
        auto* ctorFunc = mod.getFunction("Double_Constructor");
        if (!ctorFunc) {
            std::vector<LLVMIR::Type*> params = { mod.getContext().getDoubleTy() };
            auto* funcType = mod.getContext().getFunctionTy(builder.getPtrTy(), params, false);
            ctorFunc = mod.createFunction(funcType, "Double_Constructor", LLVMIR::Function::Linkage::External);
        }
        std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(builder.getDouble(val)) };
        return builder.createCall(ctorFunc, args, "ct.double");
    }

    if (value->isBool()) {
        bool val = static_cast<Semantic::CompiletimeBool*>(value)->value;
        auto* ctorFunc = mod.getFunction("Bool_Constructor");
        if (!ctorFunc) {
            std::vector<LLVMIR::Type*> params = { mod.getContext().getInt1Ty() };
            auto* funcType = mod.getContext().getFunctionTy(builder.getPtrTy(), params, false);
            ctorFunc = mod.createFunction(funcType, "Bool_Constructor", LLVMIR::Function::Linkage::External);
        }
        std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(builder.getInt1(val)) };
        auto result = builder.createCall(ctorFunc, args, "ct.bool");
        // Register as temporary for cleanup
        ctx_.registerTemporary("Bool^", result);
        return result;
    }

    // Handle object types (custom compiletime classes)
    if (value->isObject()) {
        return emitObjectConstant(static_cast<Semantic::CompiletimeObject*>(value));
    }

    // Handle string values
    if (value->isString()) {
        auto* strVal = static_cast<Semantic::CompiletimeString*>(value);
        return emitStringConstant(strVal->value);
    }

    // Fallback - return null
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::emitRawConstant(Semantic::CompiletimeValue* value) {
    auto& builder = ctx_.builder();

    // Emit true raw LLVM constants - NO constructor calls here!
    // Boxing should only happen in emitNativeTypeWrapper when needed.
    // This is called for values that canUseRawValue() returns true.

    if (value->isInteger()) {
        int64_t val = static_cast<Semantic::CompiletimeInteger*>(value)->value;
        return LLVMIR::AnyValue(builder.getInt64(val));
    }

    if (value->isFloat()) {
        float val = static_cast<Semantic::CompiletimeFloat*>(value)->value;
        return LLVMIR::AnyValue(builder.getFloat(val));
    }

    if (value->isDouble()) {
        double val = static_cast<Semantic::CompiletimeDouble*>(value)->value;
        return LLVMIR::AnyValue(builder.getDouble(val));
    }

    if (value->isBool()) {
        bool val = static_cast<Semantic::CompiletimeBool*>(value)->value;
        return LLVMIR::AnyValue(builder.getInt1(val));
    }

    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::emitStringConstant(const std::string& value) {
    // Create a global string constant and call String_Constructor
    auto& builder = ctx_.builder();

    // Create global string constant
    std::string label = ctx_.allocateStringLabel();
    ctx_.addStringLiteral(label, value);

    // Get pointer to the string constant
    auto* globalStr = ctx_.module().getOrCreateStringLiteral(value);
    auto strPtr = globalStr->toTypedValue();

    // Get or declare String_Constructor
    auto* func = ctx_.module().getFunction("String_Constructor");
    if (!func) {
        // Declare String_Constructor(const char*) -> String^
        std::vector<LLVMIR::Type*> params = { builder.getPtrTy() };
        auto* funcType = ctx_.module().getContext().getFunctionTy(
            builder.getPtrTy(), params, false);
        func = ctx_.module().createFunction(funcType, "String_Constructor",
            LLVMIR::Function::Linkage::External);
    }

    // Call String_Constructor with the C string
    std::vector<LLVMIR::AnyValue> args = { strPtr };
    auto result = builder.createCall(func, args, "ct.str");
    return LLVMIR::AnyValue(result);
}

LLVMIR::AnyValue ExprCodegen::emitObjectConstant(Semantic::CompiletimeObject* obj) {
    // Check if this is a NativeType wrapper (Integer, Float, Double, Bool)
    if (isNativeTypeWrapper(obj->className)) {
        return emitNativeTypeWrapper(obj);
    }

    // For user-defined compiletime classes, emit as struct with constant values
    return emitCompiletimeStruct(obj);
}

bool ExprCodegen::isNativeTypeWrapper(const std::string& className) const {
    return className == "Integer" || className == "Float" ||
           className == "Double" || className == "Bool" ||
           className == "String";
}

LLVMIR::AnyValue ExprCodegen::emitNativeTypeWrapper(Semantic::CompiletimeObject* obj) {
    auto& builder = ctx_.builder();

    // Integer stores its value internally - extract and call constructor
    if (obj->className == "Integer") {
        if (auto* prop = obj->getProperty("value")) {
            if (prop->isInteger()) {
                int64_t val = static_cast<Semantic::CompiletimeInteger*>(prop)->value;
                auto* func = ctx_.module().getFunction("Integer_Constructor");
                if (func) {
                    auto constArg = builder.getInt64(val);
                    std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(constArg) };
                    auto result = builder.createCall(func, args, "ct.int");
                    return LLVMIR::AnyValue(result);
                }
            }
        }
    }

    if (obj->className == "Float") {
        if (auto* prop = obj->getProperty("value")) {
            if (prop->isFloat()) {
                float val = static_cast<Semantic::CompiletimeFloat*>(prop)->value;
                auto* func = ctx_.module().getFunction("Float_Constructor");
                if (func) {
                    auto constArg = builder.getFloat(val);
                    std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(constArg) };
                    auto result = builder.createCall(func, args, "ct.float");
                    return LLVMIR::AnyValue(result);
                }
            }
        }
    }

    if (obj->className == "Double") {
        if (auto* prop = obj->getProperty("value")) {
            if (prop->isDouble()) {
                double val = static_cast<Semantic::CompiletimeDouble*>(prop)->value;
                auto* func = ctx_.module().getFunction("Double_Constructor");
                if (func) {
                    auto constArg = builder.getDouble(val);
                    std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(constArg) };
                    auto result = builder.createCall(func, args, "ct.double");
                    return LLVMIR::AnyValue(result);
                }
            }
        }
    }

    if (obj->className == "Bool") {
        if (auto* prop = obj->getProperty("value")) {
            if (prop->isBool()) {
                bool val = static_cast<Semantic::CompiletimeBool*>(prop)->value;
                auto* func = ctx_.module().getFunction("Bool_Constructor");
                if (func) {
                    auto constArg = builder.getInt1(val);
                    std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(constArg) };
                    auto result = builder.createCall(func, args, "ct.bool");
                    // Register as temporary for cleanup
                    ctx_.registerTemporary("Bool^", result);
                    return LLVMIR::AnyValue(result);
                }
            }
        }
    }

    if (obj->className == "String") {
        if (auto* prop = obj->getProperty("value")) {
            if (prop->isString()) {
                const std::string& val = static_cast<Semantic::CompiletimeString*>(prop)->value;
                return emitStringConstant(val);
            }
        }
    }

    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::emitCompiletimeStruct(Semantic::CompiletimeObject* obj) {
    // For user-defined compiletime classes:
    // 1. Get or create the struct type
    // 2. Allocate on the heap (for consistency with XXML object model)
    // 3. Initialize each property with recursively folded constant values

    auto& builder = ctx_.builder();

    // Look up class info to get struct type and property layout
    const auto* classInfo = ctx_.getClass(obj->className);
    if (!classInfo || !classInfo->structType) {
        // Fallback: return null if class not found
        return LLVMIR::AnyValue(builder.getNullPtr());
    }

    // Allocate memory for the object (malloc)
    auto* mallocFunc = ctx_.module().getFunction("xxml_malloc");
    if (!mallocFunc) {
        return LLVMIR::AnyValue(builder.getNullPtr());
    }

    // Get instance size
    size_t instanceSize = classInfo->instanceSize;
    if (instanceSize == 0) {
        instanceSize = 8; // Minimum size
    }

    auto sizeArg = builder.getInt64(static_cast<int64_t>(instanceSize));
    std::vector<LLVMIR::AnyValue> mallocArgs = { LLVMIR::AnyValue(sizeArg) };
    auto objPtr = builder.createCall(mallocFunc, mallocArgs, "ct.obj");

    // Initialize each property with constant values
    for (const auto& [propName, propValue] : obj->properties) {
        // Find property index in the class
        int propIndex = -1;
        for (size_t i = 0; i < classInfo->properties.size(); ++i) {
            if (classInfo->properties[i].name == propName) {
                propIndex = static_cast<int>(i);
                break;
            }
        }

        if (propIndex < 0) continue;

        // RECURSIVE: Emit constant value for this property
        auto foldedProp = emitConstantValue(propValue.get());

        // Store in struct field using GEP
        auto fieldPtr = builder.createStructGEP(
            classInfo->structType,
            objPtr.asPtr(),
            static_cast<unsigned>(propIndex),
            propName
        );
        builder.createStore(foldedProp, fieldPtr);
    }

    return LLVMIR::AnyValue(objPtr);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
