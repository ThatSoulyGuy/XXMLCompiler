#include "Backends/CallGenerator.h"
#include "Parser/AST.h"
#include <sstream>

namespace XXML {
namespace Backends {

LLVMValue CallGenerator::generateMethodCall(const std::string& object,
                                            const std::string& objectType,
                                            const std::string& methodName,
                                            const std::vector<LLVMValue>& args) {
    // Mangle the method name
    std::string mangledName = NameMangler::mangleMethod(objectType, methodName);

    // Get expected parameter types from resolver
    std::vector<std::string> expectedTypes = resolver_.getParameterTypes(objectType, methodName);

    // Convert arguments if necessary
    std::vector<LLVMValue> convertedArgs = convertArguments(args, expectedTypes);

    // Add 'this' pointer as first argument
    std::vector<LLVMValue> allArgs;
    allArgs.push_back(LLVMValue::makeRegister(object, LLVMType::getPointerType()));
    allArgs.insert(allArgs.end(), convertedArgs.begin(), convertedArgs.end());

    // Determine return type
    std::string returnTypeName = resolver_.getReturnType(objectType, methodName);
    LLVMType returnType = TypeConverter::xxmlToLLVM(returnTypeName);

    // Check if this is a void-returning method
    if (SpecialMethodRegistry::returnsVoid(mangledName)) {
        returnType = LLVMType::getVoidType();
    }

    // Generate the call
    std::string ir = builder_.emitCall(mangledName, returnType, allArgs);

    // Return the result value
    if (returnType.isVoid()) {
        return LLVMValue::makeNull();
    } else {
        // Extract register name from IR (format: "%rN = call ...")
        size_t eqPos = ir.find(" = ");
        if (eqPos != std::string::npos) {
            std::string reg = ir.substr(0, eqPos);
            return LLVMValue::makeRegister(reg, returnType);
        }
        return LLVMValue::makeNull();
    }
}

LLVMValue CallGenerator::generateFunctionCall(const std::string& functionName,
                                              const std::vector<LLVMValue>& args,
                                              const LLVMType& returnType) {
    // Generate the call
    std::string ir = builder_.emitCall(functionName, returnType, args);

    // Return the result value
    if (returnType.isVoid()) {
        return LLVMValue::makeNull();
    } else {
        // Extract register name from IR
        size_t eqPos = ir.find(" = ");
        if (eqPos != std::string::npos) {
            std::string reg = ir.substr(0, eqPos);
            return LLVMValue::makeRegister(reg, returnType);
        }
        return LLVMValue::makeNull();
    }
}

LLVMValue CallGenerator::generateConstructorCall(const std::string& className,
                                                 const std::vector<LLVMValue>& args) {
    // Allocate memory for the object
    std::string allocIR = builder_.emitAlloca("ptr");
    size_t eqPos = allocIR.find(" = ");
    std::string objPtr = allocIR.substr(0, eqPos);

    // Call the constructor
    std::string constructorName = NameMangler::mangleMethod(className, "init");

    // Add object pointer as first argument
    std::vector<LLVMValue> allArgs;
    allArgs.push_back(LLVMValue::makeRegister(objPtr, LLVMType::getPointerType()));
    allArgs.insert(allArgs.end(), args.begin(), args.end());

    // Constructor returns void
    builder_.emitCall(constructorName, LLVMType::getVoidType(), allArgs);

    return LLVMValue::makeRegister(objPtr, LLVMType::getPointerType());
}

LLVMValue CallGenerator::evaluateExpression(const Parser::Expression* expr) {
    // This would recursively evaluate an expression tree
    // For now, return a placeholder
    // In a complete implementation, this would:
    // - Handle literals (integers, strings, etc.)
    // - Handle variable references
    // - Handle binary operations
    // - Handle method calls
    // - etc.

    return LLVMValue::makeNull();
}

std::vector<LLVMValue> CallGenerator::convertArguments(
    const std::vector<LLVMValue>& args,
    const std::vector<std::string>& expectedTypes) {

    std::vector<LLVMValue> converted;

    for (size_t i = 0; i < args.size(); ++i) {
        if (i < expectedTypes.size()) {
            const std::string& targetTypeName = expectedTypes[i];

            if (needsConversion(args[i].getType(), targetTypeName)) {
                converted.push_back(convertType(args[i], targetTypeName));
            } else {
                converted.push_back(args[i]);
            }
        } else {
            converted.push_back(args[i]);
        }
    }

    return converted;
}

bool CallGenerator::needsConversion(const LLVMType& from,
                                    const std::string& toTypeName) const {
    std::string fromStr = from.toString();
    return fromStr != toTypeName;
}

LLVMValue CallGenerator::convertType(const LLVMValue& value,
                                     const std::string& targetType) {
    std::string sourceType = value.getType().toString();

    // ptr -> i64
    if (sourceType == "ptr" && targetType == "i64") {
        std::string ir = builder_.emitPtrToInt(value);
        size_t eqPos = ir.find(" = ");
        std::string reg = ir.substr(0, eqPos);
        return LLVMValue::makeRegister(reg, LLVMType::getI64Type());
    }

    // i64 -> ptr
    if (sourceType == "i64" && targetType == "ptr") {
        std::string ir = builder_.emitIntToPtr(value);
        size_t eqPos = ir.find(" = ");
        std::string reg = ir.substr(0, eqPos);
        return LLVMValue::makeRegister(reg, LLVMType::getPointerType());
    }

    // No conversion needed or not supported
    return value;
}

} // namespace Backends
} // namespace XXML
