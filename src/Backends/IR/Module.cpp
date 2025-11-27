#include "Backends/IR/Module.h"
#include <sstream>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// Module Implementation
// ============================================================================

Module::Module(const std::string& name) : name_(name) {
    // Default target triple and data layout for x86_64 Windows
    targetTriple_ = "x86_64-pc-windows-msvc";
    dataLayout_ = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128";
}

Module::~Module() {
    // Functions and globals will be deleted by unique_ptr
}

StructType* Module::createStructType(const std::string& name) {
    if (structTypes_.find(name) != structTypes_.end()) {
        return structTypes_[name];
    }

    StructType* st = context_.createStructTy(name);
    structTypes_[name] = st;
    return st;
}

StructType* Module::getStructType(const std::string& name) const {
    auto it = structTypes_.find(name);
    return it != structTypes_.end() ? it->second : nullptr;
}

GlobalVariable* Module::addGlobalVariable(std::unique_ptr<GlobalVariable> gv) {
    const std::string& name = gv->getName();
    GlobalVariable* ptr = gv.get();
    globals_[name] = std::move(gv);
    return ptr;
}

GlobalVariable* Module::createGlobalVariable(Type* valueType, const std::string& name,
                                              GlobalValue::Linkage linkage,
                                              Constant* initializer) {
    auto gv = std::make_unique<GlobalVariable>(
        context_.getPtrTy(), valueType, name, linkage, initializer);
    return addGlobalVariable(std::move(gv));
}

GlobalVariable* Module::getGlobalVariable(const std::string& name) const {
    auto it = globals_.find(name);
    return it != globals_.end() ? it->second.get() : nullptr;
}

Function* Module::addFunction(std::unique_ptr<Function> func) {
    const std::string& name = func->getName();
    func->setParent(this);
    Function* ptr = func.get();
    functions_[name] = std::move(func);
    return ptr;
}

Function* Module::createFunction(FunctionType* funcType, const std::string& name,
                                  GlobalValue::Linkage linkage) {
    auto func = std::make_unique<Function>(funcType, name, this, linkage);
    return addFunction(std::move(func));
}

Function* Module::getFunction(const std::string& name) const {
    auto it = functions_.find(name);
    return it != functions_.end() ? it->second.get() : nullptr;
}

Function* Module::getOrInsertFunction(const std::string& name, FunctionType* funcType) {
    Function* existing = getFunction(name);
    if (existing) {
        return existing;
    }
    return createFunction(funcType, name, GlobalValue::Linkage::External);
}

GlobalVariable* Module::getOrCreateStringLiteral(const std::string& value) {
    // Check if we already have this string
    auto it = stringLiterals_.find(value);
    if (it != stringLiterals_.end()) {
        return it->second;
    }

    // Create new string literal
    std::string name = ".str." + std::to_string(stringLiteralCounter_++);

    // Create the string constant
    ConstantString* strConst = ConstantString::get(context_, value, true);

    // Create global variable
    ArrayType* arrayType = strConst->getArrayType();
    auto gv = std::make_unique<GlobalVariable>(
        context_.getPtrTy(), arrayType, name,
        GlobalValue::Linkage::Private, strConst);
    gv->setConstant(true);

    GlobalVariable* ptr = addGlobalVariable(std::move(gv));
    stringLiterals_[value] = ptr;
    return ptr;
}

Function* Module::declareFunction(const std::string& name, FunctionType* funcType) {
    Function* existing = getFunction(name);
    if (existing) {
        return existing;
    }

    // Create a declaration (no basic blocks)
    return createFunction(funcType, name, GlobalValue::Linkage::External);
}

GlobalVariable* Module::declareGlobalVariable(const std::string& name, Type* type) {
    GlobalVariable* existing = getGlobalVariable(name);
    if (existing) {
        return existing;
    }

    // Create external declaration (no initializer)
    return createGlobalVariable(type, name, GlobalValue::Linkage::External, nullptr);
}

bool Module::verify(std::string* errorMessage) const {
    // Basic verification - more detailed verification in Verifier.cpp
    std::ostringstream errors;
    bool valid = true;

    // Check all functions
    for (const auto& pair : functions_) {
        const Function* func = pair.second.get();

        // If function has a body, verify it
        if (!func->isDeclaration()) {
            // Check that entry block exists
            if (func->empty()) {
                errors << "Function '" << func->getName() << "' has no basic blocks\n";
                valid = false;
                continue;
            }

            // Check that all blocks have terminators
            for (const auto& bb : *func) {
                if (!bb->hasTerminator()) {
                    errors << "Basic block '" << bb->getName()
                           << "' in function '" << func->getName()
                           << "' has no terminator\n";
                    valid = false;
                }
            }
        }
    }

    if (errorMessage && !valid) {
        *errorMessage = errors.str();
    }

    return valid;
}

void Module::print() const {
    // Would print module to stderr/stdout for debugging
}

std::string Module::toString() const {
    // Placeholder - actual emission handled by Emitter
    return "";
}

} // namespace IR
} // namespace Backends
} // namespace XXML
