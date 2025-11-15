#include "Core/CppTypeModel.h"
#include <algorithm>
#include <cctype>

namespace XXML {
namespace Core {

// ============================================================================
// CppTypeInfo Implementation
// ============================================================================

CppTypeInfo::CppTypeInfo(const std::string& typeName)
    : fullTypeName_(typeName)
    , baseTypeName_(typeName)
    , category_(CppTypeCategory::Unknown)
    , isConst_(false)
    , isVolatile_(false)
    , isCallable_(false) {
}

bool CppTypeInfo::canImplicitlyConvertTo(const CppTypeInfo& target) const {
    // Owned<T> can convert to T& via operator T&()
    if (category_ == CppTypeCategory::OwnedPtr && !templateArgs_.empty()) {
        std::string wrappedType = templateArgs_[0];
        if (target.isReference() && target.getBaseName() == wrappedType) {
            return true;
        }
    }

    // T can convert to const T&
    if (target.category_ == CppTypeCategory::ConstRef) {
        if (baseTypeName_ == target.baseTypeName_) {
            return true;
        }
    }

    // Same type conversions
    if (fullTypeName_ == target.fullTypeName_) {
        return true;
    }

    // Numeric conversions (simplified)
    static const std::vector<std::string> numericTypes = {
        "int", "long", "short", "char",
        "float", "double",
        "int32_t", "int64_t", "uint32_t", "uint64_t"
    };

    bool thisIsNumeric = std::find(numericTypes.begin(), numericTypes.end(),
                                   baseTypeName_) != numericTypes.end();
    bool targetIsNumeric = std::find(numericTypes.begin(), numericTypes.end(),
                                     target.baseTypeName_) != numericTypes.end();

    if (thisIsNumeric && targetIsNumeric) {
        return true;
    }

    return false;
}

// ============================================================================
// CppTypeAnalyzer Implementation
// ============================================================================

CppTypeAnalyzer::CppTypeAnalyzer() {
    // Register XXML runtime types
    registerType("Language::Runtime::Owned", CppTypeCategory::OwnedPtr);
    registerType("std::unique_ptr", CppTypeCategory::UniquePtr);
    registerType("std::shared_ptr", CppTypeCategory::SharedPtr);

    // Register XXML core value types
    registerType("Language::Core::Integer", CppTypeCategory::Value);
    registerType("Language::Core::String", CppTypeCategory::Value);
    registerType("Language::Core::Bool", CppTypeCategory::Value);
    registerType("Language::Core::Float", CppTypeCategory::Value);
    registerType("Language::Core::Double", CppTypeCategory::Value);

    // Register primitive types
    registerType("int", CppTypeCategory::Value);
    registerType("long", CppTypeCategory::Value);
    registerType("short", CppTypeCategory::Value);
    registerType("char", CppTypeCategory::Value);
    registerType("bool", CppTypeCategory::Value);
    registerType("float", CppTypeCategory::Value);
    registerType("double", CppTypeCategory::Value);
    registerType("void", CppTypeCategory::Void);

    // Register common XXML methods
    // Integer methods
    registerMethod("Language::Core::Integer", "add", "Language::Core::Integer");
    registerMethod("Language::Core::Integer", "subtract", "Language::Core::Integer");
    registerMethod("Language::Core::Integer", "multiply", "Language::Core::Integer");
    registerMethod("Language::Core::Integer", "divide", "Language::Core::Integer");
    registerMethod("Language::Core::Integer", "toString", "Language::Core::String");
    registerMethod("Language::Core::Integer", "toInt64", "int64_t");
    registerMethod("Language::Core::Integer", "equals", "Language::Core::Bool");
    registerMethod("Language::Core::Integer", "lessThan", "Language::Core::Bool");
    registerMethod("Language::Core::Integer", "greaterThan", "Language::Core::Bool");
    registerMethod("Language::Core::Integer", "lessOrEqual", "Language::Core::Bool");
    registerMethod("Language::Core::Integer", "greaterOrEqual", "Language::Core::Bool");

    // String methods
    registerMethod("Language::Core::String", "append", "Language::Core::String");
    registerMethod("Language::Core::String", "length", "Language::Core::Integer");
    registerMethod("Language::Core::String", "substring", "Language::Core::String");
    registerMethod("Language::Core::String", "charAt", "Language::Core::String");
    registerMethod("Language::Core::String", "toCString", "unsigned char*");
    registerMethod("Language::Core::String", "equals", "Language::Core::Bool");
    registerMethod("Language::Core::String", "isEmpty", "Language::Core::Bool");
    registerMethod("Language::Core::String", "copy", "Language::Core::String");

    // Bool methods
    registerMethod("Language::Core::Bool", "and", "Language::Core::Bool");
    registerMethod("Language::Core::Bool", "or", "Language::Core::Bool");
    registerMethod("Language::Core::Bool", "not", "Language::Core::Bool");
    registerMethod("Language::Core::Bool", "xor", "Language::Core::Bool");
    registerMethod("Language::Core::Bool", "toInteger", "Language::Core::Integer");
}

CppTypeInfo CppTypeAnalyzer::analyze(const std::string& cppTypeName) {
    CppTypeInfo info;
    info.setFullName(cppTypeName);

    std::string typeStr = cppTypeName;

    // Trim whitespace
    typeStr.erase(0, typeStr.find_first_not_of(" \t\n\r"));
    typeStr.erase(typeStr.find_last_not_of(" \t\n\r") + 1);

    // Check for const qualifier
    if (typeStr.find("const ") == 0) {
        info.setConst(true);
        typeStr = typeStr.substr(6);  // Remove "const "
        // Trim again
        typeStr.erase(0, typeStr.find_first_not_of(" \t\n\r"));
    }

    // Check for volatile qualifier
    if (typeStr.find("volatile ") == 0) {
        typeStr = typeStr.substr(9);  // Remove "volatile "
        typeStr.erase(0, typeStr.find_first_not_of(" \t\n\r"));
    }

    // Check for reference/pointer at the end
    if (typeStr.back() == '&') {
        // Remove trailing &
        typeStr.pop_back();
        typeStr.erase(typeStr.find_last_not_of(" \t\n\r") + 1);

        // Check if it's rvalue reference (&&)
        if (typeStr.back() == '&') {
            typeStr.pop_back();
            typeStr.erase(typeStr.find_last_not_of(" \t\n\r") + 1);
            info.setCategory(CppTypeCategory::RValueRef);
        } else {
            // Lvalue reference, check if const
            info.setCategory(info.isConst() ? CppTypeCategory::ConstRef
                                            : CppTypeCategory::LValueRef);
        }
    } else if (typeStr.back() == '*') {
        // Pointer
        typeStr.pop_back();
        typeStr.erase(typeStr.find_last_not_of(" \t\n\r") + 1);

        // Check for const after *
        if (typeStr.find(" const") == typeStr.length() - 6) {
            typeStr = typeStr.substr(0, typeStr.length() - 6);
        }

        info.setCategory(info.isConst() ? CppTypeCategory::ConstPointer
                                         : CppTypeCategory::RawPointer);
    }

    // Parse template arguments if present
    size_t templateStart = typeStr.find('<');
    std::string baseName = typeStr;

    if (templateStart != std::string::npos) {
        baseName = typeStr.substr(0, templateStart);
        size_t pos = templateStart + 1;
        std::vector<std::string> templateArgs = parseTemplateArgs(typeStr, pos);
        info.setTemplateArgs(templateArgs);
    }

    // Trim whitespace from base name
    baseName.erase(0, baseName.find_first_not_of(" \t\n\r"));
    baseName.erase(baseName.find_last_not_of(" \t\n\r") + 1);

    info.setBaseName(baseName);

    // If category not yet determined (not reference/pointer), analyze the base type
    if (info.getCategory() == CppTypeCategory::Unknown) {
        // Check if it's a registered smart pointer type
        if (isSmartPointerType(baseName)) {
            if (baseName.find("Owned") != std::string::npos ||
                baseName == "Language::Runtime::Owned") {
                info.setCategory(CppTypeCategory::OwnedPtr);
            } else if (baseName.find("unique_ptr") != std::string::npos) {
                info.setCategory(CppTypeCategory::UniquePtr);
            } else if (baseName.find("shared_ptr") != std::string::npos) {
                info.setCategory(CppTypeCategory::SharedPtr);
            }
        }
        // Check if it's void
        else if (baseName == "void") {
            info.setCategory(CppTypeCategory::Void);
        }
        // Check if it's a registered value type
        else if (isValueType(baseName)) {
            info.setCategory(CppTypeCategory::Value);
        }
        // Default to value type for unknown types
        else {
            info.setCategory(CppTypeCategory::Value);
        }
    }

    return info;
}

void CppTypeAnalyzer::registerType(const std::string& typeName, CppTypeCategory category) {
    registeredTypes_[typeName] = category;
}

void CppTypeAnalyzer::registerMethod(const std::string& typeName,
                                     const std::string& methodName,
                                     const std::string& returnType) {
    methodRegistry_[typeName][methodName] = returnType;
}

bool CppTypeAnalyzer::hasMethod(const std::string& typeName,
                                const std::string& methodName) const {
    auto typeIt = methodRegistry_.find(typeName);
    if (typeIt == methodRegistry_.end()) {
        return false;
    }

    return typeIt->second.find(methodName) != typeIt->second.end();
}

std::string CppTypeAnalyzer::getMethodReturnType(const std::string& typeName,
                                                 const std::string& methodName) const {
    auto typeIt = methodRegistry_.find(typeName);
    if (typeIt == methodRegistry_.end()) {
        return "";
    }

    auto methodIt = typeIt->second.find(methodName);
    if (methodIt == typeIt->second.end()) {
        return "";
    }

    return methodIt->second;
}

std::vector<std::string> CppTypeAnalyzer::parseTemplateArgs(const std::string& typeStr,
                                                            size_t& pos) {
    std::vector<std::string> args;
    std::string currentArg;
    int angleBracketDepth = 1;  // We're already inside one <

    while (pos < typeStr.length() && angleBracketDepth > 0) {
        char c = typeStr[pos];

        if (c == '<') {
            angleBracketDepth++;
            currentArg += c;
        } else if (c == '>') {
            angleBracketDepth--;
            if (angleBracketDepth > 0) {
                currentArg += c;
            } else {
                // End of template args
                // Trim and add current arg
                currentArg.erase(0, currentArg.find_first_not_of(" \t\n\r"));
                currentArg.erase(currentArg.find_last_not_of(" \t\n\r") + 1);
                if (!currentArg.empty()) {
                    args.push_back(currentArg);
                }
            }
        } else if (c == ',' && angleBracketDepth == 1) {
            // Argument separator at top level
            // Trim and add current arg
            currentArg.erase(0, currentArg.find_first_not_of(" \t\n\r"));
            currentArg.erase(currentArg.find_last_not_of(" \t\n\r") + 1);
            if (!currentArg.empty()) {
                args.push_back(currentArg);
            }
            currentArg.clear();
        } else {
            currentArg += c;
        }

        pos++;
    }

    return args;
}

std::string CppTypeAnalyzer::extractBaseName(const std::string& typeStr) {
    // Remove template arguments
    size_t templateStart = typeStr.find('<');
    if (templateStart != std::string::npos) {
        return typeStr.substr(0, templateStart);
    }
    return typeStr;
}

bool CppTypeAnalyzer::isSmartPointerType(const std::string& baseName) const {
    return baseName.find("unique_ptr") != std::string::npos ||
           baseName.find("shared_ptr") != std::string::npos ||
           baseName.find("Owned") != std::string::npos ||
           baseName == "Language::Runtime::Owned";
}

bool CppTypeAnalyzer::isValueType(const std::string& typeName) const {
    auto it = registeredTypes_.find(typeName);
    if (it != registeredTypes_.end()) {
        return it->second == CppTypeCategory::Value;
    }

    // Check for common patterns
    if (typeName == "int" || typeName == "bool" || typeName == "char" ||
        typeName == "float" || typeName == "double" ||
        typeName.find("int32_t") != std::string::npos ||
        typeName.find("int64_t") != std::string::npos ||
        typeName.find("Language::Core::") != std::string::npos) {
        return true;
    }

    return false;
}

} // namespace Core
} // namespace XXML
