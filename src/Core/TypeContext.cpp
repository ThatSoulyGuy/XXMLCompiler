#include "Core/TypeContext.h"

namespace XXML {
namespace Core {

// ========================================================================
// Constructor
// ========================================================================

TypeContext::TypeContext()
    : cppAnalyzer_() {
}

// ========================================================================
// Mutation API
// ========================================================================

void TypeContext::setExpressionType(Parser::Expression* expr, const ResolvedType& type) {
    if (expr) {
        expressionTypes_[expr] = type;
    }
}

void TypeContext::setVariableType(const std::string& varName, const ResolvedType& type) {
    if (!varName.empty()) {
        variableTypes_[varName] = type;
    }
}

void TypeContext::clear() {
    expressionTypes_.clear();
    variableTypes_.clear();
}

// ========================================================================
// Query API
// ========================================================================

const ResolvedType* TypeContext::getExpressionType(Parser::Expression* expr) const {
    auto it = expressionTypes_.find(expr);
    if (it != expressionTypes_.end()) {
        return &it->second;
    }
    return nullptr;
}

const ResolvedType* TypeContext::getVariableType(const std::string& varName) const {
    auto it = variableTypes_.find(varName);
    if (it != variableTypes_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool TypeContext::expressionIsSmartPtr(Parser::Expression* expr) const {
    const ResolvedType* type = getExpressionType(expr);
    return type && type->isSmartPointer();
}

bool TypeContext::variableIsSmartPtr(const std::string& varName) const {
    const ResolvedType* type = getVariableType(varName);
    return type && type->isSmartPointer();
}

Parser::OwnershipType TypeContext::getVariableOwnership(const std::string& varName) const {
    const ResolvedType* type = getVariableType(varName);
    return type ? type->ownership : Parser::OwnershipType::None;
}

std::string TypeContext::getVariableTypeName(const std::string& varName) const {
    const ResolvedType* type = getVariableType(varName);
    return type ? type->xxmlTypeName : "";
}

std::string TypeContext::getVariableCppType(const std::string& varName) const {
    const ResolvedType* type = getVariableType(varName);
    return type ? type->cppTypeName : "";
}

} // namespace Core
} // namespace XXML
