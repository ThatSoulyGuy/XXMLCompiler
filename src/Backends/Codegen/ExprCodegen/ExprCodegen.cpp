#include "Backends/Codegen/ExprCodegen/ExprCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

LLVMIR::AnyValue ExprCodegen::generate(Parser::Expression* expr) {
    if (!expr) {
        return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
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
    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        return visitCall(callExpr);
    }
    if (auto* lambdaExpr = dynamic_cast<Parser::LambdaExpr*>(expr)) {
        return visitLambda(lambdaExpr);
    }
    if (auto* typeofExpr = dynamic_cast<Parser::TypeOfExpr*>(expr)) {
        return visitTypeOf(typeofExpr);
    }

    // Unknown expression type - return null
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

// === Utility Methods ===

LLVMIR::AnyValue ExprCodegen::loadIfNeeded(LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca) {
    if (alloca && value.isPtr()) {
        // Load from alloca
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

    // Literals have known types
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

    // For other expressions, would need type inference
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

// === Default implementations (return null, overridden in specific files) ===

LLVMIR::AnyValue ExprCodegen::visitIntegerLiteral(Parser::IntegerLiteralExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getInt64(0));
}

LLVMIR::AnyValue ExprCodegen::visitFloatLiteral(Parser::FloatLiteralExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getFloat(0.0f));
}

LLVMIR::AnyValue ExprCodegen::visitDoubleLiteral(Parser::DoubleLiteralExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getDouble(0.0));
}

LLVMIR::AnyValue ExprCodegen::visitStringLiteral(Parser::StringLiteralExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitBoolLiteral(Parser::BoolLiteralExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getInt1(false));
}

LLVMIR::AnyValue ExprCodegen::visitIdentifier(Parser::IdentifierExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitThis(Parser::ThisExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitReference(Parser::ReferenceExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitBinary(Parser::BinaryExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getInt64(0));
}

LLVMIR::AnyValue ExprCodegen::visitMemberAccess(Parser::MemberAccessExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitCall(Parser::CallExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitLambda(Parser::LambdaExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

LLVMIR::AnyValue ExprCodegen::visitTypeOf(Parser::TypeOfExpr*) {
    return LLVMIR::AnyValue(ctx_.builder().getNullPtr());
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
