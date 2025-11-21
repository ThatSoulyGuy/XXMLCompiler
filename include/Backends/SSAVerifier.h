#pragma once

#include "Backends/LLVMType.h"
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace XXML {
namespace Backends {

/**
 * Verifies SSA (Static Single Assignment) form
 */
class SSAVerifier {
public:
    SSAVerifier() = default;

    // Register a definition of a register
    void registerDef(const std::string& registerName);

    // Define a register with its type
    void defineRegister(const std::string& registerName, const LLVMType& type);

    // Get the type of a register
    LLVMType getRegisterType(const std::string& registerName) const;

    // Check if a register has been defined
    bool isDefined(const std::string& registerName) const;

    // Verify that a register is defined before use
    bool verifyUse(const std::string& registerName) const;

    // Clear all definitions (e.g., at function boundary)
    void clear();

private:
    std::unordered_set<std::string> definedRegisters_;
    std::unordered_map<std::string, LLVMType> registerTypes_;
};

} // namespace Backends
} // namespace XXML
