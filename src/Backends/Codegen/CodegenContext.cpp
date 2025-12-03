#include "Backends/Codegen/CodegenContext.h"
#include "Backends/TypeNormalizer.h"
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

    // Strip ownership modifiers using TypeNormalizer
    std::string type = TypeNormalizer::stripOwnershipMarker(xxmlType);

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

std::string CodegenContext::mangleFunctionName(std::string_view className, std::string_view method) const {
    std::string result(className);
    result += "_";
    result += method;

    // Replace :: with _
    size_t pos = 0;
    while ((pos = result.find("::")) != std::string::npos) {
        result.replace(pos, 2, "_");
    }

    // Replace template characters with valid LLVM identifiers
    // < -> _LT_
    while ((pos = result.find("<")) != std::string::npos) {
        result.replace(pos, 1, "_LT_");
    }
    // > -> _GT_
    while ((pos = result.find(">")) != std::string::npos) {
        result.replace(pos, 1, "_GT_");
    }
    // , -> _C_
    while ((pos = result.find(",")) != std::string::npos) {
        result.replace(pos, 1, "_C_");
    }
    // Replace spaces
    while ((pos = result.find(" ")) != std::string::npos) {
        result.replace(pos, 1, "");
    }

    return result;
}

std::string CodegenContext::mangleTypeName(std::string_view typeName) const {
    // Use TypeNormalizer for consistent name mangling
    return TypeNormalizer::mangleForLLVM(typeName);
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
    // Strip ownership markers
    std::string baseType = typeName;
    if (!baseType.empty() && (baseType.back() == '^' || baseType.back() == '%' || baseType.back() == '&')) {
        baseType = baseType.substr(0, baseType.length() - 1);
    }

    // Primitive types don't need destruction
    if (baseType == "Integer" || baseType == "Int" || baseType == "Int64" ||
        baseType == "Int32" || baseType == "Int16" || baseType == "Int8" ||
        baseType == "Bool" || baseType == "Boolean" ||
        baseType == "Float" || baseType == "Double" ||
        baseType == "Void" || baseType == "None" || baseType == "void" ||
        baseType == "Byte" || baseType == "ptr") {
        return false;
    }

    // NativeType doesn't need destruction
    if (baseType.find("NativeType") == 0) {
        return false;
    }

    // Check if the type is a class with a Destructor defined
    auto* classInfo = getClass(baseType);
    if (classInfo) {
        // Check if destructor is defined for this class
        std::string dtorName = mangleFunctionName(baseType, "Destructor");
        return isFunctionDefined(dtorName);
    }

    // For generic template types, check if we have a destructor
    // Handle types like Collections::List<Integer>
    size_t ltPos = baseType.find('<');
    if (ltPos != std::string::npos) {
        std::string dtorName = mangleFunctionName(baseType, "Destructor");
        return isFunctionDefined(dtorName);
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

            // Get destructor function name
            std::string typeName = it->typeName;
            if (!typeName.empty() && (typeName.back() == '^' || typeName.back() == '%' || typeName.back() == '&')) {
                typeName = typeName.substr(0, typeName.length() - 1);
            }
            std::string dtorName = mangleFunctionName(typeName, "Destructor");

            // Get or declare the destructor
            auto* dtorFunc = module_->getFunction(dtorName);
            if (!dtorFunc) {
                // Declare destructor if not found
                std::vector<LLVMIR::Type*> paramTypes = { builder_->getPtrTy() };
                auto* funcType = module_->getContext().getFunctionTy(
                    module_->getContext().getVoidTy(), paramTypes, false);
                dtorFunc = module_->createFunction(funcType, dtorName,
                    LLVMIR::Function::Linkage::External);
            }

            if (dtorFunc) {
                // Call destructor
                std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(objPtr) };
                builder_->createCall(dtorFunc, args);
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

                std::string typeName = it->typeName;
                if (!typeName.empty() && (typeName.back() == '^' || typeName.back() == '%' || typeName.back() == '&')) {
                    typeName = typeName.substr(0, typeName.length() - 1);
                }
                std::string dtorName = mangleFunctionName(typeName, "Destructor");

                auto* dtorFunc = module_->getFunction(dtorName);
                if (!dtorFunc) {
                    std::vector<LLVMIR::Type*> paramTypes = { builder_->getPtrTy() };
                    auto* funcType = module_->getContext().getFunctionTy(
                        module_->getContext().getVoidTy(), paramTypes, false);
                    dtorFunc = module_->createFunction(funcType, dtorName,
                        LLVMIR::Function::Linkage::External);
                }

                if (dtorFunc) {
                    std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(objPtr) };
                    builder_->createCall(dtorFunc, args);
                }
            }
        }
    }
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

} // namespace Codegen
} // namespace Backends
} // namespace XXML
