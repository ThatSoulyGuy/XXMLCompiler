#include "Backends/SSAVerifier.h"

namespace XXML {
namespace Backends {

void SSAVerifier::registerDef(const std::string& registerName) {
    definedRegisters_.insert(registerName);
}

void SSAVerifier::defineRegister(const std::string& registerName, const LLVMType& type) {
    definedRegisters_.insert(registerName);
    registerTypes_[registerName] = type;
}

LLVMType SSAVerifier::getRegisterType(const std::string& registerName) const {
    auto it = registerTypes_.find(registerName);
    if (it != registerTypes_.end()) {
        return it->second;
    }
    return LLVMType::getPointerType(); // Default fallback
}

bool SSAVerifier::isDefined(const std::string& registerName) const {
    return definedRegisters_.find(registerName) != definedRegisters_.end();
}

bool SSAVerifier::verifyUse(const std::string& registerName) const {
    return isDefined(registerName);
}

void SSAVerifier::clear() {
    definedRegisters_.clear();
    registerTypes_.clear();
}

} // namespace Backends
} // namespace XXML
