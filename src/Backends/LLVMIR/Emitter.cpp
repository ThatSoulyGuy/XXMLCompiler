#include "Backends/LLVMIR/Emitter.h"
#include "Backends/LLVMIR/TypedBuilder.h"
#include "Backends/Codegen/RuntimeManifest.h"
#include <iomanip>
#include <algorithm>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// Main Emission Entry Point
// ============================================================================

std::string LLVMEmitter::emit() {
    // Run pre-emission verification if enabled
    // This will abort with detailed diagnostics if any errors are found
    if (verificationEnabled_ && verifier_) {
        verifier_->verifyModule();
    }

    out_.str("");
    out_.clear();

    emitModuleHeader();
    emitStructDeclarations();
    emitGlobalVariables();
    emitFunctionDeclarations();
    emitFunctionDefinitions();

    return out_.str();
}

std::string LLVMEmitter::emitFunctionsOnly() {
    out_.str("");
    out_.clear();

    // Emit struct declarations (needed for GEP instructions)
    emitStructDeclarations();

    // Emit global variables (includes string literals)
    emitGlobalVariables();

    // Emit function declarations for external functions (important for native FFI methods
    // that are created on-demand during codegen, not in the preamble)
    emitFunctionDeclarations();

    // Emit function definitions
    emitFunctionDefinitions();

    return out_.str();
}

// ============================================================================
// Module Header
// ============================================================================

void LLVMEmitter::emitModuleHeader() {
    out_ << "; ModuleID = '" << module_.getName() << "'\n";
    out_ << "source_filename = \"" << module_.getName() << "\"\n";

    if (!module_.getTargetTriple().empty()) {
        out_ << "target triple = \"" << module_.getTargetTriple() << "\"\n";
    }
    if (!module_.getDataLayout().empty()) {
        out_ << "target datalayout = \"" << module_.getDataLayout() << "\"\n";
    }
    out_ << "\n";
}

// ============================================================================
// Struct Declarations
// ============================================================================

void LLVMEmitter::emitStructDeclarations() {
    for (const auto& [name, structType] : module_.getStructs()) {
        out_ << "%" << name << " = type ";

        if (structType->isOpaque()) {
            out_ << "opaque";
        } else {
            out_ << (structType->isPacked() ? "<{" : "{");

            const auto& fields = structType->getFields();
            for (size_t i = 0; i < fields.size(); ++i) {
                if (i > 0) out_ << ", ";
                out_ << formatType(fields[i].type);
            }

            out_ << (structType->isPacked() ? "}>" : "}");
        }
        out_ << "\n";
    }

    if (!module_.getStructs().empty()) {
        out_ << "\n";
    }
}

// ============================================================================
// Global Variables
// ============================================================================

void LLVMEmitter::emitGlobalVariables() {
    for (const auto& [name, global] : module_.getGlobals()) {
        out_ << "@" << name << " = ";
        out_ << formatLinkage(global->getLinkage());

        if (global->isConstant()) {
            out_ << "constant ";
        } else {
            out_ << "global ";
        }

        out_ << formatType(global->getValueType());

        if (global->hasInitializer()) {
            out_ << " " << formatConstant(global->getInitializer());
        } else {
            out_ << " zeroinitializer";
        }

        out_ << "\n";
    }

    if (!module_.getGlobals().empty()) {
        out_ << "\n";
    }
}

// ============================================================================
// Function Declarations
// ============================================================================

void LLVMEmitter::emitFunctionDeclarations() {
    // Use RuntimeManifest to check for preamble functions precisely
    static const Codegen::RuntimeManifest runtimeManifest;

    // Preamble function prefixes - these are declared in the preamble already
    // NOTE: Only includes prefixes for functions NOT covered by RuntimeManifest
    static const std::vector<std::string> preamblePrefixes = {
        "Bool_", "Integer_", "Float_", "Double_", "String_", "Console_",
        "Object_",  // Fallback type for string operations
        "System_", "Syscall_", "Mem_",  // System-level runtime functions
        "Reflection_", "xxml_", "malloc", "free", "memcpy", "memset",
        "llvm.", "printf", "puts", "exit"
    };

    for (const auto& [name, func] : module_.getFunctions()) {
        if (func->isDeclaration()) {
            // First, check RuntimeManifest for exact match (most precise)
            if (runtimeManifest.hasFunction(name)) {
                continue;  // Skip - declared in preamble
            }

            // Then, check prefixes for other preamble functions
            bool isPreambleFunc = false;
            for (const auto& prefix : preamblePrefixes) {
                if (name.find(prefix) == 0) {
                    isPreambleFunc = true;
                    break;
                }
            }

            // Skip preamble functions - they are declared in the preamble already
            if (isPreambleFunc) {
                continue;
            }

            out_ << "declare ";
            out_ << formatType(func->getReturnType()) << " ";
            out_ << "@" << name << "(";

            const auto& paramTypes = func->getFunctionType()->getParamTypes();
            for (size_t i = 0; i < paramTypes.size(); ++i) {
                if (i > 0) out_ << ", ";
                out_ << formatType(paramTypes[i]);
            }

            if (func->getFunctionType()->isVarArg()) {
                if (!paramTypes.empty()) out_ << ", ";
                out_ << "...";
            }

            out_ << ")\n";
        }
    }

    out_ << "\n";
}

// ============================================================================
// Function Definitions
// ============================================================================

void LLVMEmitter::emitFunctionDefinitions() {
    for (const auto& [name, func] : module_.getFunctions()) {
        if (func->isDefinition()) {
            emitFunction(func.get());
        }
    }
}

void LLVMEmitter::emitFunction(const Function* func) {
    resetValueNames();
    assignValueNames(func);

    out_ << "define ";
    out_ << formatLinkage(func->getLinkage());

    if (func->getCallingConv() != Function::CallingConv::C) {
        out_ << formatCallingConv(func->getCallingConv()) << " ";
    }

    out_ << formatType(func->getReturnType()) << " ";
    out_ << "@" << func->getName() << "(";

    const auto& args = func->getArgs();
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out_ << ", ";
        out_ << formatType(args[i]->getType());
        out_ << " " << getValueName(args[i].get());
    }

    if (func->getFunctionType()->isVarArg()) {
        if (!args.empty()) out_ << ", ";
        out_ << "...";
    }

    out_ << ") {\n";

    for (const auto& bb : func->getBasicBlocks()) {
        emitBasicBlock(bb.get());
    }

    out_ << "}\n\n";
}

void LLVMEmitter::emitBasicBlock(const BasicBlock* bb) {
    std::string name = getValueName(bb);
    // Remove the % prefix for basic block labels
    if (!name.empty() && name[0] == '%') {
        name = name.substr(1);
    }

    out_ << name << ":\n";

    bool sawTerminator = false;
    for (const Instruction* inst : *bb) {
        // Skip unreachable instructions after a terminator
        if (sawTerminator) continue;

        out_ << "  ";
        emitInstruction(inst);
        out_ << "\n";

        // Check if this instruction is a terminator
        if (inst->isTerminator()) {
            sawTerminator = true;
        }
    }
}

// ============================================================================
// Instruction Emission
// ============================================================================

void LLVMEmitter::emitInstruction(const Instruction* inst) {
    switch (inst->getOpcode()) {
        case Opcode::Alloca:
            emitAlloca(static_cast<const AllocaInst*>(inst));
            break;
        case Opcode::Load:
            emitLoad(static_cast<const LoadInst*>(inst));
            break;
        case Opcode::Store:
            emitStore(static_cast<const StoreInst*>(inst));
            break;
        case Opcode::GetElementPtr:
            emitGEP(static_cast<const GetElementPtrInst*>(inst));
            break;

        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::UDiv:
        case Opcode::SRem:
        case Opcode::URem:
            emitIntBinaryOp(static_cast<const IntBinaryOp*>(inst));
            break;

        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
        case Opcode::FRem:
            emitFloatBinaryOp(static_cast<const FloatBinaryOp*>(inst));
            break;

        case Opcode::FNeg:
            emitFloatNeg(static_cast<const FloatNegInst*>(inst));
            break;

        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
            emitBitwiseOp(static_cast<const BitwiseOp*>(inst));
            break;

        case Opcode::ICmp:
            emitICmp(static_cast<const ICmpInst*>(inst));
            break;
        case Opcode::FCmp:
            emitFCmp(static_cast<const FCmpInst*>(inst));
            break;

        case Opcode::Trunc:
        case Opcode::ZExt:
        case Opcode::SExt:
        case Opcode::FPToUI:
        case Opcode::FPToSI:
        case Opcode::UIToFP:
        case Opcode::SIToFP:
        case Opcode::FPTrunc:
        case Opcode::FPExt:
        case Opcode::PtrToInt:
        case Opcode::IntToPtr:
        case Opcode::Bitcast:
            emitConversion(inst);
            break;

        case Opcode::Br:
            emitBranch(static_cast<const BranchInst*>(inst));
            break;
        case Opcode::CondBr:
            emitCondBranch(static_cast<const CondBranchInst*>(inst));
            break;
        case Opcode::Ret:
            if (auto* retVoid = dynamic_cast<const ReturnVoidInst*>(inst)) {
                emitReturn(retVoid);
            } else if (auto* retVal = dynamic_cast<const ReturnValueInst*>(inst)) {
                emitReturn(retVal);
            }
            break;
        case Opcode::Unreachable:
            emitUnreachable(static_cast<const UnreachableInst*>(inst));
            break;

        case Opcode::Call:
            emitCall(static_cast<const CallInst*>(inst));
            break;
        case Opcode::IndirectCall:
            emitIndirectCall(static_cast<const IndirectCallInst*>(inst));
            break;
        case Opcode::PHI:
            emitPHI(inst);
            break;
        case Opcode::Select:
            emitSelect(inst);
            break;

        default:
            out_ << "; unknown instruction";
            break;
    }
}

void LLVMEmitter::emitAlloca(const AllocaInst* inst) {
    out_ << getValueName(inst) << " = alloca ";
    out_ << formatType(inst->getAllocatedType());

    if (inst->hasArraySize()) {
        out_ << ", " << formatType(inst->getArraySize()->getType());
        out_ << " " << formatValue(inst->getArraySize());
    }
}

void LLVMEmitter::emitLoad(const LoadInst* inst) {
    out_ << getValueName(inst) << " = load ";
    out_ << formatType(inst->getType()) << ", ";
    out_ << formatValueWithType(inst->getPointer().raw());
}

void LLVMEmitter::emitStore(const StoreInst* inst) {
    out_ << "store ";
    out_ << formatValueWithType(inst->getValue().raw()) << ", ";
    out_ << formatValueWithType(inst->getPointer().raw());
}

void LLVMEmitter::emitGEP(const GetElementPtrInst* inst) {
    out_ << getValueName(inst) << " = getelementptr ";

    if (inst->isInBounds()) {
        out_ << "inbounds ";
    }

    out_ << formatType(inst->getSourceElementType()) << ", ";
    out_ << formatValueWithType(inst->getBasePointer().raw());

    for (Value* idx : inst->getIndices()) {
        out_ << ", " << formatValueWithType(idx);
    }
}

void LLVMEmitter::emitIntBinaryOp(const IntBinaryOp* inst) {
    out_ << getValueName(inst) << " = ";
    out_ << getOpcodeName(inst->getOpcode()) << " ";
    out_ << formatType(inst->getType()) << " ";
    out_ << formatValue(inst->getLHS().raw()) << ", ";
    out_ << formatValue(inst->getRHS().raw());
}

void LLVMEmitter::emitFloatBinaryOp(const FloatBinaryOp* inst) {
    out_ << getValueName(inst) << " = ";
    out_ << getOpcodeName(inst->getOpcode()) << " ";
    out_ << formatType(inst->getType()) << " ";
    out_ << formatValue(inst->getLHS().raw()) << ", ";
    out_ << formatValue(inst->getRHS().raw());
}

void LLVMEmitter::emitBitwiseOp(const BitwiseOp* inst) {
    out_ << getValueName(inst) << " = ";
    out_ << getOpcodeName(inst->getOpcode()) << " ";
    out_ << formatType(inst->getType()) << " ";
    out_ << formatValue(inst->getLHS().raw()) << ", ";
    out_ << formatValue(inst->getRHS().raw());
}

void LLVMEmitter::emitFloatNeg(const FloatNegInst* inst) {
    out_ << getValueName(inst) << " = fneg ";
    out_ << formatValueWithType(inst->getOperand().raw());
}

void LLVMEmitter::emitICmp(const ICmpInst* inst) {
    out_ << getValueName(inst) << " = icmp ";
    out_ << getICmpPredicateName(inst->getPredicate()) << " ";
    out_ << formatType(inst->getLHS().type()) << " ";
    out_ << formatValue(inst->getLHS().raw()) << ", ";
    out_ << formatValue(inst->getRHS().raw());
}

void LLVMEmitter::emitFCmp(const FCmpInst* inst) {
    out_ << getValueName(inst) << " = fcmp ";
    out_ << getFCmpPredicateName(inst->getPredicate()) << " ";
    out_ << formatType(inst->getLHS().type()) << " ";
    out_ << formatValue(inst->getLHS().raw()) << ", ";
    out_ << formatValue(inst->getRHS().raw());
}

void LLVMEmitter::emitPtrCmp(const PtrCmpInst* inst) {
    out_ << getValueName(inst) << " = icmp ";
    out_ << getICmpPredicateName(inst->getPredicate()) << " ";
    out_ << "ptr ";
    out_ << formatValue(inst->getLHS().raw()) << ", ";
    out_ << formatValue(inst->getRHS().raw());
}

void LLVMEmitter::emitConversion(const Instruction* inst) {
    out_ << getValueName(inst) << " = ";
    out_ << getOpcodeName(inst->getOpcode()) << " ";

    // Get source value and dest type based on instruction type
    if (auto* trunc = dynamic_cast<const TruncInst*>(inst)) {
        out_ << formatValueWithType(trunc->getSource().raw());
        out_ << " to " << formatType(trunc->getDestType());
    } else if (auto* zext = dynamic_cast<const ZExtInst*>(inst)) {
        out_ << formatValueWithType(zext->getSource().raw());
        out_ << " to " << formatType(zext->getDestType());
    } else if (auto* sext = dynamic_cast<const SExtInst*>(inst)) {
        out_ << formatValueWithType(sext->getSource().raw());
        out_ << " to " << formatType(sext->getDestType());
    } else if (auto* fptoui = dynamic_cast<const FPToUIInst*>(inst)) {
        out_ << formatValueWithType(fptoui->getSource().raw());
        out_ << " to " << formatType(fptoui->getDestType());
    } else if (auto* fptosi = dynamic_cast<const FPToSIInst*>(inst)) {
        out_ << formatValueWithType(fptosi->getSource().raw());
        out_ << " to " << formatType(fptosi->getDestType());
    } else if (auto* uitofp = dynamic_cast<const UIToFPInst*>(inst)) {
        out_ << formatValueWithType(uitofp->getSource().raw());
        out_ << " to " << formatType(uitofp->getDestType());
    } else if (auto* sitofp = dynamic_cast<const SIToFPInst*>(inst)) {
        out_ << formatValueWithType(sitofp->getSource().raw());
        out_ << " to " << formatType(sitofp->getDestType());
    } else if (auto* fptrunc = dynamic_cast<const FPTruncInst*>(inst)) {
        out_ << formatValueWithType(fptrunc->getSource().raw());
        out_ << " to " << formatType(fptrunc->getDestType());
    } else if (auto* fpext = dynamic_cast<const FPExtInst*>(inst)) {
        out_ << formatValueWithType(fpext->getSource().raw());
        out_ << " to " << formatType(fpext->getDestType());
    } else if (auto* ptrtoint = dynamic_cast<const PtrToIntInst*>(inst)) {
        out_ << formatValueWithType(ptrtoint->getSource().raw());
        out_ << " to " << formatType(ptrtoint->getDestType());
    } else if (auto* inttoptr = dynamic_cast<const IntToPtrInst*>(inst)) {
        out_ << formatValueWithType(inttoptr->getSource().raw());
        out_ << " to ptr";
    }
}

void LLVMEmitter::emitBranch(const BranchInst* inst) {
    out_ << "br label " << getValueName(inst->getDestination());
}

void LLVMEmitter::emitCondBranch(const CondBranchInst* inst) {
    out_ << "br i1 " << formatValue(inst->getCondition().raw());
    out_ << ", label " << getValueName(inst->getTrueBranch());
    out_ << ", label " << getValueName(inst->getFalseBranch());
}

void LLVMEmitter::emitReturn(const ReturnVoidInst* inst) {
    out_ << "ret void";
}

void LLVMEmitter::emitReturn(const ReturnValueInst* inst) {
    out_ << "ret " << formatValueWithType(inst->getValue().raw());
}

void LLVMEmitter::emitUnreachable(const UnreachableInst* inst) {
    out_ << "unreachable";
}

void LLVMEmitter::emitCall(const CallInst* inst) {
    Type* retType = inst->getCallee()->getReturnType();

    if (!retType->isVoid()) {
        out_ << getValueName(inst) << " = ";
    }

    out_ << "call " << formatType(retType) << " ";
    out_ << "@" << inst->getCallee()->getName() << "(";

    const auto& args = inst->getArguments();
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out_ << ", ";
        out_ << formatValueWithType(args[i]);
    }

    out_ << ")";
}

void LLVMEmitter::emitIndirectCall(const IndirectCallInst* inst) {
    Type* retType = inst->getFuncType()->getReturnType();

    if (!retType->isVoid()) {
        out_ << getValueName(inst) << " = ";
    }

    // Emit calling convention
    out_ << "call ";
    switch (inst->getCallingConv()) {
        case CallingConv::CDecl:
            out_ << "ccc ";
            break;
        case CallingConv::StdCall:
            out_ << "x86_stdcallcc ";
            break;
        case CallingConv::FastCall:
            out_ << "x86_fastcallcc ";
            break;
    }

    out_ << formatType(retType) << " ";
    out_ << formatValue(inst->getFuncPtr().raw()) << "(";

    const auto& args = inst->getArguments();
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) out_ << ", ";
        out_ << formatValueWithType(args[i]);
    }

    out_ << ")";
}

void LLVMEmitter::emitPHI(const Instruction* inst) {
    out_ << getValueName(inst) << " = phi " << formatType(inst->getType()) << " ";

    // Handle typed PHI nodes
    if (auto* intPhi = dynamic_cast<const TypedPHINode<IntValue>*>(inst)) {
        const auto& incoming = intPhi->getIncoming();
        for (size_t i = 0; i < incoming.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << "[ " << formatValue(incoming[i].first.raw());
            out_ << ", " << getValueName(incoming[i].second) << " ]";
        }
    } else if (auto* floatPhi = dynamic_cast<const TypedPHINode<FloatValue>*>(inst)) {
        const auto& incoming = floatPhi->getIncoming();
        for (size_t i = 0; i < incoming.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << "[ " << formatValue(incoming[i].first.raw());
            out_ << ", " << getValueName(incoming[i].second) << " ]";
        }
    } else if (auto* ptrPhi = dynamic_cast<const TypedPHINode<PtrValue>*>(inst)) {
        const auto& incoming = ptrPhi->getIncoming();
        for (size_t i = 0; i < incoming.size(); ++i) {
            if (i > 0) out_ << ", ";
            out_ << "[ " << formatValue(incoming[i].first.raw());
            out_ << ", " << getValueName(incoming[i].second) << " ]";
        }
    }
}

void LLVMEmitter::emitSelect(const Instruction* inst) {
    out_ << getValueName(inst) << " = select i1 ";

    if (auto* intSel = dynamic_cast<const TypedSelectInst<IntValue>*>(inst)) {
        out_ << formatValue(intSel->getCondition().raw()) << ", ";
        out_ << formatValueWithType(intSel->getTrueValue().raw()) << ", ";
        out_ << formatValueWithType(intSel->getFalseValue().raw());
    } else if (auto* floatSel = dynamic_cast<const TypedSelectInst<FloatValue>*>(inst)) {
        out_ << formatValue(floatSel->getCondition().raw()) << ", ";
        out_ << formatValueWithType(floatSel->getTrueValue().raw()) << ", ";
        out_ << formatValueWithType(floatSel->getFalseValue().raw());
    } else if (auto* ptrSel = dynamic_cast<const TypedSelectInst<PtrValue>*>(inst)) {
        out_ << formatValue(ptrSel->getCondition().raw()) << ", ";
        out_ << formatValueWithType(ptrSel->getTrueValue().raw()) << ", ";
        out_ << formatValueWithType(ptrSel->getFalseValue().raw());
    }
}

// ============================================================================
// Value Formatting
// ============================================================================

std::string LLVMEmitter::formatValue(const Value* val) {
    if (!val) return "null";

    // Constants
    if (auto* ci = dynamic_cast<const ConstantInt*>(val)) {
        return std::to_string(ci->getValue());
    }
    if (auto* cf = dynamic_cast<const ConstantFP*>(val)) {
        std::ostringstream ss;
        ss << std::scientific << std::setprecision(17) << cf->getValue();
        return ss.str();
    }
    if (dynamic_cast<const ConstantNull*>(val)) {
        return "null";
    }
    if (auto* cs = dynamic_cast<const ConstantString*>(val)) {
        std::string result = "c\"";
        for (char c : cs->getValue()) {
            if (c == '\\') result += "\\5C";
            else if (c == '"') result += "\\22";
            else if (c == '\n') result += "\\0A";
            else if (c == '\r') result += "\\0D";
            else if (c == '\t') result += "\\09";
            else if (c < 32 || c > 126) {
                char hex[5];
                snprintf(hex, sizeof(hex), "\\%02X", static_cast<unsigned char>(c));
                result += hex;
            } else {
                result += c;
            }
        }
        result += "\\00\"";  // Null terminator
        return result;
    }

    // Named values
    return getValueName(val);
}

std::string LLVMEmitter::formatValueWithType(const Value* val) {
    if (!val) return "void null";

    std::string type = formatType(val->getType());
    std::string value = formatValue(val);
    return type + " " + value;
}

std::string LLVMEmitter::formatType(const Type* type) {
    if (!type) return "void";
    return type->toLLVM();
}

std::string LLVMEmitter::formatConstant(const Constant* constant) {
    return formatValue(constant);
}

std::string LLVMEmitter::formatLinkage(GlobalVariable::Linkage linkage) {
    switch (linkage) {
        case GlobalVariable::Linkage::External: return "";
        case GlobalVariable::Linkage::Internal: return "internal ";
        case GlobalVariable::Linkage::Private: return "private ";
        case GlobalVariable::Linkage::Common: return "common ";
        case GlobalVariable::Linkage::Weak: return "weak ";
        case GlobalVariable::Linkage::DLLExport: return "dllexport ";
    }
    return "";
}

std::string LLVMEmitter::formatCallingConv(Function::CallingConv cc) {
    switch (cc) {
        case Function::CallingConv::C: return "ccc";
        case Function::CallingConv::Fast: return "fastcc";
        case Function::CallingConv::Cold: return "coldcc";
        case Function::CallingConv::X86_StdCall: return "x86_stdcallcc";
        case Function::CallingConv::X86_FastCall: return "x86_fastcallcc";
        case Function::CallingConv::Win64: return "win64cc";
    }
    return "ccc";
}

// ============================================================================
// Value Naming
// ============================================================================

std::string LLVMEmitter::getValueName(const Value* val) {
    if (!val) return "%null";

    auto it = valueNames_.find(val);
    if (it != valueNames_.end()) {
        return it->second;
    }

    // Determine prefix based on value type
    // GlobalVariable and Function use '@', local values use '%'
    std::string prefix = "%";
    if (dynamic_cast<const GlobalVariable*>(val) || dynamic_cast<const Function*>(val)) {
        prefix = "@";
    }

    // Assign a new name
    std::string name;
    if (val->hasName()) {
        std::string baseName = std::string(val->getName());
        std::string candidate = prefix + baseName;
        // Check if name is already used, if so append a number to make it unique
        if (usedNames_.count(candidate)) {
            int suffix = 1;
            do {
                candidate = prefix + baseName + "." + std::to_string(suffix++);
            } while (usedNames_.count(candidate));
        }
        name = candidate;
        usedNames_.insert(name);
    } else {
        name = "%" + std::to_string(tempCounter_++);
    }

    valueNames_[val] = name;
    return name;
}

void LLVMEmitter::assignValueNames(const Function* func) {
    // Helper to get a unique name
    auto getUniqueName = [this](const std::string& baseName) -> std::string {
        std::string candidate = "%" + baseName;
        if (usedNames_.count(candidate)) {
            int suffix = 1;
            do {
                candidate = "%" + baseName + "." + std::to_string(suffix++);
            } while (usedNames_.count(candidate));
        }
        usedNames_.insert(candidate);
        return candidate;
    };

    // Assign names to arguments
    for (const auto& arg : func->getArgs()) {
        if (arg->hasName()) {
            valueNames_[arg.get()] = getUniqueName(std::string(arg->getName()));
        } else {
            valueNames_[arg.get()] = "%" + std::to_string(tempCounter_++);
        }
    }

    // Assign names to basic blocks and instructions
    for (const auto& bb : func->getBasicBlocks()) {
        if (bb->hasName()) {
            valueNames_[bb.get()] = getUniqueName(std::string(bb->getName()));
        } else {
            valueNames_[bb.get()] = "%bb" + std::to_string(blockCounter_++);
        }

        bool sawTerminator = false;
        for (const Instruction* inst : *bb) {
            // Skip unreachable instructions after a terminator
            if (sawTerminator) continue;

            // Check if this instruction is a terminator BEFORE numbering
            // Terminators must be checked first because they don't produce SSA values
            // even if their type is non-void (e.g., ReturnValueInst stores the type
            // of the returned value, not void, but ret doesn't produce a new value)
            if (inst->isTerminator()) {
                sawTerminator = true;
                continue;  // Don't number terminator instructions
            }

            // Only name instructions that produce values
            if (inst->getType() && !inst->getType()->isVoid()) {
                if (inst->hasName()) {
                    valueNames_[inst] = getUniqueName(std::string(inst->getName()));
                } else {
                    valueNames_[inst] = "%" + std::to_string(tempCounter_++);
                }
            }
        }
    }
}

void LLVMEmitter::resetValueNames() {
    valueNames_.clear();
    usedNames_.clear();
    tempCounter_ = 0;
    blockCounter_ = 0;
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
