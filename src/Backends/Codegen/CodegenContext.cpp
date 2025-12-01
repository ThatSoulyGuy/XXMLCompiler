#include "Backends/Codegen/CodegenContext.h"
#include "Core/TypeRegistry.h"
#include <algorithm>

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
}

CodegenContext::~CodegenContext() = default;

void CodegenContext::setInsertPoint(LLVMIR::BasicBlock* bb) {
    currentBlock_ = bb;
    if (bb) {
        builder_->setInsertPoint(bb);
    }
}

// === Variable Management ===

void CodegenContext::declareVariable(const std::string& name, const std::string& xxmlType,
                                     LLVMIR::AnyValue value, LLVMIR::AllocaInst* alloca) {
    if (variableScopes_.empty()) {
        variableScopes_.emplace_back();
    }
    variableScopes_.back()[name] = VariableInfo{name, xxmlType, value, alloca, false};
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

// === Type Mapping ===

LLVMIR::Type* CodegenContext::mapType(std::string_view xxmlType) {
    auto& ctx = module_->getContext();

    std::string type(xxmlType);

    // Strip ownership modifiers
    if (!type.empty() && (type.back() == '^' || type.back() == '%' || type.back() == '&')) {
        type = type.substr(0, type.length() - 1);
    }

    // Primitive types
    if (type == "Integer" || type == "Int" || type == "Int64") {
        return ctx.getInt64Ty();
    }
    if (type == "Int32" || type == "int32") {
        return ctx.getInt32Ty();
    }
    if (type == "Int16" || type == "int16") {
        return ctx.getInt16Ty();
    }
    if (type == "Int8" || type == "int8" || type == "Byte") {
        return ctx.getInt8Ty();
    }
    if (type == "Bool" || type == "Boolean") {
        return ctx.getInt1Ty();
    }
    if (type == "Float") {
        return ctx.getFloatTy();
    }
    if (type == "Double") {
        return ctx.getDoubleTy();
    }
    if (type == "Void" || type == "None" || type == "void") {
        return ctx.getVoidTy();
    }

    // All objects/classes are pointers
    return ctx.getPtrTy();
}

std::string CodegenContext::getLLVMTypeString(std::string_view xxmlType) const {
    std::string type(xxmlType);

    // Strip ownership modifiers
    if (!type.empty() && (type.back() == '^' || type.back() == '%' || type.back() == '&')) {
        type = type.substr(0, type.length() - 1);
    }

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

std::string CodegenContext::mangleFunctionName(std::string_view className, std::string_view method) const {
    std::string result(className);
    result += "_";
    result += method;

    // Replace :: with _
    size_t pos = 0;
    while ((pos = result.find("::")) != std::string::npos) {
        result.replace(pos, 2, "_");
    }

    return result;
}

std::string CodegenContext::mangleTypeName(std::string_view typeName) const {
    std::string result(typeName);

    // Replace ::, <, >, ,, space with _
    size_t pos = 0;
    while ((pos = result.find("::")) != std::string::npos) {
        result.replace(pos, 2, "_");
    }
    while ((pos = result.find("<")) != std::string::npos) {
        result.replace(pos, 1, "_");
    }
    while ((pos = result.find(">")) != std::string::npos) {
        result.erase(pos, 1);
    }
    while ((pos = result.find(",")) != std::string::npos) {
        result.replace(pos, 1, "_");
    }
    while ((pos = result.find(" ")) != std::string::npos) {
        result.erase(pos, 1);
    }

    return result;
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

// === Scope Management ===

void CodegenContext::pushScope() {
    variableScopes_.emplace_back();
}

void CodegenContext::popScope() {
    if (variableScopes_.size() > 1) {
        variableScopes_.pop_back();
    }
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
