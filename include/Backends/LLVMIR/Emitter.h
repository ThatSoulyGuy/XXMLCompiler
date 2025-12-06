#pragma once

#include "Backends/LLVMIR/TypedModule.h"
#include "Backends/LLVMIR/TypedInstructions.h"
#include "Backends/LLVMIR/IRVerifier.h"
#include <string>
#include <sstream>
#include <map>
#include <set>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// LLVMEmitter - Converts Typed IR to LLVM IR Text
// ============================================================================

/// The emitter traverses the typed IR and produces valid LLVM IR text output.
/// Because the input is a type-checked IR representation, the output is
/// guaranteed to be well-formed LLVM IR (assuming the typed IR was correctly
/// constructed via the type-safe builder).
///
/// When a verifier is set, the emitter will run module-level verification
/// before emission. If verification fails, the emitter will abort with
/// detailed diagnostics rather than producing potentially invalid IR.

class LLVMEmitter {
public:
    explicit LLVMEmitter(const Module& module) : module_(module) {}

    /// Set the verifier for pre-emission verification
    /// When set, emit() will call verifyModule() before producing output
    void setVerifier(IRVerifier* verifier) { verifier_ = verifier; }

    /// Get the current verifier
    IRVerifier* getVerifier() const { return verifier_; }

    /// Enable/disable pre-emission verification (verifier must be set)
    void setVerificationEnabled(bool enabled) { verificationEnabled_ = enabled; }
    bool isVerificationEnabled() const { return verificationEnabled_; }

    /// Emit the entire module to LLVM IR text
    std::string emit();

    /// Emit only function definitions (for use when preamble handles header/structs)
    std::string emitFunctionsOnly();

private:
    // Emission helpers
    void emitModuleHeader();
    void emitStructDeclarations();
    void emitGlobalVariables();
    void emitFunctionDeclarations();
    void emitFunctionDefinitions();

    void emitFunction(const Function* func);
    void emitBasicBlock(const BasicBlock* bb);
    void emitInstruction(const Instruction* inst);

    // Instruction emission
    void emitAlloca(const AllocaInst* inst);
    void emitLoad(const LoadInst* inst);
    void emitStore(const StoreInst* inst);
    void emitGEP(const GetElementPtrInst* inst);

    void emitIntBinaryOp(const IntBinaryOp* inst);
    void emitFloatBinaryOp(const FloatBinaryOp* inst);
    void emitBitwiseOp(const BitwiseOp* inst);
    void emitFloatNeg(const FloatNegInst* inst);

    void emitICmp(const ICmpInst* inst);
    void emitFCmp(const FCmpInst* inst);
    void emitPtrCmp(const PtrCmpInst* inst);

    void emitConversion(const Instruction* inst);

    void emitBranch(const BranchInst* inst);
    void emitCondBranch(const CondBranchInst* inst);
    void emitReturn(const ReturnVoidInst* inst);
    void emitReturn(const ReturnValueInst* inst);
    void emitUnreachable(const UnreachableInst* inst);

    void emitCall(const CallInst* inst);
    void emitIndirectCall(const IndirectCallInst* inst);
    void emitPHI(const Instruction* inst);
    void emitSelect(const Instruction* inst);

    // Value and type formatting
    std::string formatValue(const Value* val);
    std::string formatValueWithType(const Value* val);
    std::string formatType(const Type* type);
    std::string formatConstant(const Constant* constant);
    std::string formatLinkage(GlobalVariable::Linkage linkage);
    std::string formatCallingConv(Function::CallingConv cc);

    // Name management
    std::string getValueName(const Value* val);
    void assignValueNames(const Function* func);
    void resetValueNames();

    const Module& module_;
    std::ostringstream out_;

    // Value naming state
    std::map<const Value*, std::string> valueNames_;
    std::set<std::string> usedNames_;
    unsigned tempCounter_ = 0;
    unsigned blockCounter_ = 0;

    // Verification infrastructure
    IRVerifier* verifier_ = nullptr;
    bool verificationEnabled_ = true;
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
