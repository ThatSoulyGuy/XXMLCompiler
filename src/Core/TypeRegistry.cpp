#include "Core/TypeRegistry.h"
#include "Core/FormatCompat.h"
#include <algorithm>
#include <ranges>

namespace XXML::Core {

using XXML::Core::format;

// TypeInfo debug string
std::string TypeInfo::toDebugString() const {
    return format("TypeInfo(xxml='{}', cpp='{}', category={}, ownership={}, builtin={})",
                      xxmlName, cppType, static_cast<int>(category),
                      static_cast<int>(ownership), isBuiltin);
}

TypeRegistry::TypeRegistry() {
    // Constructor - types will be registered via registerBuiltinTypes()
}

void TypeRegistry::registerType(const TypeInfo& info) {
    std::lock_guard<std::mutex> lock(mutex_);
    types_[info.xxmlName] = info;
}

void TypeRegistry::registerBuiltinTypes() {
    registerCoreTypes();
    registerSystemTypes();
    registerCollectionTypes();
    registerMathTypes();
}

void TypeRegistry::registerCoreTypes() {
    // Integer type
    TypeInfo integer;
    integer.xxmlName = "Integer";
    integer.cppType = "Integer";  // Using wrapper class
    integer.llvmType = "i64";
    integer.category = TypeCategory::Class;
    integer.ownership = OwnershipSemantics::Value;
    integer.isBuiltin = true;
    registerType(integer);

    // String type
    TypeInfo string;
    string.xxmlName = "String";
    string.cppType = "String";
    string.llvmType = "ptr";  // Pointer type in LLVM
    string.category = TypeCategory::Class;
    string.ownership = OwnershipSemantics::Value;
    string.isBuiltin = true;
    registerType(string);

    // Bool type
    TypeInfo boolean;
    boolean.xxmlName = "Bool";
    boolean.cppType = "Bool";
    boolean.llvmType = "i1";
    boolean.category = TypeCategory::Class;
    boolean.ownership = OwnershipSemantics::Value;
    boolean.isBuiltin = true;
    registerType(boolean);

    // Float type
    TypeInfo floatType;
    floatType.xxmlName = "Float";
    floatType.cppType = "Float";
    floatType.llvmType = "float";
    floatType.category = TypeCategory::Class;
    floatType.ownership = OwnershipSemantics::Value;
    floatType.isBuiltin = true;
    registerType(floatType);

    // Double type
    TypeInfo doubleType;
    doubleType.xxmlName = "Double";
    doubleType.cppType = "Double";
    doubleType.llvmType = "double";
    doubleType.category = TypeCategory::Class;
    doubleType.ownership = OwnershipSemantics::Value;
    doubleType.isBuiltin = true;
    registerType(doubleType);

    // None type (unit type)
    TypeInfo none;
    none.xxmlName = "None";
    none.cppType = "None";
    none.llvmType = "void";
    none.category = TypeCategory::Class;
    none.ownership = OwnershipSemantics::Value;
    none.isBuiltin = true;
    registerType(none);
}

void TypeRegistry::registerSystemTypes() {
    // Console type
    TypeInfo console;
    console.xxmlName = "Console";
    console.cppType = "Console";
    console.category = TypeCategory::Class;
    console.ownership = OwnershipSemantics::Value;
    console.isBuiltin = true;
    registerType(console);
}

void TypeRegistry::registerCollectionTypes() {
    // Array type (template)
    TypeInfo array;
    array.xxmlName = "Array";
    array.cppType = "Array";
    array.category = TypeCategory::Template;
    array.ownership = OwnershipSemantics::Value;
    array.isBuiltin = true;
    array.isTemplate = true;
    array.templateParams = {"T"};
    registerType(array);

    // List type (template)
    TypeInfo list;
    list.xxmlName = "List";
    list.cppType = "List";
    list.category = TypeCategory::Template;
    list.ownership = OwnershipSemantics::Value;
    list.isBuiltin = true;
    list.isTemplate = true;
    list.templateParams = {"T"};
    registerType(list);

    // HashMap type (template)
    TypeInfo hashMap;
    hashMap.xxmlName = "HashMap";
    hashMap.cppType = "HashMap";
    hashMap.category = TypeCategory::Template;
    hashMap.ownership = OwnershipSemantics::Value;
    hashMap.isBuiltin = true;
    hashMap.isTemplate = true;
    hashMap.templateParams = {"K", "V"};
    registerType(hashMap);
}

void TypeRegistry::registerMathTypes() {
    // Math utility class
    TypeInfo math;
    math.xxmlName = "Math";
    math.cppType = "Math";
    math.category = TypeCategory::Class;
    math.ownership = OwnershipSemantics::Value;
    math.isBuiltin = true;
    registerType(math);
}

bool TypeRegistry::isRegistered(std::string_view typeName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Extract base type for templates (e.g., "Array<Integer>" -> "Array")
    std::string baseType = extractBaseType(typeName);

    return types_.contains(baseType);
}

const TypeInfo* TypeRegistry::getTypeInfo(std::string_view typeName) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string baseType = extractBaseType(typeName);

    auto it = types_.find(baseType);
    if (it != types_.end()) {
        return &it->second;
    }
    return nullptr;
}

TypeInfo* TypeRegistry::getTypeInfo(std::string_view typeName) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string baseType = extractBaseType(typeName);

    auto it = types_.find(baseType);
    if (it != types_.end()) {
        return &it->second;
    }
    return nullptr;
}

bool TypeRegistry::isCompatible(std::string_view from, std::string_view to) const {
    // Same type is always compatible
    if (from == to) return true;

    // Get type infos
    auto fromInfo = getTypeInfo(from);
    auto toInfo = getTypeInfo(to);

    if (!fromInfo || !toInfo) return false;

    // Primitive types can have some implicit conversions
    if (fromInfo->isPrimitive() && toInfo->isPrimitive()) {
        // Integer -> Float, Double (widening)
        if (from == "Integer" && (to == "Float" || to == "Double")) return true;
        // Float -> Double (widening)
        if (from == "Float" && to == "Double") return true;
    }

    return false;
}

bool TypeRegistry::canAssign(std::string_view from, std::string_view to) const {
    return isCompatible(from, to);
}

bool TypeRegistry::requiresConversion(std::string_view from, std::string_view to) const {
    if (from == to) return false;
    return isCompatible(from, to);
}

bool TypeRegistry::isPrimitive(std::string_view typeName) const {
    auto info = getTypeInfo(typeName);
    return info && info->isPrimitive();
}

bool TypeRegistry::isValueType(std::string_view typeName) const {
    auto info = getTypeInfo(typeName);
    return info && info->isValueType();
}

bool TypeRegistry::requiresSmartPointer(std::string_view typeName) const {
    auto info = getTypeInfo(typeName);
    return info && info->requiresSmartPointer();
}

OwnershipSemantics TypeRegistry::getOwnershipSemantics(std::string_view typeName) const {
    auto info = getTypeInfo(typeName);
    return info ? info->ownership : OwnershipSemantics::Value;
}

std::string TypeRegistry::getCppType(std::string_view xxmlType) const {
    auto info = getTypeInfo(xxmlType);
    return info ? info->cppType : std::string(xxmlType);
}

std::string TypeRegistry::getLLVMType(std::string_view xxmlType) const {
    auto info = getTypeInfo(xxmlType);
    return info && !info->llvmType.empty() ? info->llvmType : "ptr";
}

bool TypeRegistry::isTemplate(std::string_view typeName) const {
    auto info = getTypeInfo(typeName);
    return info && info->isTemplate;
}

std::string TypeRegistry::instantiateTemplate(std::string_view templateName,
                                             const std::vector<std::string>& args) const {
    // For now, simple concatenation: "Array<Integer>"
    std::string result{templateName};
    if (!args.empty()) {
        result += "<";
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) result += ", ";
            result += args[i];
        }
        result += ">";
    }
    return result;
}

bool TypeRegistry::hasOperatorOverload(std::string_view typeName,
                                      std::string_view op,
                                      std::string_view rightType) const {
    auto info = getTypeInfo(typeName);
    if (!info) return false;

    for (const auto& overload : info->operatorOverloads) {
        if (overload.op == op && overload.rightType == rightType) {
            return true;
        }
    }
    return false;
}

std::optional<TypeInfo::OperatorOverload>
TypeRegistry::getOperatorOverload(std::string_view typeName,
                                 std::string_view op,
                                 std::string_view rightType) const {
    auto info = getTypeInfo(typeName);
    if (!info) return std::nullopt;

    for (const auto& overload : info->operatorOverloads) {
        if (overload.op == op && overload.rightType == rightType) {
            return overload;
        }
    }
    return std::nullopt;
}

std::vector<std::string> TypeRegistry::getAllRegisteredTypes() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(types_.size());

    for (const auto& [name, _] : types_) {
        result.push_back(name);
    }

    std::ranges::sort(result);
    return result;
}

void TypeRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    types_.clear();
}

size_t TypeRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return types_.size();
}

size_t TypeRegistry::builtinCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return std::ranges::count_if(types_ | std::views::values,
                                  [](const TypeInfo& info) { return info.isBuiltin; });
}

size_t TypeRegistry::userDefinedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    return std::ranges::count_if(types_ | std::views::values,
                                  [](const TypeInfo& info) { return !info.isBuiltin; });
}

std::string TypeRegistry::extractBaseType(std::string_view fullType) const {
    // Extract base type from template instantiation
    // e.g., "Array<Integer>" -> "Array"
    auto pos = fullType.find('<');
    if (pos != std::string_view::npos) {
        return std::string(fullType.substr(0, pos));
    }
    return std::string(fullType);
}

bool TypeRegistry::isGenericTemplateType(std::string_view typeName) const {
    return typeName.find('<') != std::string_view::npos;
}

} // namespace XXML::Core
