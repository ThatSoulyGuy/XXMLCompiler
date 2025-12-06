#include "Backends/LLVMIR/GlobalBuilder.h"
#include <stdexcept>

namespace XXML {
namespace Backends {
namespace LLVMIR {

GlobalBuilder::GlobalBuilder(Module& module)
    : module_(module) {
}

// ============================================================================
// String Constants
// ============================================================================

GlobalVariable* GlobalBuilder::createStringConstant(std::string_view content,
                                                    std::string_view name) {
    return createStringConstant(content, true, name);
}

GlobalVariable* GlobalBuilder::createStringConstant(std::string_view content,
                                                    bool nullTerminate,
                                                    std::string_view name) {
    // If name is empty, generate one
    std::string actualName = name.empty() ? generateStringName() : std::string(name);

    if (nullTerminate) {
        // Use Module's built-in string literal support which handles null termination
        return module_.getOrCreateStringLiteral(content);
    } else {
        // Create array type without null terminator
        ArrayType* arrayType = module_.getContext().getArrayTy(
            module_.getContext().getInt8Ty(),
            content.size()
        );

        // Create global variable
        GlobalVariable* gv = module_.createGlobal(
            arrayType,
            actualName,
            GlobalVariable::Linkage::Private
        );
        gv->setConstant(true);
        return gv;
    }
}

// ============================================================================
// Global Variables
// ============================================================================

GlobalVariable* GlobalBuilder::createGlobalInt(IntegerType* type, int64_t value,
                                               std::string_view name,
                                               GlobalVariable::Linkage linkage) {
    ConstantInt* initializer = module_.getConstantInt(type, value);
    GlobalVariable* gv = module_.createGlobal(type, name, linkage, initializer);
    return gv;
}

GlobalVariable* GlobalBuilder::createGlobalFloat(FloatType* type, double value,
                                                 std::string_view name,
                                                 GlobalVariable::Linkage linkage) {
    ConstantFP* initializer = module_.getConstantFP(type, value);
    GlobalVariable* gv = module_.createGlobal(type, name, linkage, initializer);
    return gv;
}

GlobalVariable* GlobalBuilder::createGlobalNullPtr(std::string_view name,
                                                   GlobalVariable::Linkage linkage) {
    PointerType* ptrTy = module_.getContext().getPtrTy();
    ConstantNull* initializer = module_.getConstantNull(ptrTy);
    GlobalVariable* gv = module_.createGlobal(ptrTy, name, linkage, initializer);
    return gv;
}

GlobalVariable* GlobalBuilder::createGlobal(Type* valueType, std::string_view name,
                                            GlobalVariable::Linkage linkage) {
    return module_.createGlobal(valueType, name, linkage, nullptr);
}

GlobalVariable* GlobalBuilder::createConstantGlobal(Type* valueType, Constant* initializer,
                                                    std::string_view name,
                                                    GlobalVariable::Linkage linkage) {
    GlobalVariable* gv = module_.createGlobal(valueType, name, linkage, initializer);
    gv->setConstant(true);
    return gv;
}

// ============================================================================
// Constant Arrays
// ============================================================================

GlobalVariable* GlobalBuilder::createConstantIntArray(IntegerType* elementType,
                                                      std::span<const int64_t> values,
                                                      std::string_view name,
                                                      GlobalVariable::Linkage linkage) {
    // Create array type
    ArrayType* arrayType = module_.getContext().getArrayTy(elementType, values.size());

    // Generate name if empty
    std::string actualName = name.empty() ? generateArrayName() : std::string(name);

    // Create the global variable
    // Note: The initializer will be a constant array, but for now we create without
    // explicit initializer - the emitter will handle the values
    GlobalVariable* gv = module_.createGlobal(arrayType, actualName, linkage, nullptr);
    gv->setConstant(true);

    return gv;
}

GlobalVariable* GlobalBuilder::createConstantPtrArray(std::span<GlobalVariable*> values,
                                                      std::string_view name,
                                                      GlobalVariable::Linkage linkage) {
    // Create array type of pointers
    PointerType* ptrTy = module_.getContext().getPtrTy();
    ArrayType* arrayType = module_.getContext().getArrayTy(ptrTy, values.size());

    // Generate name if empty
    std::string actualName = name.empty() ? generateArrayName() : std::string(name);

    // Create the global variable
    GlobalVariable* gv = module_.createGlobal(arrayType, actualName, linkage, nullptr);
    gv->setConstant(true);

    return gv;
}

// ============================================================================
// Function Declarations
// ============================================================================

Function* GlobalBuilder::declareFunction(FunctionType* funcType, std::string_view name,
                                         Function::Linkage linkage) {
    // Check if already exists
    if (Function* existing = module_.getFunction(name)) {
        return existing;
    }

    // Create function declaration (no basic blocks = declaration)
    return module_.createFunction(funcType, name, linkage);
}

Function* GlobalBuilder::declareFunction(Type* returnType,
                                         std::span<Type*> paramTypes,
                                         std::string_view name,
                                         bool isVarArg,
                                         Function::Linkage linkage) {
    // Create function type
    std::vector<Type*> params(paramTypes.begin(), paramTypes.end());
    FunctionType* funcType = module_.getContext().getFunctionTy(returnType, std::move(params), isVarArg);

    return declareFunction(funcType, name, linkage);
}

Function* GlobalBuilder::declareVarArgFunction(Type* returnType,
                                               std::span<Type*> paramTypes,
                                               std::string_view name,
                                               Function::Linkage linkage) {
    return declareFunction(returnType, paramTypes, name, true, linkage);
}

// ============================================================================
// Function Definitions
// ============================================================================

Function* GlobalBuilder::defineFunction(FunctionType* funcType, std::string_view name,
                                        Function::Linkage linkage) {
    Function* func = declareFunction(funcType, name, linkage);
    // The caller is responsible for adding basic blocks to make it a definition
    return func;
}

Function* GlobalBuilder::defineInternalFunction(FunctionType* funcType, std::string_view name) {
    return defineFunction(funcType, name, Function::Linkage::Internal);
}

// ============================================================================
// Struct Type Definitions
// ============================================================================

StructType* GlobalBuilder::getOrCreateStruct(std::string_view name) {
    // Check if exists in module
    if (StructType* existing = module_.getStruct(name)) {
        return existing;
    }

    // Create new struct
    return module_.createStruct(name);
}

StructType* GlobalBuilder::createStruct(std::string_view name, std::span<Type*> fieldTypes,
                                        bool isPacked) {
    StructType* structTy = getOrCreateStruct(name);

    // Set body if not already set
    if (structTy->isOpaque()) {
        std::vector<Type*> fields(fieldTypes.begin(), fieldTypes.end());
        structTy->setBody(std::move(fields), isPacked);
    }

    return structTy;
}

// ============================================================================
// Private Helpers
// ============================================================================

std::string GlobalBuilder::generateStringName() {
    return ".str." + std::to_string(stringCounter_++);
}

std::string GlobalBuilder::generateArrayName() {
    return ".arr." + std::to_string(arrayCounter_++);
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
