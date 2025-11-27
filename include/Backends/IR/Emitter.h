#pragma once

#include "Backends/IR/Module.h"
#include "Backends/IR/Verifier.h"
#include <string>
#include <sstream>
#include <unordered_map>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// Emitter - Convert IR to LLVM IR text format
// ============================================================================

class Emitter {
public:
    explicit Emitter(const Module& module);

    // Main emission method
    std::string emit();

    // Configuration
    void setVerifyBeforeEmit(bool verify) { verifyBeforeEmit_ = verify; }
    void setEmitComments(bool comments) { emitComments_ = comments; }
    void setUseNamedRegisters(bool named) { useNamedRegisters_ = named; }

    // Get emitted output
    const std::string& getOutput() const { return output_; }

    // Errors from verification
    const std::vector<VerifierError>& getVerifierErrors() const { return verifierErrors_; }

private:
    // Module components
    void emitModuleHeader();
    void emitStructTypes();
    void emitGlobalVariables();
    void emitFunctionDeclarations();
    void emitFunctionDefinitions();

    // Type emission
    std::string emitType(const Type* type);
    std::string emitStructType(const StructType* type);
    std::string emitFunctionType(const FunctionType* type);
    std::string emitArrayType(const ArrayType* type);

    // Value emission
    std::string emitValue(const Value* value);
    std::string emitConstant(const Constant* constant);
    std::string emitConstantInt(const ConstantInt* ci);
    std::string emitConstantFP(const ConstantFP* fp);
    std::string emitConstantString(const ConstantString* str);
    std::string emitConstantArray(const ConstantArray* arr);
    std::string emitConstantNull(const ConstantNull* null);
    std::string emitConstantUndef(const ConstantUndef* undef);
    std::string emitConstantZeroInit(const ConstantZeroInit* zero);
    std::string emitGlobalValue(const GlobalValue* gv);

    // Function emission
    void emitFunction(const Function* func);
    void emitFunctionSignature(const Function* func, bool isDeclaration);
    void emitBasicBlock(const BasicBlock* bb);
    void emitInstruction(const Instruction* inst);

    // Instruction emission
    std::string emitAlloca(const AllocaInst* inst);
    std::string emitLoad(const LoadInst* inst);
    std::string emitStore(const StoreInst* inst);
    std::string emitGEP(const GetElementPtrInst* inst);
    std::string emitBinaryOp(const BinaryOperator* inst);
    std::string emitICmp(const ICmpInst* inst);
    std::string emitFCmp(const FCmpInst* inst);
    std::string emitCast(const CastInst* inst);
    std::string emitBranch(const BranchInst* inst);
    std::string emitSwitch(const SwitchInst* inst);
    std::string emitReturn(const ReturnInst* inst);
    std::string emitUnreachable(const UnreachableInst* inst);
    std::string emitPHI(const PHINode* inst);
    std::string emitCall(const CallInst* inst);
    std::string emitSelect(const SelectInst* inst);

    // Helper methods
    std::string getValueName(const Value* value);
    std::string getBlockLabel(const BasicBlock* bb);
    std::string escapeString(const std::string& str);
    std::string getLinkageString(GlobalValue::Linkage linkage);
    std::string getCallingConvString(Function::CallingConv cc);
    std::string getICmpPredicateString(ICmpInst::Predicate pred);
    std::string getFCmpPredicateString(FCmpInst::Predicate pred);
    std::string getCastOpcode(Opcode op);

    // Output helpers
    void writeLine(const std::string& line);
    void writeIndented(const std::string& line);
    void writeBlankLine();

    const Module& module_;
    std::ostringstream stream_;
    std::string output_;

    // Name generation
    std::unordered_map<const Value*, std::string> valueNames_;
    std::unordered_map<const BasicBlock*, std::string> blockLabels_;
    unsigned tempCounter_ = 0;
    unsigned labelCounter_ = 0;

    // Configuration
    bool verifyBeforeEmit_ = true;
    bool emitComments_ = false;
    bool useNamedRegisters_ = true;

    // Verification errors
    std::vector<VerifierError> verifierErrors_;

    // Current context
    const Function* currentFunction_ = nullptr;
};

// ============================================================================
// Utility functions
// ============================================================================

// Quick emit (returns empty string on verification failure)
std::string emitModule(const Module& module);

// Emit with error handling
std::string emitModule(const Module& module, std::string* errorMessage);

// Emit without verification
std::string emitModuleUnchecked(const Module& module);

} // namespace IR
} // namespace Backends
} // namespace XXML
