#include "Backends/IRBuilder.h"
#include <sstream>

namespace XXML {
namespace Backends {

std::string IRBuilder::allocateRegister() {
    return "%r" + std::to_string(nextRegister_++);
}

std::string IRBuilder::emitAlloca(const std::string& type, const std::string& name) {
    std::string reg = name.empty() ? allocateRegister() : name;
    std::ostringstream ir;
    ir << reg << " = alloca " << type;

    verifier_.defineRegister(reg, LLVMType::getPointerType());
    return ir.str();
}

std::string IRBuilder::emitStore(const LLVMValue& value, const std::string& ptr) {
    std::ostringstream ir;
    ir << "store " << value.getType().toString() << " " << value.getName()
       << ", ptr " << ptr;
    return ir.str();
}

std::string IRBuilder::emitLoad(const LLVMType& type, const std::string& ptr,
                                const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = load " << type.toString() << ", ptr " << ptr;

    verifier_.defineRegister(resultReg, type);
    return ir.str();
}

std::string IRBuilder::emitCall(const std::string& function, const LLVMType& returnType,
                                const std::vector<LLVMValue>& args) {
    std::ostringstream ir;

    if (returnType.isVoid()) {
        ir << "call void @" << function << "(";
    } else {
        std::string reg = allocateRegister();
        ir << reg << " = call " << returnType.toString() << " @" << function << "(";
        verifier_.defineRegister(reg, returnType);
    }

    // Arguments
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) ir << ", ";
        ir << args[i].getType().toString() << " " << args[i].getName();
    }
    ir << ")";

    return ir.str();
}

std::string IRBuilder::emitRet(const LLVMValue& value) {
    std::ostringstream ir;
    ir << "ret " << value.getType().toString() << " " << value.getName();
    return ir.str();
}

std::string IRBuilder::emitRetVoid() {
    return "ret void";
}

std::string IRBuilder::emitBr(const std::string & label) {
    return "br label %" + label;
}

std::string IRBuilder::emitCondBr(const LLVMValue& cond, const std::string& trueLabel,
                                  const std::string& falseLabel) {
    std::ostringstream ir;
    ir << "br " << cond.getType().toString() << " " << cond.getName()
       << ", label %" << trueLabel << ", label %" << falseLabel;
    return ir.str();
}

std::string IRBuilder::emitAdd(const LLVMValue& left, const LLVMValue& right,
                               const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = add " << left.getType().toString() << " "
       << left.getName() << ", " << right.getName();

    verifier_.defineRegister(resultReg, left.getType());
    return ir.str();
}

std::string IRBuilder::emitSub(const LLVMValue& left, const LLVMValue& right,
                               const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = sub " << left.getType().toString() << " "
       << left.getName() << ", " << right.getName();

    verifier_.defineRegister(resultReg, left.getType());
    return ir.str();
}

std::string IRBuilder::emitMul(const LLVMValue& left, const LLVMValue& right,
                               const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = mul " << left.getType().toString() << " "
       << left.getName() << ", " << right.getName();

    verifier_.defineRegister(resultReg, left.getType());
    return ir.str();
}

std::string IRBuilder::emitSDiv(const LLVMValue& left, const LLVMValue& right,
                                const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = sdiv " << left.getType().toString() << " "
       << left.getName() << ", " << right.getName();

    verifier_.defineRegister(resultReg, left.getType());
    return ir.str();
}

std::string IRBuilder::emitICmp(const std::string& predicate, const LLVMValue& left,
                                const LLVMValue& right, const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = icmp " << predicate << " " << left.getType().toString()
       << " " << left.getName() << ", " << right.getName();

    verifier_.defineRegister(resultReg, LLVMType::getI1Type());
    return ir.str();
}

std::string IRBuilder::emitIntToPtr(const LLVMValue& value, const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = inttoptr " << value.getType().toString() << " "
       << value.getName() << " to ptr";

    verifier_.defineRegister(resultReg, LLVMType::getPointerType());
    return ir.str();
}

std::string IRBuilder::emitPtrToInt(const LLVMValue& value, const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = ptrtoint ptr " << value.getName() << " to i64";

    verifier_.defineRegister(resultReg, LLVMType::getI64Type());
    return ir.str();
}

std::string IRBuilder::emitBitcast(const LLVMValue& value, const LLVMType& targetType,
                                   const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = bitcast " << value.getType().toString() << " "
       << value.getName() << " to " << targetType.toString();

    verifier_.defineRegister(resultReg, targetType);
    return ir.str();
}

std::string IRBuilder::emitGEP(const LLVMType& type, const std::string& ptr,
                               const std::vector<std::string>& indices,
                               const std::string& reg) {
    std::string resultReg = reg.empty() ? allocateRegister() : reg;
    std::ostringstream ir;
    ir << resultReg << " = getelementptr " << type.toString() << ", ptr " << ptr;

    for (const auto& idx : indices) {
        ir << ", i32 " << idx;
    }

    verifier_.defineRegister(resultReg, LLVMType::getPointerType());
    return ir.str();
}

std::string IRBuilder::emitLabel(const std::string& name) {
    return name + ":";
}

std::string IRBuilder::emitComment(const std::string& text) {
    return "; " + text;
}

void IRBuilder::defineRegister(const std::string& reg, const LLVMType& type) {
    verifier_.defineRegister(reg, type);
}

LLVMType IRBuilder::getRegisterType(const std::string& reg) const {
    return verifier_.getRegisterType(reg);
}

void IRBuilder::reset() {
    nextRegister_ = 0;
}

} // namespace Backends
} // namespace XXML
