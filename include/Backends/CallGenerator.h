#pragma once

#include "Backends/LLVMValue.h"
#include "Backends/IRBuilder.h"
#include "Backends/MethodResolver.h"
#include "Backends/NameMangler.h"
#include "Backends/SpecialMethodRegistry.h"
#include "Backends/TypeConverter.h"
#include <string>
#include <vector>
#include <memory>

namespace XXML {

namespace Parser {
class Expression;
}

namespace Backends {

/**
 * Generates function and method calls in LLVM IR
 */
class CallGenerator {
public:
    CallGenerator(IRBuilder& builder, MethodResolver& resolver,
                  SpecialMethodRegistry& registry)
        : builder_(builder), resolver_(resolver), registry_(registry) {}

    // Generate a method call
    LLVMValue generateMethodCall(const std::string& object,
                                 const std::string& objectType,
                                 const std::string& methodName,
                                 const std::vector<LLVMValue>& args);

    // Generate a function call
    LLVMValue generateFunctionCall(const std::string& functionName,
                                   const std::vector<LLVMValue>& args,
                                   const LLVMType& returnType);

    // Generate a constructor call
    LLVMValue generateConstructorCall(const std::string& className,
                                      const std::vector<LLVMValue>& args);

    // Generate evaluation of expressions into LLVMValues
    LLVMValue evaluateExpression(const Parser::Expression* expr);

    // Convert argument types if necessary
    std::vector<LLVMValue> convertArguments(const std::vector<LLVMValue>& args,
                                           const std::vector<std::string>& expectedTypes);

private:
    IRBuilder& builder_;
    MethodResolver& resolver_;
    SpecialMethodRegistry& registry_;

    // Helper to determine if conversion is needed
    bool needsConversion(const LLVMType& from, const std::string& toTypeName) const;

    // Helper to perform type conversion
    LLVMValue convertType(const LLVMValue& value, const std::string& targetType);
};

} // namespace Backends
} // namespace XXML
