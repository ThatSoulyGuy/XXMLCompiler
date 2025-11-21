#pragma once

#include "Backends/LLVMType.h"
#include "Backends/LLVMValue.h"
#include "Backends/ValueTracker.h"
#include "Backends/TypeSafetyChecker.h"
#include "Backends/SSAVerifier.h"
#include "Backends/DestructorManager.h"
#include <string>
#include <vector>
#include <sstream>

namespace XXML {
namespace Backends {

/**
 * Main infrastructure for building LLVM IR instructions
 */
class IRBuilder {
public:
    IRBuilder(ValueTracker& tracker, TypeSafetyChecker& checker,
              SSAVerifier& verifier, DestructorManager& destructor)
        : tracker_(tracker), checker_(checker), verifier_(verifier),
          destructor_(destructor), nextRegister_(0) {}

    // Register allocation
    std::string allocateRegister();

    // Basic instructions
    std::string emitAlloca(const std::string& type, const std::string& name = "");
    std::string emitStore(const LLVMValue& value, const std::string& ptr);
    std::string emitLoad(const LLVMType& type, const std::string& ptr, const std::string& reg = "");
    std::string emitCall(const std::string& function, const LLVMType& returnType,
                        const std::vector<LLVMValue>& args);
    std::string emitRet(const LLVMValue& value);
    std::string emitRetVoid();
    std::string emitBr(const std::string& label);
    std::string emitCondBr(const LLVMValue& cond, const std::string& trueLabel,
                          const std::string& falseLabel);

    // Arithmetic operations
    std::string emitAdd(const LLVMValue& left, const LLVMValue& right, const std::string& reg = "");
    std::string emitSub(const LLVMValue& left, const LLVMValue& right, const std::string& reg = "");
    std::string emitMul(const LLVMValue& left, const LLVMValue& right, const std::string& reg = "");
    std::string emitSDiv(const LLVMValue& left, const LLVMValue& right, const std::string& reg = "");

    // Comparison operations
    std::string emitICmp(const std::string& predicate, const LLVMValue& left,
                        const LLVMValue& right, const std::string& reg = "");

    // Type conversions
    std::string emitIntToPtr(const LLVMValue& value, const std::string& reg = "");
    std::string emitPtrToInt(const LLVMValue& value, const std::string& reg = "");
    std::string emitBitcast(const LLVMValue& value, const LLVMType& targetType,
                           const std::string& reg = "");

    // GEP (GetElementPtr)
    std::string emitGEP(const LLVMType& type, const std::string& ptr,
                       const std::vector<std::string>& indices, const std::string& reg = "");

    // Labels
    std::string emitLabel(const std::string& name);

    // Comments
    std::string emitComment(const std::string& text);

    // Register tracking
    void defineRegister(const std::string& reg, const LLVMType& type);
    LLVMType getRegisterType(const std::string& reg) const;

    // Reset state
    void reset();

private:
    ValueTracker& tracker_;
    TypeSafetyChecker& checker_;
    SSAVerifier& verifier_;
    DestructorManager& destructor_;
    int nextRegister_;
};

} // namespace Backends
} // namespace XXML
