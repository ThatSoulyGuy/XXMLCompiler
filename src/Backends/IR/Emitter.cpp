#include "Backends/IR/Emitter.h"
#include <iomanip>
#include <algorithm>
#include <cmath>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// Emitter Implementation
// ============================================================================

Emitter::Emitter(const Module& module) : module_(module) {}

std::string Emitter::emit() {
    stream_.str("");
    stream_.clear();
    valueNames_.clear();
    blockLabels_.clear();
    tempCounter_ = 0;
    labelCounter_ = 0;
    verifierErrors_.clear();

    // Verify if requested
    if (verifyBeforeEmit_) {
        Verifier verifier(module_);
        if (!verifier.verify()) {
            verifierErrors_ = verifier.getErrors();
            output_ = "; IR verification failed\n" + verifier.getErrorReport();
            return output_;
        }
    }

    // Emit module components
    emitModuleHeader();
    emitStructTypes();
    emitGlobalVariables();
    emitFunctionDeclarations();
    emitFunctionDefinitions();

    output_ = stream_.str();
    return output_;
}

// ============================================================================
// Module Components
// ============================================================================

void Emitter::emitModuleHeader() {
    writeLine("; ModuleID = '" + module_.getName() + "'");
    writeLine("source_filename = \"" + module_.getName() + "\"");
    writeLine("target datalayout = \"" + module_.getDataLayout() + "\"");
    writeLine("target triple = \"" + module_.getTargetTriple() + "\"");
    writeBlankLine();
}

void Emitter::emitStructTypes() {
    bool hasTypes = false;
    for (const auto& pair : module_.getStructTypes()) {
        const StructType* st = pair.second;
        if (!st) continue;

        hasTypes = true;
        stream_ << "%" << st->getName() << " = type ";

        if (st->isOpaque()) {
            stream_ << "opaque";
        } else {
            if (st->isPacked()) {
                stream_ << "<";
            }
            stream_ << "{ ";

            for (size_t i = 0; i < st->getNumElements(); ++i) {
                if (i > 0) stream_ << ", ";
                stream_ << emitType(st->getElementType(i));
            }

            stream_ << " }";
            if (st->isPacked()) {
                stream_ << ">";
            }
        }
        stream_ << "\n";
    }

    if (hasTypes) {
        writeBlankLine();
    }
}

void Emitter::emitGlobalVariables() {
    bool hasGlobals = false;
    for (const auto& pair : module_.getGlobals()) {
        const GlobalVariable* gv = pair.second.get();
        if (!gv) continue;

        hasGlobals = true;
        stream_ << "@" << gv->getName() << " = ";

        // Linkage
        std::string linkage = getLinkageString(gv->getLinkage());
        if (!linkage.empty()) {
            stream_ << linkage << " ";
        }

        // Constant/Global
        if (gv->isConstant()) {
            stream_ << "constant ";
        } else {
            stream_ << "global ";
        }

        // Type and initializer
        stream_ << emitType(gv->getValueType());

        if (const Constant* init = gv->getInitializer()) {
            stream_ << " " << emitConstant(init);
        } else {
            // External declaration
            // No initializer needed for declarations
        }

        stream_ << "\n";
    }

    if (hasGlobals) {
        writeBlankLine();
    }
}

void Emitter::emitFunctionDeclarations() {
    for (const auto& pair : module_.getFunctions()) {
        const Function* func = pair.second.get();
        if (!func || !func->isDeclaration()) continue;

        emitFunctionSignature(func, true);
        stream_ << "\n";
    }
}

void Emitter::emitFunctionDefinitions() {
    for (const auto& pair : module_.getFunctions()) {
        const Function* func = pair.second.get();
        if (!func || func->isDeclaration()) continue;

        emitFunction(func);
        writeBlankLine();
    }
}

// ============================================================================
// Type Emission
// ============================================================================

std::string Emitter::emitType(const Type* type) {
    if (!type) return "void";

    switch (type->getKind()) {
        case Type::Kind::Void:
            return "void";

        case Type::Kind::Integer: {
            const IntegerType* intTy = static_cast<const IntegerType*>(type);
            return "i" + std::to_string(intTy->getBitWidth());
        }

        case Type::Kind::Float: {
            const FloatType* floatTy = static_cast<const FloatType*>(type);
            switch (floatTy->getPrecision()) {
                case FloatType::Precision::Half:   return "half";
                case FloatType::Precision::Float:  return "float";
                case FloatType::Precision::Double: return "double";
                case FloatType::Precision::FP128:  return "fp128";
            }
            return "float";
        }

        case Type::Kind::Pointer:
            return "ptr";

        case Type::Kind::Array:
            return emitArrayType(static_cast<const ArrayType*>(type));

        case Type::Kind::Struct:
            return emitStructType(static_cast<const StructType*>(type));

        case Type::Kind::Function:
            return emitFunctionType(static_cast<const FunctionType*>(type));

        case Type::Kind::Label:
            return "label";

        default:
            return "void";
    }
}

std::string Emitter::emitStructType(const StructType* type) {
    if (type->hasName()) {
        return "%" + type->getName();
    }

    // Anonymous struct - emit inline
    std::ostringstream oss;
    if (type->isPacked()) oss << "<";
    oss << "{ ";
    for (size_t i = 0; i < type->getNumElements(); ++i) {
        if (i > 0) oss << ", ";
        oss << emitType(type->getElementType(i));
    }
    oss << " }";
    if (type->isPacked()) oss << ">";
    return oss.str();
}

std::string Emitter::emitFunctionType(const FunctionType* type) {
    std::ostringstream oss;
    oss << emitType(type->getReturnType()) << " (";
    for (size_t i = 0; i < type->getNumParams(); ++i) {
        if (i > 0) oss << ", ";
        oss << emitType(type->getParamType(i));
    }
    if (type->isVarArg()) {
        if (type->getNumParams() > 0) oss << ", ";
        oss << "...";
    }
    oss << ")";
    return oss.str();
}

std::string Emitter::emitArrayType(const ArrayType* type) {
    std::ostringstream oss;
    oss << "[" << type->getNumElements() << " x " << emitType(type->getElementType()) << "]";
    return oss.str();
}

// ============================================================================
// Value Emission
// ============================================================================

std::string Emitter::emitValue(const Value* value) {
    if (!value) return "undef";

    // Check for constants
    if (const Constant* c = dyn_cast<Constant>(value)) {
        return emitConstant(c);
    }

    // Check for global values
    if (const GlobalValue* gv = dyn_cast<GlobalValue>(value)) {
        return emitGlobalValue(gv);
    }

    // Regular value - use name or generate one
    return getValueName(value);
}

std::string Emitter::emitConstant(const Constant* constant) {
    if (!constant) return "undef";

    if (const ConstantInt* ci = dyn_cast<ConstantInt>(constant)) {
        return emitConstantInt(ci);
    }
    if (const ConstantFP* fp = dyn_cast<ConstantFP>(constant)) {
        return emitConstantFP(fp);
    }
    if (const ConstantString* str = dyn_cast<ConstantString>(constant)) {
        return emitConstantString(str);
    }
    if (const ConstantArray* arr = dyn_cast<ConstantArray>(constant)) {
        return emitConstantArray(arr);
    }
    if (const ConstantNull* null = dyn_cast<ConstantNull>(constant)) {
        return emitConstantNull(null);
    }
    if (const ConstantUndef* undef = dyn_cast<ConstantUndef>(constant)) {
        return emitConstantUndef(undef);
    }
    if (const ConstantZeroInit* zero = dyn_cast<ConstantZeroInit>(constant)) {
        return emitConstantZeroInit(zero);
    }

    return "undef";
}

std::string Emitter::emitConstantInt(const ConstantInt* ci) {
    if (ci->getBitWidth() == 1) {
        return ci->getValue() ? "true" : "false";
    }
    return std::to_string(ci->getSExtValue());
}

std::string Emitter::emitConstantFP(const ConstantFP* fp) {
    double value = fp->getValue();

    // Handle special values
    if (std::isinf(value)) {
        return value > 0 ? "0x7FF0000000000000" : "0xFFF0000000000000";
    }
    if (std::isnan(value)) {
        return "0x7FF8000000000000";
    }

    // Use hex representation for exact values
    std::ostringstream oss;
    oss << std::scientific << std::setprecision(15) << value;
    return oss.str();
}

std::string Emitter::emitConstantString(const ConstantString* str) {
    return "c\"" + escapeString(str->getValue()) + (str->isNullTerminated() ? "\\00" : "") + "\"";
}

std::string Emitter::emitConstantArray(const ConstantArray* arr) {
    std::ostringstream oss;
    oss << "[";

    Type* elemType = arr->getType() ?
        (dyn_cast<ArrayType>(arr->getType()) ?
         static_cast<const ArrayType*>(arr->getType())->getElementType() : nullptr) : nullptr;

    const auto& elements = arr->getElements();
    for (size_t i = 0; i < elements.size(); ++i) {
        if (i > 0) oss << ", ";
        if (elemType) {
            oss << emitType(elemType) << " ";
        }
        oss << emitConstant(elements[i]);
    }
    oss << "]";
    return oss.str();
}

std::string Emitter::emitConstantNull(const ConstantNull*) {
    return "null";
}

std::string Emitter::emitConstantUndef(const ConstantUndef*) {
    return "undef";
}

std::string Emitter::emitConstantZeroInit(const ConstantZeroInit*) {
    return "zeroinitializer";
}

std::string Emitter::emitGlobalValue(const GlobalValue* gv) {
    return "@" + gv->getName();
}

// ============================================================================
// Function Emission
// ============================================================================

void Emitter::emitFunction(const Function* func) {
    currentFunction_ = func;
    valueNames_.clear();
    blockLabels_.clear();
    tempCounter_ = 0;
    labelCounter_ = 0;

    // Pre-populate argument names
    for (const auto& arg : func->args()) {
        if (arg->hasName()) {
            valueNames_[arg.get()] = "%" + arg->getName();
        } else {
            valueNames_[arg.get()] = "%" + std::to_string(tempCounter_++);
        }
    }

    // Pre-populate block labels
    for (const auto& bb : *func) {
        if (bb->hasName()) {
            blockLabels_[bb.get()] = bb->getName();
        } else {
            blockLabels_[bb.get()] = std::to_string(labelCounter_++);
        }
    }

    // Pre-populate instruction names
    for (const auto& bb : *func) {
        for (const auto& inst : *bb) {
            if (inst->getType() && inst->getType()->getKind() != Type::Kind::Void) {
                if (inst->hasName() && useNamedRegisters_) {
                    valueNames_[inst.get()] = "%" + inst->getName();
                } else {
                    valueNames_[inst.get()] = "%" + std::to_string(tempCounter_++);
                }
            }
        }
    }

    // Emit signature
    emitFunctionSignature(func, false);
    stream_ << " {\n";

    // Emit blocks
    bool first = true;
    for (const auto& bb : *func) {
        if (!first) {
            stream_ << "\n";
        }
        first = false;
        emitBasicBlock(bb.get());
    }

    stream_ << "}\n";
    currentFunction_ = nullptr;
}

void Emitter::emitFunctionSignature(const Function* func, bool isDeclaration) {
    stream_ << (isDeclaration ? "declare " : "define ");

    // Linkage
    std::string linkage = getLinkageString(func->getLinkage());
    if (!linkage.empty() && linkage != "external") {
        stream_ << linkage << " ";
    }

    // Calling convention
    std::string cc = getCallingConvString(func->getCallingConv());
    if (!cc.empty()) {
        stream_ << cc << " ";
    }

    // Return type
    const FunctionType* funcType = func->getFunctionType();
    stream_ << emitType(funcType->getReturnType()) << " ";

    // Function name
    stream_ << "@" << func->getName() << "(";

    // Parameters
    for (size_t i = 0; i < funcType->getNumParams(); ++i) {
        if (i > 0) stream_ << ", ";
        stream_ << emitType(funcType->getParamType(i));

        // Include parameter name for definitions
        if (!isDeclaration && i < func->getNumArgs()) {
            const Argument* arg = func->getArg(i);
            if (arg) {
                stream_ << " " << getValueName(arg);
            }
        }
    }

    if (funcType->isVarArg()) {
        if (funcType->getNumParams() > 0) stream_ << ", ";
        stream_ << "...";
    }

    stream_ << ")";

    // Function attributes
    if (func->doesNotThrow()) {
        stream_ << " nounwind";
    }
}

void Emitter::emitBasicBlock(const BasicBlock* bb) {
    // Block label
    std::string label = getBlockLabel(bb);
    stream_ << label << ":\n";

    // Instructions
    for (const auto& inst : *bb) {
        emitInstruction(inst.get());
    }
}

void Emitter::emitInstruction(const Instruction* inst) {
    std::string result;

    switch (inst->getOpcode()) {
        case Opcode::Alloca:
            result = emitAlloca(cast<AllocaInst>(inst));
            break;
        case Opcode::Load:
            result = emitLoad(cast<LoadInst>(inst));
            break;
        case Opcode::Store:
            result = emitStore(cast<StoreInst>(inst));
            break;
        case Opcode::GetElementPtr:
            result = emitGEP(cast<GetElementPtrInst>(inst));
            break;

        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::UDiv:
        case Opcode::SRem:
        case Opcode::URem:
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
        case Opcode::FRem:
        case Opcode::Shl:
        case Opcode::LShr:
        case Opcode::AShr:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
            result = emitBinaryOp(cast<BinaryOperator>(inst));
            break;

        case Opcode::ICmp:
            result = emitICmp(cast<ICmpInst>(inst));
            break;
        case Opcode::FCmp:
            result = emitFCmp(cast<FCmpInst>(inst));
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
            result = emitCast(cast<CastInst>(inst));
            break;

        case Opcode::Br:
        case Opcode::CondBr:
            result = emitBranch(cast<BranchInst>(inst));
            break;
        case Opcode::Switch:
            result = emitSwitch(cast<SwitchInst>(inst));
            break;
        case Opcode::Ret:
            result = emitReturn(cast<ReturnInst>(inst));
            break;
        case Opcode::Unreachable:
            result = emitUnreachable(cast<UnreachableInst>(inst));
            break;

        case Opcode::PHI:
            result = emitPHI(cast<PHINode>(inst));
            break;
        case Opcode::Call:
            result = emitCall(cast<CallInst>(inst));
            break;
        case Opcode::Select:
            result = emitSelect(cast<SelectInst>(inst));
            break;

        default:
            result = "; unknown instruction";
            break;
    }

    writeIndented(result);
}

// ============================================================================
// Instruction Emission
// ============================================================================

std::string Emitter::emitAlloca(const AllocaInst* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = alloca " << emitType(inst->getAllocatedType());

    if (inst->getArraySize()) {
        oss << ", " << emitType(inst->getArraySize()->getType())
            << " " << emitValue(inst->getArraySize());
    }

    if (inst->getAlignment() > 0) {
        oss << ", align " << inst->getAlignment();
    }

    return oss.str();
}

std::string Emitter::emitLoad(const LoadInst* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = load " << emitType(inst->getLoadedType())
        << ", ptr " << emitValue(inst->getPointer());

    if (inst->getAlignment() > 0) {
        oss << ", align " << inst->getAlignment();
    }

    return oss.str();
}

std::string Emitter::emitStore(const StoreInst* inst) {
    std::ostringstream oss;
    oss << "store " << emitType(inst->getValue()->getType())
        << " " << emitValue(inst->getValue())
        << ", ptr " << emitValue(inst->getPointer());

    if (inst->getAlignment() > 0) {
        oss << ", align " << inst->getAlignment();
    }

    return oss.str();
}

std::string Emitter::emitGEP(const GetElementPtrInst* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = getelementptr ";

    if (inst->isInBounds()) {
        oss << "inbounds ";
    }

    oss << emitType(inst->getSourceElementType())
        << ", ptr " << emitValue(inst->getPointer());

    for (unsigned i = 0; i < inst->getNumIndices(); ++i) {
        Value* idx = inst->getIndex(i);
        oss << ", " << emitType(idx->getType()) << " " << emitValue(idx);
    }

    return oss.str();
}

std::string Emitter::emitBinaryOp(const BinaryOperator* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = ";

    // Opcode
    switch (inst->getOpcode()) {
        case Opcode::Add:  oss << "add"; break;
        case Opcode::Sub:  oss << "sub"; break;
        case Opcode::Mul:  oss << "mul"; break;
        case Opcode::SDiv: oss << "sdiv"; break;
        case Opcode::UDiv: oss << "udiv"; break;
        case Opcode::SRem: oss << "srem"; break;
        case Opcode::URem: oss << "urem"; break;
        case Opcode::FAdd: oss << "fadd"; break;
        case Opcode::FSub: oss << "fsub"; break;
        case Opcode::FMul: oss << "fmul"; break;
        case Opcode::FDiv: oss << "fdiv"; break;
        case Opcode::FRem: oss << "frem"; break;
        case Opcode::Shl:  oss << "shl"; break;
        case Opcode::LShr: oss << "lshr"; break;
        case Opcode::AShr: oss << "ashr"; break;
        case Opcode::And:  oss << "and"; break;
        case Opcode::Or:   oss << "or"; break;
        case Opcode::Xor:  oss << "xor"; break;
        default: oss << "unknown"; break;
    }

    // NSW/NUW flags
    if (inst->hasNoSignedWrap()) {
        oss << " nsw";
    }
    if (inst->hasNoUnsignedWrap()) {
        oss << " nuw";
    }

    oss << " " << emitType(inst->getLHS()->getType())
        << " " << emitValue(inst->getLHS())
        << ", " << emitValue(inst->getRHS());

    return oss.str();
}

std::string Emitter::emitICmp(const ICmpInst* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = icmp "
        << getICmpPredicateString(inst->getPredicate()) << " "
        << emitType(inst->getLHS()->getType())
        << " " << emitValue(inst->getLHS())
        << ", " << emitValue(inst->getRHS());
    return oss.str();
}

std::string Emitter::emitFCmp(const FCmpInst* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = fcmp "
        << getFCmpPredicateString(inst->getPredicate()) << " "
        << emitType(inst->getLHS()->getType())
        << " " << emitValue(inst->getLHS())
        << ", " << emitValue(inst->getRHS());
    return oss.str();
}

std::string Emitter::emitCast(const CastInst* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = " << getCastOpcode(inst->getOpcode()) << " "
        << emitType(inst->getSrcValue()->getType())
        << " " << emitValue(inst->getSrcValue())
        << " to " << emitType(inst->getDestType());
    return oss.str();
}

std::string Emitter::emitBranch(const BranchInst* inst) {
    std::ostringstream oss;
    if (inst->isConditional()) {
        oss << "br i1 " << emitValue(inst->getCondition())
            << ", label %" << getBlockLabel(inst->getTrueBlock())
            << ", label %" << getBlockLabel(inst->getFalseBlock());
    } else {
        oss << "br label %" << getBlockLabel(inst->getDestBlock());
    }
    return oss.str();
}

std::string Emitter::emitSwitch(const SwitchInst* inst) {
    std::ostringstream oss;
    oss << "switch " << emitType(inst->getCondition()->getType())
        << " " << emitValue(inst->getCondition())
        << ", label %" << getBlockLabel(inst->getDefaultDest()) << " [\n";

    for (const auto& caseVal : inst->getCases()) {
        oss << "    " << emitType(caseVal.first->getType())
            << " " << emitConstant(caseVal.first)
            << ", label %" << getBlockLabel(caseVal.second) << "\n";
    }

    oss << "  ]";
    return oss.str();
}

std::string Emitter::emitReturn(const ReturnInst* inst) {
    std::ostringstream oss;
    if (inst->getReturnValue()) {
        oss << "ret " << emitType(inst->getReturnValue()->getType())
            << " " << emitValue(inst->getReturnValue());
    } else {
        oss << "ret void";
    }
    return oss.str();
}

std::string Emitter::emitUnreachable(const UnreachableInst*) {
    return "unreachable";
}

std::string Emitter::emitPHI(const PHINode* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = phi " << emitType(inst->getType());

    for (unsigned i = 0; i < inst->getNumIncoming(); ++i) {
        if (i > 0) oss << ",";
        oss << " [ " << emitValue(inst->getIncomingValue(i))
            << ", %" << getBlockLabel(inst->getIncomingBlock(i)) << " ]";
    }

    return oss.str();
}

std::string Emitter::emitCall(const CallInst* inst) {
    std::ostringstream oss;

    // Result assignment (if not void)
    Type* retType = inst->getType();
    bool hasResult = retType && retType->getKind() != Type::Kind::Void;

    if (hasResult) {
        oss << getValueName(inst) << " = ";
    }

    oss << "call " << emitType(retType);

    // Callee
    if (inst->getCalledFunction()) {
        oss << " @" << inst->getCalledFunction()->getName();
    } else if (inst->getCallee()) {
        oss << " " << emitValue(inst->getCallee());
    }

    // Arguments
    oss << "(";
    for (size_t i = 0; i < inst->getNumArgs(); ++i) {
        if (i > 0) oss << ", ";
        Value* arg = inst->getArg(i);
        oss << emitType(arg->getType()) << " " << emitValue(arg);
    }
    oss << ")";

    return oss.str();
}

std::string Emitter::emitSelect(const SelectInst* inst) {
    std::ostringstream oss;
    oss << getValueName(inst) << " = select i1 " << emitValue(inst->getCondition())
        << ", " << emitType(inst->getTrueValue()->getType())
        << " " << emitValue(inst->getTrueValue())
        << ", " << emitType(inst->getFalseValue()->getType())
        << " " << emitValue(inst->getFalseValue());
    return oss.str();
}

// ============================================================================
// Helper Methods
// ============================================================================

std::string Emitter::getValueName(const Value* value) {
    if (!value) return "undef";

    auto it = valueNames_.find(value);
    if (it != valueNames_.end()) {
        return it->second;
    }

    // Generate new name
    std::string name;
    if (value->hasName() && useNamedRegisters_) {
        name = "%" + value->getName();
    } else {
        name = "%" + std::to_string(tempCounter_++);
    }

    valueNames_[value] = name;
    return name;
}

std::string Emitter::getBlockLabel(const BasicBlock* bb) {
    if (!bb) return "error";

    auto it = blockLabels_.find(bb);
    if (it != blockLabels_.end()) {
        return it->second;
    }

    // Generate new label
    std::string label;
    if (bb->hasName()) {
        label = bb->getName();
    } else {
        label = std::to_string(labelCounter_++);
    }

    blockLabels_[bb] = label;
    return label;
}

std::string Emitter::escapeString(const std::string& str) {
    std::ostringstream oss;
    for (unsigned char c : str) {
        if (c == '\\') {
            oss << "\\\\";
        } else if (c == '"') {
            oss << "\\\"";
        } else if (c >= 32 && c < 127) {
            oss << c;
        } else {
            oss << "\\";
            oss << std::hex << std::setfill('0') << std::setw(2)
                << static_cast<unsigned>(c);
        }
    }
    return oss.str();
}

std::string Emitter::getLinkageString(GlobalValue::Linkage linkage) {
    switch (linkage) {
        case GlobalValue::Linkage::External:    return "external";
        case GlobalValue::Linkage::Internal:    return "internal";
        case GlobalValue::Linkage::Private:     return "private";
        case GlobalValue::Linkage::LinkOnce:    return "linkonce";
        case GlobalValue::Linkage::Weak:        return "weak";
        case GlobalValue::Linkage::Common:      return "common";
        case GlobalValue::Linkage::Appending:   return "appending";
        case GlobalValue::Linkage::ExternWeak:  return "extern_weak";
        case GlobalValue::Linkage::AvailableExternally: return "available_externally";
    }
    return "";
}

std::string Emitter::getCallingConvString(Function::CallingConv cc) {
    switch (cc) {
        case Function::CallingConv::C:           return "";
        case Function::CallingConv::Fast:        return "fastcc";
        case Function::CallingConv::Cold:        return "coldcc";
        case Function::CallingConv::X86_StdCall: return "x86_stdcallcc";
        case Function::CallingConv::X86_FastCall: return "x86_fastcallcc";
        case Function::CallingConv::Win64:       return "win64cc";
    }
    return "";
}

std::string Emitter::getICmpPredicateString(ICmpInst::Predicate pred) {
    switch (pred) {
        case ICmpInst::Predicate::EQ:  return "eq";
        case ICmpInst::Predicate::NE:  return "ne";
        case ICmpInst::Predicate::UGT: return "ugt";
        case ICmpInst::Predicate::UGE: return "uge";
        case ICmpInst::Predicate::ULT: return "ult";
        case ICmpInst::Predicate::ULE: return "ule";
        case ICmpInst::Predicate::SGT: return "sgt";
        case ICmpInst::Predicate::SGE: return "sge";
        case ICmpInst::Predicate::SLT: return "slt";
        case ICmpInst::Predicate::SLE: return "sle";
    }
    return "eq";
}

std::string Emitter::getFCmpPredicateString(FCmpInst::Predicate pred) {
    switch (pred) {
        case FCmpInst::Predicate::OEQ: return "oeq";
        case FCmpInst::Predicate::OGT: return "ogt";
        case FCmpInst::Predicate::OGE: return "oge";
        case FCmpInst::Predicate::OLT: return "olt";
        case FCmpInst::Predicate::OLE: return "ole";
        case FCmpInst::Predicate::ONE: return "one";
        case FCmpInst::Predicate::ORD: return "ord";
        case FCmpInst::Predicate::UNO: return "uno";
        case FCmpInst::Predicate::UEQ: return "ueq";
        case FCmpInst::Predicate::UGT: return "ugt";
        case FCmpInst::Predicate::UGE: return "uge";
        case FCmpInst::Predicate::ULT: return "ult";
        case FCmpInst::Predicate::ULE: return "ule";
        case FCmpInst::Predicate::UNE: return "une";
        case FCmpInst::Predicate::AlwaysTrue: return "true";
        case FCmpInst::Predicate::AlwaysFalse: return "false";
    }
    return "oeq";
}

std::string Emitter::getCastOpcode(Opcode op) {
    switch (op) {
        case Opcode::Trunc:    return "trunc";
        case Opcode::ZExt:     return "zext";
        case Opcode::SExt:     return "sext";
        case Opcode::FPToUI:   return "fptoui";
        case Opcode::FPToSI:   return "fptosi";
        case Opcode::UIToFP:   return "uitofp";
        case Opcode::SIToFP:   return "sitofp";
        case Opcode::FPTrunc:  return "fptrunc";
        case Opcode::FPExt:    return "fpext";
        case Opcode::PtrToInt: return "ptrtoint";
        case Opcode::IntToPtr: return "inttoptr";
        case Opcode::Bitcast:  return "bitcast";
        default: return "unknown";
    }
}

void Emitter::writeLine(const std::string& line) {
    stream_ << line << "\n";
}

void Emitter::writeIndented(const std::string& line) {
    stream_ << "  " << line << "\n";
}

void Emitter::writeBlankLine() {
    stream_ << "\n";
}

// ============================================================================
// Utility Functions
// ============================================================================

std::string emitModule(const Module& module) {
    Emitter emitter(module);
    return emitter.emit();
}

std::string emitModule(const Module& module, std::string* errorMessage) {
    Emitter emitter(module);
    std::string result = emitter.emit();

    if (!emitter.getVerifierErrors().empty() && errorMessage) {
        std::ostringstream oss;
        for (const auto& error : emitter.getVerifierErrors()) {
            oss << error.toString() << "\n";
        }
        *errorMessage = oss.str();
    }

    return result;
}

std::string emitModuleUnchecked(const Module& module) {
    Emitter emitter(module);
    emitter.setVerifyBeforeEmit(false);
    return emitter.emit();
}

} // namespace IR
} // namespace Backends
} // namespace XXML
