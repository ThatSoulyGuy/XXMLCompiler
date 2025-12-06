#pragma once

#include "Backends/LLVMIR/TypedModule.h"
#include "Backends/LLVMIR/TypedValue.h"
#include <string>
#include <string_view>
#include <vector>
#include <span>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// GlobalBuilder - Type-Safe Module-Level Declaration Builder
// ============================================================================
//
// This builder creates module-level declarations (globals, functions, strings)
// with compile-time type safety. All raw string emission is replaced with
// typed operations that go through the Module and TypeContext.
//
// Key design principles:
// 1. No raw string emission - all IR generated through typed objects
// 2. All types validated through TypeContext before use
// 3. Returns typed values (PtrValue) for use in other builders

class GlobalBuilder {
public:
    explicit GlobalBuilder(Module& module);

    // ========================================================================
    // String Constants
    // ========================================================================

    /// Create a string constant and return a pointer to it.
    /// The string is null-terminated automatically.
    GlobalVariable* createStringConstant(std::string_view content,
                                         std::string_view name = "");

    /// Create a string constant with explicit null termination control.
    GlobalVariable* createStringConstant(std::string_view content,
                                         bool nullTerminate,
                                         std::string_view name = "");

    // ========================================================================
    // Global Variables
    // ========================================================================

    /// Create a global variable with an integer initializer.
    GlobalVariable* createGlobalInt(IntegerType* type, int64_t value,
                                    std::string_view name,
                                    GlobalVariable::Linkage linkage = GlobalVariable::Linkage::Internal);

    /// Create a global variable with a float initializer.
    GlobalVariable* createGlobalFloat(FloatType* type, double value,
                                      std::string_view name,
                                      GlobalVariable::Linkage linkage = GlobalVariable::Linkage::Internal);

    /// Create a global variable with a null pointer initializer.
    GlobalVariable* createGlobalNullPtr(std::string_view name,
                                        GlobalVariable::Linkage linkage = GlobalVariable::Linkage::Internal);

    /// Create an uninitialized global variable.
    GlobalVariable* createGlobal(Type* valueType, std::string_view name,
                                 GlobalVariable::Linkage linkage = GlobalVariable::Linkage::External);

    /// Create a constant global (immutable).
    GlobalVariable* createConstantGlobal(Type* valueType, Constant* initializer,
                                         std::string_view name,
                                         GlobalVariable::Linkage linkage = GlobalVariable::Linkage::Private);

    // ========================================================================
    // Constant Arrays
    // ========================================================================

    /// Create a constant array of integers.
    GlobalVariable* createConstantIntArray(IntegerType* elementType,
                                           std::span<const int64_t> values,
                                           std::string_view name,
                                           GlobalVariable::Linkage linkage = GlobalVariable::Linkage::Private);

    /// Create a constant array of pointers (e.g., vtable, string table).
    GlobalVariable* createConstantPtrArray(std::span<GlobalVariable*> values,
                                           std::string_view name,
                                           GlobalVariable::Linkage linkage = GlobalVariable::Linkage::Private);

    // ========================================================================
    // Function Declarations
    // ========================================================================

    /// Declare an external function (no body).
    Function* declareFunction(FunctionType* funcType, std::string_view name,
                              Function::Linkage linkage = Function::Linkage::External);

    /// Declare an external function with parameter types specified individually.
    Function* declareFunction(Type* returnType,
                              std::span<Type*> paramTypes,
                              std::string_view name,
                              bool isVarArg = false,
                              Function::Linkage linkage = Function::Linkage::External);

    /// Declare a variadic function (like printf).
    Function* declareVarArgFunction(Type* returnType,
                                    std::span<Type*> paramTypes,
                                    std::string_view name,
                                    Function::Linkage linkage = Function::Linkage::External);

    // ========================================================================
    // Function Definitions
    // ========================================================================

    /// Create a function definition (with body).
    Function* defineFunction(FunctionType* funcType, std::string_view name,
                             Function::Linkage linkage = Function::Linkage::External);

    /// Create an internal function (private to this module).
    Function* defineInternalFunction(FunctionType* funcType, std::string_view name);

    // ========================================================================
    // Struct Type Definitions
    // ========================================================================

    /// Create or get a named struct type.
    StructType* getOrCreateStruct(std::string_view name);

    /// Create a struct type with the given field types.
    StructType* createStruct(std::string_view name, std::span<Type*> fieldTypes,
                             bool isPacked = false);

    // ========================================================================
    // Module & Context Accessors
    // ========================================================================

    Module& getModule() { return module_; }
    const Module& getModule() const { return module_; }

    TypeContext& getContext() { return module_.getContext(); }
    const TypeContext& getContext() const { return module_.getContext(); }

    // Type shortcuts
    VoidType* getVoidTy() { return module_.getContext().getVoidTy(); }
    IntegerType* getInt1Ty() { return module_.getContext().getInt1Ty(); }
    IntegerType* getInt8Ty() { return module_.getContext().getInt8Ty(); }
    IntegerType* getInt16Ty() { return module_.getContext().getInt16Ty(); }
    IntegerType* getInt32Ty() { return module_.getContext().getInt32Ty(); }
    IntegerType* getInt64Ty() { return module_.getContext().getInt64Ty(); }
    FloatType* getFloatTy() { return module_.getContext().getFloatTy(); }
    FloatType* getDoubleTy() { return module_.getContext().getDoubleTy(); }
    PointerType* getPtrTy() { return module_.getContext().getPtrTy(); }

private:
    Module& module_;
    size_t stringCounter_ = 0;
    size_t arrayCounter_ = 0;

    std::string generateStringName();
    std::string generateArrayName();
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
