#include "Backends/Codegen/CodegenContext.h"
#include "Backends/TypeNormalizer.h"
#include "Backends/NameMangler.h"
#include "Core/TypeRegistry.h"
#include "Semantic/SemanticError.h"
#include "Semantic/SemanticAnalyzer.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <iostream>

namespace XXML {
namespace Backends {
namespace Codegen {

CodegenContext::CodegenContext(Core::CompilationContext* compCtx)
    : compCtx_(compCtx) {
    // Initialize IR infrastructure
    module_ = std::make_unique<LLVMIR::Module>("xxml_module");
    builder_ = std::make_unique<LLVMIR::IRBuilder>(*module_);

    // Initialize with one scope
    variableScopes_.emplace_back();

    // Initialize verification infrastructure (on by default)
    enableVerification(true);
}

CodegenContext::~CodegenContext() = default;

void CodegenContext::setCurrentFunction(LLVMIR::Function* func) {
    currentFunction_ = func;
    // Clear temporaries when entering a new function
    // This prevents cross-function value references which cause IR validation errors
    if (func) {
        clearTemporaries();
    }
}

void CodegenContext::setInsertPoint(LLVMIR::BasicBlock* bb) {
    currentBlock_ = bb;
    if (bb) {
        builder_->setInsertPoint(bb);
    }
}

// === Variable Management ===

void CodegenContext::declareVariable(const std::string& name, const std::string& xxmlType,
                                     LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca,
                                     bool isReference) {
    if (variableScopes_.empty()) {
        variableScopes_.emplace_back();
    }
    variableScopes_.back()[name] = VariableInfo{name, xxmlType, value, alloca, false, isReference};
    if (alloca) {
        allocas_[name] = alloca;
    }
}

void CodegenContext::declareParameter(const std::string& name, const std::string& xxmlType,
                                      LLVMIR::AnyValue value) {
    if (variableScopes_.empty()) {
        variableScopes_.emplace_back();
    }
    variableScopes_.back()[name] = VariableInfo{name, xxmlType, value, nullptr, true};
}

bool CodegenContext::hasVariable(const std::string& name) const {
    for (auto it = variableScopes_.rbegin(); it != variableScopes_.rend(); ++it) {
        if (it->find(name) != it->end()) {
            return true;
        }
    }
    return false;
}

const VariableInfo* CodegenContext::getVariable(const std::string& name) const {
    for (auto it = variableScopes_.rbegin(); it != variableScopes_.rend(); ++it) {
        auto varIt = it->find(name);
        if (varIt != it->end()) {
            return &varIt->second;
        }
    }
    return nullptr;
}

void CodegenContext::setVariableValue(const std::string& name, LLVMIR::AnyValue value) {
    for (auto it = variableScopes_.rbegin(); it != variableScopes_.rend(); ++it) {
        auto varIt = it->find(name);
        if (varIt != it->end()) {
            varIt->second.value = value;
            return;
        }
    }
}

// === Alloca Management ===

void CodegenContext::registerAlloca(const std::string& name, LLVMIR::AllocaInst* alloca) {
    allocas_[name] = alloca;
}

LLVMIR::AllocaInst* CodegenContext::getAlloca(const std::string& name) const {
    auto it = allocas_.find(name);
    return (it != allocas_.end()) ? it->second : nullptr;
}

// === Class Management ===

void CodegenContext::registerClass(const std::string& name, const ClassInfo& info) {
    classes_[name] = info;
}

const ClassInfo* CodegenContext::getClass(const std::string& name) const {
    auto it = classes_.find(name);
    return (it != classes_.end()) ? &it->second : nullptr;
}

bool CodegenContext::hasClass(const std::string& name) const {
    return classes_.find(name) != classes_.end();
}

// === NativeStructure Management ===

void CodegenContext::registerNativeStruct(const std::string& name) {
    nativeStructs_.insert(name);
}

bool CodegenContext::isNativeStruct(const std::string& name) const {
    return nativeStructs_.find(name) != nativeStructs_.end();
}

// === Type Mapping ===

LLVMIR::Type* CodegenContext::mapType(std::string_view xxmlType) {
    auto& ctx = module_->getContext();

    // Strip ownership modifiers first to get the base type
    std::string type = TypeNormalizer::stripOwnershipMarker(xxmlType);

    // NativeType is ALWAYS a primitive value type, even with ownership markers
    // e.g., NativeType<int64>^ should still be i64, not ptr
    if (type.find("NativeType<") != std::string::npos) {
        size_t start = type.find('<') + 1;
        size_t end = type.rfind('>');
        if (start < end) {
            std::string nativeType = type.substr(start, end - start);
            // Remove quotes if present
            if (nativeType.size() >= 2 && nativeType.front() == '"' && nativeType.back() == '"') {
                nativeType = nativeType.substr(1, nativeType.size() - 2);
            }
            if (nativeType == "int64") return ctx.getInt64Ty();
            if (nativeType == "int32") return ctx.getInt32Ty();
            if (nativeType == "int16") return ctx.getInt16Ty();
            if (nativeType == "int8") return ctx.getInt8Ty();
            if (nativeType == "bool") return ctx.getInt1Ty();
            if (nativeType == "float") return ctx.getFloatTy();
            if (nativeType == "double") return ctx.getDoubleTy();
            if (nativeType == "ptr" || nativeType == "cstr" || nativeType == "string_ptr") {
                return ctx.getPtrTy();
            }
        }
    }

    // Boxed types are classes in XXML - they're always pointers
    // Both Integer and Integer^ should return ptr (ownership marker stripped above)
    if (type == "Integer" || type == "Int" || type == "Int64") {
        return ctx.getPtrTy();  // Integer is a boxed class
    }
    if (type == "Bool" || type == "Boolean") {
        return ctx.getPtrTy();  // Bool is a boxed class
    }
    if (type == "Float") {
        return ctx.getPtrTy();  // Float is a boxed class
    }
    if (type == "Double") {
        return ctx.getPtrTy();  // Double is a boxed class
    }

    // Lowercase primitive type names for ABI compatibility (used in FFI/native code)
    if (type == "int32") {
        return ctx.getInt32Ty();
    }
    if (type == "int16") {
        return ctx.getInt16Ty();
    }
    if (type == "int8" || type == "Byte") {
        return ctx.getInt8Ty();
    }

    // Void types
    if (type == "Void" || type == "None" || type == "void") {
        return ctx.getVoidTy();
    }

    // Check if it's a known class type (returns pointer)
    if (hasClass(type)) {
        return ctx.getPtrTy();
    }

    // Check for template instantiation pattern (e.g., Box<Integer>)
    if (type.find('<') != std::string::npos) {
        return ctx.getPtrTy();  // Template instances are objects (pointers)
    }

    // Check for String type (builtin class)
    if (type == "String") {
        return ctx.getPtrTy();
    }

    // Check for qualified class names (contain :: but might be unregistered template base)
    // This handles cases like "Language::Collections::List" (template base without params)
    if (type.find("::") != std::string::npos) {
        return ctx.getPtrTy();  // Treat qualified names as object pointers
    }

    // If it's a simple identifier (no special chars) that starts with uppercase,
    // assume it's a class name that will be registered later (e.g., in template instantiation)
    // This handles cases like "IntKey" used as template parameter before class is registered
    if (!type.empty() && std::isupper(type[0]) && type.find_first_of("<>[]") == std::string::npos) {
        return ctx.getPtrTy();  // Treat as object pointer - will be validated at link time
    }

    // Check if it's a NativeStructure (opaque pointer type)
    // NativeStructures like _GLFWcursor are treated as pointers
    if (isNativeStruct(type)) {
        return ctx.getPtrTy();
    }

    // STRICT MODE: Unknown types are now a hard failure
    // This indicates semantic analysis did not resolve this type
    if (type != "Unknown" && !type.empty()) {
        // Throw invariant violation - unknown types should never reach codegen
        throw Semantic::UnresolvedTypeError(std::string(type));
    }

    // Empty type or "Unknown" - still a violation but with different message
    throw Semantic::UnresolvedTypeError(
        type.empty() ? "<empty>" : std::string(type));
}

std::string CodegenContext::getLLVMTypeString(std::string_view xxmlType) const {
    // Strip ownership modifiers using TypeNormalizer
    std::string type = TypeNormalizer::stripOwnershipMarker(xxmlType);

    // Primitive types
    if (type == "Integer" || type == "Int" || type == "Int64") return "i64";
    if (type == "Int32" || type == "int32") return "i32";
    if (type == "Int16" || type == "int16") return "i16";
    if (type == "Int8" || type == "int8" || type == "Byte") return "i8";
    if (type == "Bool" || type == "Boolean") return "i1";
    if (type == "Float") return "float";
    if (type == "Double") return "double";
    if (type == "Void" || type == "None" || type == "void") return "void";

    // Objects are pointers
    return "ptr";
}

std::string CodegenContext::getDefaultValue(std::string_view llvmType) const {
    if (llvmType == "i1" || llvmType == "i8" || llvmType == "i16" ||
        llvmType == "i32" || llvmType == "i64") {
        return "0";
    }
    if (llvmType == "float") return "0.0";
    if (llvmType == "double") return "0.0";
    if (llvmType == "ptr") return "null";
    return "0";
}

// === Name Mangling ===
// NOTE: Delegates to NameMangler for centralized, consistent mangling.

std::string CodegenContext::mangleFunctionName(std::string_view className, std::string_view method) const {
    return NameMangler::mangleFunctionName(className, method);
}

std::string CodegenContext::mangleTypeName(std::string_view typeName) const {
    return NameMangler::mangleForLLVM(typeName);
}

// === Method Signature Lookup ===

void CodegenContext::registerMethodReturnType(const std::string& mangledFuncName, const std::string& returnType) {
    methodReturnTypes_[mangledFuncName] = returnType;
}

std::string CodegenContext::lookupMethodReturnType(const std::string& mangledName) const {
    // Check our local registry first (populated by DeclCodegen)
    // This handles template-instantiated classes correctly without lossy demangling
    auto registryIt = methodReturnTypes_.find(mangledName);
    if (registryIt != methodReturnTypes_.end()) {
        return registryIt->second;
    }

    // Check preamble/runtime functions first
    // These are declared in PreambleGen but not registered in the module
    static const std::unordered_map<std::string, std::string> preambleFunctions = {
        // Native type constructors - these allocate and return owned pointers
        {"Bool_Constructor", "Bool^"},
        {"Integer_Constructor", "Integer^"},
        {"Float_Constructor", "Float^"},
        {"Double_Constructor", "Double^"},
        {"String_Constructor", "String^"},
        {"Byte_Constructor", "Byte^"},
        // Integer methods that return NativeType
        {"Integer_getValue", "NativeType<int64>"},
        {"Integer_toInt64", "NativeType<int64>"},
        {"Integer_toInt32", "NativeType<int32>"},
        // Float methods
        {"Float_getValue", "NativeType<float>"},
        // Double methods
        {"Double_getValue", "NativeType<double>"},
        // Bool methods
        {"Bool_getValue", "NativeType<bool>"},
        // String methods that return primitives
        {"String_length", "NativeType<int64>"},
        {"String_equals", "NativeType<bool>"},
        {"String_isEmpty", "NativeType<bool>"},
        // Memory/Syscall methods
        {"xxml_int64_read", "NativeType<int64>"},
        {"xxml_read_byte", "NativeType<int8>"},
        {"xxml_string_hash", "NativeType<int64>"},
        {"xxml_ptr_is_null", "NativeType<int64>"},
        // Reflection methods that return primitives
        {"Reflection_getTypeCount", "NativeType<int32>"},
        {"xxml_reflection_type_isTemplate", "NativeType<int64>"},
        {"xxml_reflection_type_getTemplateParamCount", "NativeType<int64>"},
        {"xxml_reflection_type_getPropertyCount", "NativeType<int64>"},
        {"xxml_reflection_type_getMethodCount", "NativeType<int64>"},
        {"xxml_reflection_type_getInstanceSize", "NativeType<int64>"},
        {"xxml_reflection_property_getOwnership", "NativeType<int64>"},
        {"xxml_reflection_property_getOffset", "NativeType<int64>"},
        {"xxml_reflection_method_getReturnOwnership", "NativeType<int64>"},
        {"xxml_reflection_method_getParameterCount", "NativeType<int64>"},
        {"xxml_reflection_method_isStatic", "NativeType<int64>"},
        {"xxml_reflection_method_isConstructor", "NativeType<int64>"},
        {"xxml_reflection_parameter_getOwnership", "NativeType<int64>"},
        // Language::Reflection::Type methods (used in reflection API)
        {"Language_Reflection_Type_getName", "String"},
        {"Language_Reflection_Type_getFullName", "String"},
        {"Language_Reflection_Type_getNamespace", "String"},
        {"Language_Reflection_Type_isTemplate", "Bool"},
        {"Language_Reflection_Type_getTemplateParameterCount", "Integer"},
        {"Language_Reflection_Type_getPropertyCount", "Integer"},
        {"Language_Reflection_Type_getPropertyAt", "Language::Reflection::PropertyInfo"},
        {"Language_Reflection_Type_getProperty", "Language::Reflection::PropertyInfo"},
        {"Language_Reflection_Type_getMethodCount", "Integer"},
        {"Language_Reflection_Type_getMethodAt", "Language::Reflection::MethodInfo"},
        {"Language_Reflection_Type_getMethod", "Language::Reflection::MethodInfo"},
        {"Language_Reflection_Type_forName", "Language::Reflection::Type"},
        {"Language_Reflection_Type_getInstanceSize", "Integer"},
        {"Language_Reflection_GetType_String_get", "Language::Reflection::Type"},
    };

    auto preambleIt = preambleFunctions.find(mangledName);
    if (preambleIt != preambleFunctions.end()) {
        return preambleIt->second;
    }

    // Special handling for Native class methods (FFI)
    // Native methods are mangled as "Native_methodName"
    // IMPORTANT: The mangling function removes consecutive underscores, so:
    //   - Method "_glfwGetTime" mangles to "Native_glfwGetTime" (not "Native__glfwGetTime")
    // We need to try both with and without leading underscore
    if (mangledName.find("Native_") == 0 && mangledName.length() > 7) {
        std::string nativeMethodName = mangledName.substr(7);  // Everything after "Native_"
        if (semanticAnalyzer_) {
            // Try exact match first (for methods without leading underscore)
            auto* methodInfo = semanticAnalyzer_->findMethod("Native", nativeMethodName);
            if (methodInfo) {
                return methodInfo->returnType;
            }
            // Try with leading underscore (mangling removes consecutive underscores)
            // e.g., "Native_glfwGetTime" -> try "_glfwGetTime"
            std::string underscoreName = "_" + nativeMethodName;
            methodInfo = semanticAnalyzer_->findMethod("Native", underscoreName);
            if (methodInfo) {
                return methodInfo->returnType;
            }
        }
    }

    // Parse mangled name: "ClassName_methodName" or "Namespace_Class_methodName"
    // Find the last underscore to split class from method
    size_t lastUnderscore = mangledName.rfind('_');
    if (lastUnderscore == std::string::npos || lastUnderscore == 0) {
        return "";  // Invalid format
    }

    std::string methodName = mangledName.substr(lastUnderscore + 1);
    std::string classPath = mangledName.substr(0, lastUnderscore);

    // Convert underscores to :: for qualified names
    std::string className = classPath;
    // Replace _ with :: for namespace separators (simple heuristic)
    // e.g., "Language_Collections_HashMap" -> "Language::Collections::HashMap"

    // Look up in semantic analyzer
    if (!semanticAnalyzer_) {
        return "";
    }

    // Try exact match first
    auto* methodInfo = semanticAnalyzer_->findMethod(className, methodName);
    if (methodInfo) {
        return methodInfo->returnType;
    }

    // Try with :: separators
    std::string qualifiedClassName = className;
    size_t pos = 0;
    while ((pos = qualifiedClassName.find('_', pos)) != std::string::npos) {
        qualifiedClassName.replace(pos, 1, "::");
        pos += 2;
    }
    methodInfo = semanticAnalyzer_->findMethod(qualifiedClassName, methodName);
    if (methodInfo) {
        return methodInfo->returnType;
    }

    return "";
}

void CodegenContext::registerMethodParameterTypes(const std::string& mangledFuncName, const std::vector<std::string>& paramTypes) {
    methodParameterTypes_[mangledFuncName] = paramTypes;
}

std::vector<std::string> CodegenContext::lookupMethodParameterTypes(const std::string& mangledName) const {
    auto it = methodParameterTypes_.find(mangledName);
    if (it != methodParameterTypes_.end()) {
        return it->second;
    }
    return {};
}

std::string CodegenContext::lookupMethodReturnTypeDirect(const std::string& className, const std::string& methodName) const {
    if (!semanticAnalyzer_) {
        return "";
    }

    // Try exact match first
    auto* methodInfo = semanticAnalyzer_->findMethod(className, methodName);
    if (methodInfo) {
        // Include ownership marker in return type
        std::string result = methodInfo->returnType;
        switch (methodInfo->returnOwnership) {
            case Parser::OwnershipType::Owned:
                result += "^";
                break;
            case Parser::OwnershipType::Reference:
                result += "&";
                break;
            case Parser::OwnershipType::Copy:
                result += "%";
                break;
            default:
                break;
        }
        return result;
    }

    // For template classes like MyClass<Integer>, also try the base template class
    // to get generic method definitions that return template parameters
    size_t templateStart = className.find('<');
    if (templateStart != std::string::npos) {
        std::string baseClass = className.substr(0, templateStart);
        methodInfo = semanticAnalyzer_->findMethod(baseClass, methodName);
        if (methodInfo) {
            // The return type might be a template parameter like "T"
            // We need to substitute it with the actual type argument
            std::string returnType = methodInfo->returnType;

            // Track the ownership marker from the method signature
            Parser::OwnershipType ownership = methodInfo->returnOwnership;

            // Extract template arguments from className (e.g., "Integer, String" from "HashMap<Integer, String>")
            size_t templateEnd = className.rfind('>');
            if (templateEnd != std::string::npos && templateEnd > templateStart) {
                std::string templateArgStr = className.substr(templateStart + 1, templateEnd - templateStart - 1);

                // Parse multiple template arguments (split by comma, respecting nested <>)
                std::vector<std::string> templateArgs;
                int depth = 0;
                std::string currentArg;
                for (char c : templateArgStr) {
                    if (c == '<') depth++;
                    else if (c == '>') depth--;
                    else if (c == ',' && depth == 0) {
                        // Trim whitespace
                        size_t start = currentArg.find_first_not_of(" \t");
                        size_t end = currentArg.find_last_not_of(" \t");
                        if (start != std::string::npos) {
                            templateArgs.push_back(currentArg.substr(start, end - start + 1));
                        }
                        currentArg.clear();
                        continue;
                    }
                    currentArg += c;
                }
                // Don't forget the last argument
                if (!currentArg.empty()) {
                    size_t start = currentArg.find_first_not_of(" \t");
                    size_t end = currentArg.find_last_not_of(" \t");
                    if (start != std::string::npos) {
                        templateArgs.push_back(currentArg.substr(start, end - start + 1));
                    }
                }

                // Get the template parameter names from the base class via public getClassRegistry()
                const auto& classRegistry = semanticAnalyzer_->getClassRegistry();
                auto classIt = classRegistry.find(baseClass);
                if (classIt != classRegistry.end() && !classIt->second.templateParams.empty()) {
                    // Check all template parameters, not just the first
                    for (size_t i = 0; i < classIt->second.templateParams.size() && i < templateArgs.size(); i++) {
                        const std::string& templateParam = classIt->second.templateParams[i].name;
                        if (returnType == templateParam || returnType == templateParam + "^" ||
                            returnType == templateParam + "&" || returnType == templateParam + "%") {
                            // Substitute template param with the actual type argument
                            char ownershipMarker = '\0';
                            if (!returnType.empty() && (returnType.back() == '^' ||
                                returnType.back() == '&' || returnType.back() == '%')) {
                                ownershipMarker = returnType.back();
                            }
                            returnType = templateArgs[i];
                            if (ownershipMarker != '\0') {
                                returnType += ownershipMarker;
                            }
                            break;  // Found the match, stop searching
                        }
                    }
                }
            }

            // Add ownership marker from the method signature if not already present
            if (!returnType.empty() && returnType.back() != '^' &&
                returnType.back() != '&' && returnType.back() != '%') {
                switch (ownership) {
                    case Parser::OwnershipType::Owned:
                        returnType += "^";
                        break;
                    case Parser::OwnershipType::Reference:
                        returnType += "&";
                        break;
                    case Parser::OwnershipType::Copy:
                        returnType += "%";
                        break;
                    default:
                        break;
                }
            }
            return returnType;
        }
    }

    // Fallback: Check for Language::Reflection intrinsic methods
    // These are implemented via syscalls and aren't in classRegistry_
    static const std::unordered_map<std::string, std::unordered_map<std::string, std::string>> reflectionMethods = {
        {"Language::Reflection::Type", {
            {"getName", "String"},
            {"getFullName", "String"},
            {"getNamespace", "String"},
            {"isTemplate", "Bool"},
            {"getTemplateParameterCount", "Integer"},
            {"getPropertyCount", "Integer"},
            {"getPropertyAt", "Language::Reflection::PropertyInfo"},
            {"getProperty", "Language::Reflection::PropertyInfo"},
            {"getMethodCount", "Integer"},
            {"getMethodAt", "Language::Reflection::MethodInfo"},
            {"getMethod", "Language::Reflection::MethodInfo"},
            {"forName", "Language::Reflection::Type"},
            {"getInstanceSize", "Integer"},
        }},
        {"Language::Reflection::GetType", {
            {"get", "Language::Reflection::Type"},
        }},
    };

    // Check Language::Reflection methods first
    auto classIt = reflectionMethods.find(className);
    if (classIt != reflectionMethods.end()) {
        auto methodIt = classIt->second.find(methodName);
        if (methodIt != classIt->second.end()) {
            return methodIt->second;
        }
    }

    // Also check with stripped template args for GetType<T> etc.
    if (templateStart != std::string::npos) {
        std::string baseClass = className.substr(0, templateStart);
        auto baseClassIt = reflectionMethods.find(baseClass);
        if (baseClassIt != reflectionMethods.end()) {
            auto methodIt = baseClassIt->second.find(methodName);
            if (methodIt != baseClassIt->second.end()) {
                return methodIt->second;
            }
        }
    }

    return "";
}

// === Loop Stack ===

void CodegenContext::pushLoop(LLVMIR::BasicBlock* condBlock, LLVMIR::BasicBlock* endBlock) {
    loopStack_.push_back({condBlock, endBlock});
}

void CodegenContext::popLoop() {
    if (!loopStack_.empty()) {
        loopStack_.pop_back();
    }
}

const LoopContext* CodegenContext::currentLoop() const {
    return loopStack_.empty() ? nullptr : &loopStack_.back();
}

// === String Literals ===

void CodegenContext::addStringLiteral(const std::string& label, const std::string& content) {
    stringLiterals_.emplace_back(label, content);
}

const std::vector<std::pair<std::string, std::string>>& CodegenContext::stringLiterals() const {
    return stringLiterals_;
}

std::string CodegenContext::allocateStringLabel() {
    return "str." + std::to_string(stringLabelCounter_++);
}

// === Lambda Management ===

void CodegenContext::registerLambda(const std::string& reg, const LambdaInfo& info) {
    lambdas_[reg] = info;
}

const LambdaInfo* CodegenContext::getLambda(const std::string& reg) const {
    auto it = lambdas_.find(reg);
    return (it != lambdas_.end()) ? &it->second : nullptr;
}

int CodegenContext::allocateLambdaId() {
    return lambdaCounter_++;
}

void CodegenContext::addPendingLambdaDefinition(const std::string& def) {
    pendingLambdaDefs_.push_back(def);
}

const std::vector<std::string>& CodegenContext::pendingLambdaDefinitions() const {
    return pendingLambdaDefs_;
}

// === Native Method/FFI Tracking ===

void CodegenContext::registerNativeMethod(const std::string& name, const NativeMethodInfo& info) {
    nativeMethods_[name] = info;
}

const NativeMethodInfo* CodegenContext::getNativeMethod(const std::string& name) const {
    auto it = nativeMethods_.find(name);
    return (it != nativeMethods_.end()) ? &it->second : nullptr;
}

// === Callback Thunk Tracking ===

void CodegenContext::registerCallbackThunk(const std::string& typeName, const CallbackThunkInfo& info) {
    callbackThunks_[typeName] = info;
}

const CallbackThunkInfo* CodegenContext::getCallbackThunk(const std::string& typeName) const {
    auto it = callbackThunks_.find(typeName);
    return (it != callbackThunks_.end()) ? &it->second : nullptr;
}

// === Enumeration Tracking ===

void CodegenContext::registerEnumValue(const std::string& fullName, int64_t value) {
    enumValues_[fullName] = value;
}

bool CodegenContext::hasEnumValue(const std::string& fullName) const {
    return enumValues_.find(fullName) != enumValues_.end();
}

int64_t CodegenContext::getEnumValue(const std::string& fullName) const {
    auto it = enumValues_.find(fullName);
    return (it != enumValues_.end()) ? it->second : 0;
}

std::optional<std::pair<std::string, int64_t>> CodegenContext::resolveEnumValue(const std::string& unqualifiedName) const {
    // First try exact match
    auto it = enumValues_.find(unqualifiedName);
    if (it != enumValues_.end()) {
        return std::make_pair(it->first, it->second);
    }

    // Search for enum value by suffix - look for entries ending with "::EnumName::Value"
    // where unqualifiedName is "EnumName::Value"
    std::string suffix = "::" + unqualifiedName;
    for (const auto& [name, value] : enumValues_) {
        // Check if name ends with the suffix (e.g., "GLFW::GamepadButton::A" ends with "::GamepadButton::A")
        if (name.size() > suffix.size() &&
            name.substr(name.size() - suffix.size()) == suffix) {
            return std::make_pair(name, value);
        }
    }

    return std::nullopt;
}

// === Label/Register Allocation ===

std::string CodegenContext::allocateRegister() {
    return "%r" + std::to_string(registerCounter_++);
}

std::string CodegenContext::allocateLabel(std::string_view prefix) {
    return std::string(prefix) + std::to_string(labelCounter_++);
}

// === Function Tracking ===

void CodegenContext::markFunctionDeclared(const std::string& name) {
    declaredFunctions_.insert(name);
}

void CodegenContext::markFunctionDefined(const std::string& name) {
    definedFunctions_.insert(name);
}

bool CodegenContext::isFunctionDeclared(const std::string& name) const {
    return declaredFunctions_.find(name) != declaredFunctions_.end();
}

bool CodegenContext::isFunctionDefined(const std::string& name) const {
    return definedFunctions_.find(name) != definedFunctions_.end();
}

// === Class Generation Tracking ===

void CodegenContext::markClassGenerated(const std::string& name) {
    generatedClasses_.insert(name);
}

bool CodegenContext::isClassGenerated(const std::string& name) const {
    return generatedClasses_.find(name) != generatedClasses_.end();
}

// === RAII Destructor Management ===

void CodegenContext::registerForDestruction(const std::string& varName,
                                             const std::string& typeName,
                                             LLVMIR::AllocaInst* alloca) {
    if (destructorScopes_.empty()) {
        destructorScopes_.push_back({});
    }
    // Only register if the type needs destruction
    if (needsDestruction(typeName)) {
        destructorScopes_.back().push_back({varName, typeName, alloca});
    }
}

bool CodegenContext::needsDestruction(const std::string& typeName) const {
    // Strip ownership markers using TypeNormalizer
    std::string baseType = TypeNormalizer::stripOwnershipMarker(typeName);

    // Only true primitives don't need destruction - these are stack-allocated
    // NativeType wrappers around raw CPU primitives (i64, i32, i1, f64, etc.)
    if (baseType == "Void" || baseType == "None" || baseType == "void" || baseType == "ptr") {
        return false;
    }

    // NativeType doesn't need destruction (raw primitive wrapper)
    if (baseType.find("NativeType") == 0) {
        return false;
    }

    // IMPORTANT: In XXML, Bool, Integer, Float, Double, Byte, String are
    // heap-allocated wrapper objects created via *_Constructor functions.
    // They DO need cleanup via xxml_free!

    // All class types need destruction (at minimum, xxml_free must be called)
    // The destructor emission code will call the destructor if it exists,
    // then always call xxml_free to release the object memory
    auto* classInfo = getClass(baseType);
    if (classInfo) {
        return true;  // All classes need cleanup (xxml_free at minimum)
    }

    // For generic template types like Collections::List<Integer>,
    // they also need cleanup
    size_t ltPos = baseType.find('<');
    if (ltPos != std::string::npos) {
        return true;  // All template instantiations need cleanup
    }

    // Native wrapper types always need destruction - they are heap-allocated
    // and created by *_Constructor functions (Integer_Constructor, Bool_Constructor, etc.)
    static const std::unordered_set<std::string> nativeWrapperTypes = {
        "String", "Integer", "Bool", "Float", "Double", "Byte"
    };
    if (nativeWrapperTypes.count(baseType) > 0) {
        return true;
    }

    return false;
}

void CodegenContext::emitScopeDestructors() {
    if (destructorScopes_.empty()) return;

    auto& scope = destructorScopes_.back();
    // LIFO order - destroy in reverse order of construction
    for (auto it = scope.rbegin(); it != scope.rend(); ++it) {
        // Get the variable's pointer value
        if (it->alloca) {
            // Load the object pointer
            auto objPtr = builder_->createLoadPtr(
                LLVMIR::PtrValue(it->alloca),
                it->varName + ".dtor_load"
            );

            // Get destructor/dispose function name - strip ownership and resolve
            std::string typeName = TypeNormalizer::stripOwnershipMarker(it->typeName);
            typeName = resolveToQualifiedName(typeName);

            // Try Destructor first, then dispose (XXML standard library convention)
            std::string dtorName = mangleFunctionName(typeName, "Destructor");
            std::string disposeName = mangleFunctionName(typeName, "dispose");

            // Get or declare the destructor/dispose function
            auto* cleanupFunc = module_->getFunction(dtorName);
            if (!cleanupFunc) {
                cleanupFunc = module_->getFunction(disposeName);
            }

            if (cleanupFunc) {
                // Call cleanup function to release internal resources
                std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(objPtr) };
                builder_->createCall(cleanupFunc, args);
            }

            // Always free the object memory itself (not just internal resources)
            auto* freeFunc = module_->getFunction("xxml_free");
            if (freeFunc) {
                std::vector<LLVMIR::AnyValue> freeArgs = { LLVMIR::AnyValue(objPtr) };
                builder_->createCall(freeFunc, freeArgs);
            }
        }
    }
}

void CodegenContext::emitAllDestructors() {
    // Emit all scopes in reverse order (for return statements)
    for (auto scopeIt = destructorScopes_.rbegin(); scopeIt != destructorScopes_.rend(); ++scopeIt) {
        for (auto it = scopeIt->rbegin(); it != scopeIt->rend(); ++it) {
            if (it->alloca) {
                auto objPtr = builder_->createLoadPtr(
                    LLVMIR::PtrValue(it->alloca),
                    it->varName + ".dtor_load"
                );

                // Strip ownership and resolve to fully qualified name
                std::string typeName = TypeNormalizer::stripOwnershipMarker(it->typeName);
                typeName = resolveToQualifiedName(typeName);

                // Try Destructor first, then dispose (XXML standard library convention)
                std::string dtorName = mangleFunctionName(typeName, "Destructor");
                std::string disposeName = mangleFunctionName(typeName, "dispose");

                auto* cleanupFunc = module_->getFunction(dtorName);
                if (!cleanupFunc) {
                    cleanupFunc = module_->getFunction(disposeName);
                }

                if (cleanupFunc) {
                    // Call cleanup function to release internal resources
                    std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(objPtr) };
                    builder_->createCall(cleanupFunc, args);
                }

                // Always free the object memory itself (not just internal resources)
                auto* freeFunc = module_->getFunction("xxml_free");
                if (freeFunc) {
                    std::vector<LLVMIR::AnyValue> freeArgs = { LLVMIR::AnyValue(objPtr) };
                    builder_->createCall(freeFunc, freeArgs);
                }
            }
        }
    }
}

void CodegenContext::emitAllDestructorsExcept(const std::string& excludeVar) {
    // Emit all scopes in reverse order, but skip the returned variable
    // This prevents destroying an object that's being returned (ownership transfer)
    for (auto scopeIt = destructorScopes_.rbegin(); scopeIt != destructorScopes_.rend(); ++scopeIt) {
        for (auto it = scopeIt->rbegin(); it != scopeIt->rend(); ++it) {
            // Skip the variable being returned (ownership transfers to caller)
            if (it->varName == excludeVar) {
                continue;
            }

            if (it->alloca) {
                auto objPtr = builder_->createLoadPtr(
                    LLVMIR::PtrValue(it->alloca),
                    it->varName + ".dtor_load"
                );

                // Strip ownership and resolve to fully qualified name
                std::string typeName = TypeNormalizer::stripOwnershipMarker(it->typeName);
                typeName = resolveToQualifiedName(typeName);

                // Try Destructor first, then dispose (XXML standard library convention)
                std::string dtorName = mangleFunctionName(typeName, "Destructor");
                std::string disposeName = mangleFunctionName(typeName, "dispose");

                auto* cleanupFunc = module_->getFunction(dtorName);
                if (!cleanupFunc) {
                    cleanupFunc = module_->getFunction(disposeName);
                }

                if (cleanupFunc) {
                    // Call cleanup function to release internal resources
                    std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(objPtr) };
                    builder_->createCall(cleanupFunc, args);
                }

                // Always free the object memory itself (not just internal resources)
                auto* freeFunc = module_->getFunction("xxml_free");
                if (freeFunc) {
                    std::vector<LLVMIR::AnyValue> freeArgs = { LLVMIR::AnyValue(objPtr) };
                    builder_->createCall(freeFunc, freeArgs);
                }
            }
        }
    }
}

// === Temporary Object Management ===

void CodegenContext::registerTemporary(const std::string& typeName, LLVMIR::AnyValue value) {
    // Only track temporaries that need destruction
    if (needsDestruction(typeName)) {
        temporaries_.push_back({typeName, value});
    }
}

void CodegenContext::unregisterTemporary(LLVMIR::AnyValue value) {
    // Remove the temporary from the list when ownership transfers to another function
    // This prevents double-free when a value is passed to a method that takes ownership
    auto it = std::remove_if(temporaries_.begin(), temporaries_.end(),
        [&value](const TemporaryInfo& info) {
            // Compare the underlying LLVM values
            return info.value.raw() == value.raw();
        });
    temporaries_.erase(it, temporaries_.end());
}

void CodegenContext::emitTemporaryCleanup() {
    if (temporaries_.empty()) return;

    // Clean up temporaries in reverse order (LIFO)
    for (auto it = temporaries_.rbegin(); it != temporaries_.rend(); ++it) {
        // Get the temporary's pointer value
        auto objPtr = it->value;

        // Strip ownership and resolve to fully qualified name
        std::string typeName = TypeNormalizer::stripOwnershipMarker(it->typeName);
        typeName = resolveToQualifiedName(typeName);

        // Try Destructor first, then dispose
        std::string dtorName = mangleFunctionName(typeName, "Destructor");
        std::string disposeName = mangleFunctionName(typeName, "dispose");

        auto* cleanupFunc = module_->getFunction(dtorName);
        if (!cleanupFunc) {
            cleanupFunc = module_->getFunction(disposeName);
        }

        if (cleanupFunc) {
            // Call cleanup function to release internal resources
            std::vector<LLVMIR::AnyValue> args = { objPtr };
            builder_->createCall(cleanupFunc, args);
        }

        // Always free the object memory itself
        auto* freeFunc = module_->getFunction("xxml_free");
        if (freeFunc) {
            std::vector<LLVMIR::AnyValue> freeArgs = { objPtr };
            builder_->createCall(freeFunc, freeArgs);
        }
    }

    // Clear the temporaries list
    temporaries_.clear();
}

void CodegenContext::clearTemporaries() {
    temporaries_.clear();
}

// === Scope Management ===

void CodegenContext::pushScope() {
    variableScopes_.emplace_back();
    destructorScopes_.push_back({});
}

void CodegenContext::popScope() {
    // Emit destructors for this scope before popping
    emitScopeDestructors();

    if (variableScopes_.size() > 1) {
        variableScopes_.pop_back();
    }
    if (!destructorScopes_.empty()) {
        destructorScopes_.pop_back();
    }
}

void CodegenContext::popScopeWithoutDestructors() {
    // Pop scope without emitting destructors (use when already emitted via emitAllDestructors)
    if (variableScopes_.size() > 1) {
        variableScopes_.pop_back();
    }
    if (!destructorScopes_.empty()) {
        destructorScopes_.pop_back();
    }
}

// === Template Parameter Substitution ===

void CodegenContext::setTemplateSubstitutions(const std::unordered_map<std::string, std::string>& subs) {
    templateSubstitutions_ = subs;
}

void CodegenContext::clearTemplateSubstitutions() {
    templateSubstitutions_.clear();
}

std::string CodegenContext::substituteTemplateParams(const std::string& typeName) const {
    // Direct match (e.g., "T" -> "Integer")
    auto it = templateSubstitutions_.find(typeName);
    if (it != templateSubstitutions_.end()) {
        return it->second;
    }

    // Handle Class@T pattern -> Class@Integer
    size_t atPos = typeName.find('@');
    if (atPos != std::string::npos) {
        std::string base = typeName.substr(0, atPos);
        std::string param = typeName.substr(atPos + 1);
        auto paramIt = templateSubstitutions_.find(param);
        if (paramIt != templateSubstitutions_.end()) {
            return base + "@" + paramIt->second;
        }
    }

    // Handle Class<T> pattern -> Class<Integer>
    size_t ltPos = typeName.find('<');
    if (ltPos != std::string::npos) {
        size_t gtPos = typeName.rfind('>');
        if (gtPos != std::string::npos && gtPos > ltPos) {
            std::string base = typeName.substr(0, ltPos);
            std::string param = typeName.substr(ltPos + 1, gtPos - ltPos - 1);
            auto paramIt = templateSubstitutions_.find(param);
            if (paramIt != templateSubstitutions_.end()) {
                return base + "<" + paramIt->second + ">";
            }
        }
    }

    // Handle namespace::Class@T pattern
    size_t colonPos = typeName.rfind("::");
    if (colonPos != std::string::npos) {
        std::string ns = typeName.substr(0, colonPos + 2);
        std::string className = typeName.substr(colonPos + 2);
        std::string substituted = substituteTemplateParams(className);
        if (substituted != className) {
            return ns + substituted;
        }
    }

    return typeName;
}

std::string CodegenContext::resolveToQualifiedName(const std::string& typeName) const {
    // Strip ownership markers first
    std::string baseType = TypeNormalizer::stripOwnershipMarker(typeName);

    // Already qualified - return as-is
    if (baseType.find("::") != std::string::npos) {
        return baseType;
    }

    // "Native" is a special FFI class that maps to external C functions
    // It should NOT be qualified with namespace to preserve FFI calling convention
    if (baseType == "Native") {
        return baseType;
    }

    // Delegate to SemanticAnalyzer if available
    if (semanticAnalyzer_) {
        std::string resolved = semanticAnalyzer_->resolveTypeArgToQualified(baseType);
        if (resolved != baseType) {
            return resolved;  // Successfully resolved
        }
    }

    // If semantic analyzer didn't resolve and we have a current namespace,
    // try prepending the current namespace (for types in same namespace)
    if (!currentNamespace_.empty()) {
        std::string qualifiedName = currentNamespace_ + "::" + baseType;
        // Check if this qualified name exists as a class
        if (hasClass(qualifiedName)) {
            return qualifiedName;
        }
    }

    // Fallback: return as-is
    return baseType;
}

// === Reflection Metadata ===

void CodegenContext::addReflectionMetadata(const std::string& fullName, const ReflectionClassMetadata& metadata) {
    // Don't overwrite if already exists
    if (reflectionMetadata_.find(fullName) == reflectionMetadata_.end()) {
        reflectionMetadata_[fullName] = metadata;
    }
}

const ReflectionClassMetadata* CodegenContext::getReflectionMetadata(const std::string& fullName) const {
    auto it = reflectionMetadata_.find(fullName);
    return (it != reflectionMetadata_.end()) ? &it->second : nullptr;
}

bool CodegenContext::hasReflectionMetadata(const std::string& fullName) const {
    return reflectionMetadata_.find(fullName) != reflectionMetadata_.end();
}

// === Annotation Metadata ===

void CodegenContext::addAnnotationMetadata(const PendingAnnotationMetadata& metadata) {
    annotationMetadata_.push_back(metadata);
}

void CodegenContext::markAnnotationRetained(const std::string& annotationName) {
    retainedAnnotations_.insert(annotationName);
}

bool CodegenContext::isAnnotationRetained(const std::string& annotationName) const {
    return retainedAnnotations_.find(annotationName) != retainedAnnotations_.end();
}

// === Deferred Type Verification ===

bool CodegenContext::verifyTypeResolved(const std::string& typeName, const std::string& context) {
    // In debug builds, assert that types are resolved.
    // This should NEVER fire if semantic verification passed - it indicates
    // a bug in the semantic verification phase.
    #ifndef NDEBUG
    if (typeName == "Deferred" || typeName.find("Deferred") == 0 ||
        typeName == "Unknown" || typeName.find("Unknown") == 0) {
        // This is a compiler bug - semantic verification should have caught this
        std::cerr << "[INTERNAL ERROR] Unresolved type '" << typeName
                  << "' reached codegen at " << context << "\n";
        std::cerr << "This indicates a bug in semantic verification.\n";
        assert(false && "Unresolved type reached codegen - semantic verification bug");
    }
    #else
    // In release builds, just return true since verification already passed
    (void)typeName;
    (void)context;
    #endif
    return true;
}

void CodegenContext::trackTypeUsage(const std::string& typeName, const std::string& location) {
    typeUsageTracking_[typeName].push_back(location);
}

bool CodegenContext::verifyAllTypesResolved() {
    bool allResolved = true;
    int deferredCount = 0;
    int unknownCount = 0;

    // Check all tracked types
    for (const auto& [typeName, locations] : typeUsageTracking_) {
        if (typeName == "Deferred" || typeName.find("Deferred") == 0) {
            deferredCount++;
            for (const auto& loc : locations) {
                std::cerr << "[ERROR] Deferred type not instantiated at: " << loc << "\n";
            }
            allResolved = false;
        }
        if (typeName == "Unknown" || typeName.find("Unknown") == 0) {
            unknownCount++;
            for (const auto& loc : locations) {
                std::cerr << "[ERROR] Unknown type at: " << loc << "\n";
            }
            allResolved = false;
        }
    }

    // Check all registered variables
    for (const auto& scope : variableScopes_) {
        for (const auto& [varName, varInfo] : scope) {
            if (varInfo.xxmlType == "Deferred" || varInfo.xxmlType.find("Deferred") == 0) {
                std::cerr << "[ERROR] Variable '" << varName
                          << "' has unresolved Deferred type\n";
                deferredCount++;
                allResolved = false;
            }
            if (varInfo.xxmlType == "Unknown" || varInfo.xxmlType.find("Unknown") == 0) {
                std::cerr << "[ERROR] Variable '" << varName
                          << "' has Unknown type\n";
                unknownCount++;
                allResolved = false;
            }
        }
    }

    // Check all registered classes
    for (const auto& [className, classInfo] : classes_) {
        for (const auto& prop : classInfo.properties) {
            if (prop.xxmlType == "Deferred" || prop.xxmlType.find("Deferred") == 0) {
                std::cerr << "[ERROR] Property '" << prop.name << "' in class '"
                          << className << "' has unresolved Deferred type\n";
                deferredCount++;
                allResolved = false;
            }
            if (prop.xxmlType == "Unknown" || prop.xxmlType.find("Unknown") == 0) {
                std::cerr << "[ERROR] Property '" << prop.name << "' in class '"
                          << className << "' has Unknown type\n";
                unknownCount++;
                allResolved = false;
            }
        }
    }

    if (!allResolved) {
        std::cerr << "[VERIFICATION FAILED] Found " << deferredCount
                  << " Deferred and " << unknownCount << " Unknown types\n";
    }

    return allResolved;
}

CodegenContext::TypeVerificationStats CodegenContext::getTypeVerificationStats() const {
    TypeVerificationStats stats;

    for (const auto& [typeName, locations] : typeUsageTracking_) {
        stats.totalTypes++;
        if (typeName == "Deferred" || typeName.find("Deferred") == 0) {
            stats.deferredTypes++;
            for (const auto& loc : locations) {
                stats.unresolvedLocations.push_back("Deferred at " + loc);
            }
        }
        if (typeName == "Unknown" || typeName.find("Unknown") == 0) {
            stats.unknownTypes++;
            for (const auto& loc : locations) {
                stats.unresolvedLocations.push_back("Unknown at " + loc);
            }
        }
    }

    // Also count from variables and classes
    for (const auto& scope : variableScopes_) {
        for (const auto& [varName, varInfo] : scope) {
            stats.totalTypes++;
            if (varInfo.xxmlType == "Deferred" || varInfo.xxmlType.find("Deferred") == 0) {
                stats.deferredTypes++;
                stats.unresolvedLocations.push_back("Deferred variable: " + varName);
            }
            if (varInfo.xxmlType == "Unknown" || varInfo.xxmlType.find("Unknown") == 0) {
                stats.unknownTypes++;
                stats.unresolvedLocations.push_back("Unknown variable: " + varName);
            }
        }
    }

    return stats;
}

// === IR Verification Infrastructure ===

void CodegenContext::enableVerification(bool enable) {
    verificationEnabled_ = enable;

    if (enable && !irVerifier_) {
        // Create verification infrastructure
        irVerifier_ = std::make_unique<LLVMIR::IRVerifier>(*module_);
        valueTracker_ = std::make_unique<LLVMIR::ValueTracker>();
        checkpointManager_ = std::make_unique<LLVMIR::CheckpointManager>(*module_);

        // Connect builder to verification infrastructure
        builder_->setVerifier(irVerifier_.get());
        builder_->setValueTracker(valueTracker_.get());
        builder_->setCheckpointManager(checkpointManager_.get());
    } else if (!enable) {
        // Disconnect from builder
        builder_->setVerifier(nullptr);
        builder_->setValueTracker(nullptr);
        builder_->setCheckpointManager(nullptr);

        // Destroy verification infrastructure
        irVerifier_.reset();
        valueTracker_.reset();
        checkpointManager_.reset();
    }
}

void CodegenContext::finalizeFunction(LLVMIR::Function* func) {
    if (!verificationEnabled_ || !irVerifier_ || !func) {
        return;
    }

    // Update value tracker with this function's context
    if (valueTracker_) {
        valueTracker_->setFunction(func);

        // Build dominance info for this function
        auto idom = irVerifier_->computeDominators(func);
        valueTracker_->setDominanceInfo(idom);
    }

    // Run function-level verification
    // This will abort with detailed diagnostics if any errors are found
    irVerifier_->verifyFunction(func);
}

void CodegenContext::finalizeModule() {
    if (!verificationEnabled_ || !irVerifier_) {
        return;
    }

    // Run module-level verification
    // This will abort with detailed diagnostics if any errors are found
    irVerifier_->verifyModule();
}

void CodegenContext::createCheckpoint(const std::string& name) {
    if (checkpointManager_) {
        checkpointManager_->createCheckpoint(name);
    }
}

LLVMIR::SnapshotDiff CodegenContext::getCheckpointDiff(const std::string& name) const {
    if (checkpointManager_) {
        return checkpointManager_->getDiffFromCheckpoint(name);
    }
    return LLVMIR::SnapshotDiff();
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
