// IRExample.cpp - Demonstrates the new IR infrastructure
//
// This file shows how to use the type-safe IR builder to generate LLVM IR.
// Compare this to the string-based approach in LLVMBackend.cpp

#include "Backends/IR/IR.h"
#include <iostream>

namespace XXML {
namespace Backends {
namespace IR {

// Example: Generate a simple "Hello, World!" program
// Equivalent LLVM IR:
//   @.str = private constant [14 x i8] c"Hello, World!\00"
//   declare void @Console_printLine(ptr)
//   define void @main() {
//     call void @Console_printLine(ptr @.str)
//     ret void
//   }
std::string generateHelloWorld() {
    // Create module
    Module module("hello_world");

    // Get type context
    TypeContext& ctx = module.getContext();

    // Create Console_printLine declaration
    FunctionType* printLineTy = ctx.getFunctionTy(ctx.getVoidTy(), {ctx.getPtrTy()});
    Function* printLine = module.declareFunction("Console_printLine", printLineTy);

    // Create main function
    FunctionType* mainTy = ctx.getFunctionTy(ctx.getVoidTy(), {});
    Function* mainFunc = module.createFunction(mainTy, "main", GlobalValue::Linkage::External);

    // Get string literal
    GlobalVariable* helloStr = module.getOrCreateStringLiteral("Hello, World!");

    // Create entry block and builder
    BasicBlock* entry = mainFunc->createBasicBlock("entry");
    IRBuilder builder(entry);

    // Call Console_printLine(@.str)
    builder.CreateCall(printLine, {helloStr});

    // Return void
    builder.CreateRetVoid();

    // Verify and emit
    std::string error;
    if (!verifyModule(module, &error)) {
        return "; Verification failed:\n" + error;
    }

    return emitModule(module);
}

// Example: Generate a function that adds two integers
// Equivalent LLVM IR:
//   define i64 @add_integers(i64 %a, i64 %b) {
//     %result = add i64 %a, %b
//     ret i64 %result
//   }
std::string generateAddFunction() {
    Module module("add_example");
    TypeContext& ctx = module.getContext();

    // Create function type: i64 (i64, i64)
    FunctionType* addTy = ctx.getFunctionTy(ctx.getInt64Ty(), {ctx.getInt64Ty(), ctx.getInt64Ty()});

    // Create function
    Function* addFunc = module.createFunction(addTy, "add_integers", GlobalValue::Linkage::External);

    // Name the arguments
    Argument* argA = addFunc->getArg(0);
    Argument* argB = addFunc->getArg(1);
    argA->setName("a");
    argB->setName("b");

    // Create entry block
    BasicBlock* entry = addFunc->createBasicBlock("entry");
    IRBuilder builder(entry);

    // Add the two arguments
    Value* result = builder.CreateAdd(argA, argB, "result");

    // Return the result
    builder.CreateRet(result);

    // Verify and emit
    return emitModule(module);
}

// Example: Generate a conditional (if-then-else)
// Equivalent LLVM IR:
//   define i64 @max(i64 %a, i64 %b) {
//   entry:
//     %cmp = icmp sgt i64 %a, %b
//     br i1 %cmp, label %then, label %else
//   then:
//     br label %merge
//   else:
//     br label %merge
//   merge:
//     %result = phi i64 [ %a, %then ], [ %b, %else ]
//     ret i64 %result
//   }
std::string generateMaxFunction() {
    Module module("max_example");
    TypeContext& ctx = module.getContext();

    // Create function type: i64 (i64, i64)
    FunctionType* maxTy = ctx.getFunctionTy(ctx.getInt64Ty(), {ctx.getInt64Ty(), ctx.getInt64Ty()});

    // Create function
    Function* maxFunc = module.createFunction(maxTy, "max", GlobalValue::Linkage::External);

    // Name the arguments
    Argument* argA = maxFunc->getArg(0);
    Argument* argB = maxFunc->getArg(1);
    argA->setName("a");
    argB->setName("b");

    // Create basic blocks
    BasicBlock* entry = maxFunc->createBasicBlock("entry");
    BasicBlock* thenBB = maxFunc->createBasicBlock("then");
    BasicBlock* elseBB = maxFunc->createBasicBlock("else");
    BasicBlock* mergeBB = maxFunc->createBasicBlock("merge");

    // Entry block: compare and branch
    IRBuilder builder(entry);
    Value* cmp = builder.CreateICmpSGT(argA, argB, "cmp");
    builder.CreateCondBr(cmp, thenBB, elseBB);

    // Then block: branch to merge
    builder.setInsertPoint(thenBB);
    builder.CreateBr(mergeBB);

    // Else block: branch to merge
    builder.setInsertPoint(elseBB);
    builder.CreateBr(mergeBB);

    // Merge block: PHI node and return
    builder.setInsertPoint(mergeBB);
    PHINode* phi = builder.CreatePHI(ctx.getInt64Ty(), 2, "result");
    phi->addIncoming(argA, thenBB);
    phi->addIncoming(argB, elseBB);
    builder.CreateRet(phi);

    // Verify and emit
    return emitModule(module);
}

// Example: Generate a class constructor
// Shows how to handle struct types and GEP
std::string generateClassConstructor() {
    Module module("class_example");
    TypeContext& ctx = module.getContext();

    // Create struct type for a Point class: { i64, i64 }
    StructType* pointTy = module.createStructType("class.Point");
    std::vector<Type*> pointFields = {ctx.getInt64Ty(), ctx.getInt64Ty()};
    pointTy->setBody(pointFields);

    // Create constructor: ptr @Point_Constructor(ptr %this, i64 %x, i64 %y)
    FunctionType* ctorTy = ctx.getFunctionTy(ctx.getPtrTy(), {ctx.getPtrTy(), ctx.getInt64Ty(), ctx.getInt64Ty()});
    Function* ctor = module.createFunction(ctorTy, "Point_Constructor", GlobalValue::Linkage::External);

    Argument* thisArg = ctor->getArg(0);
    Argument* xArg = ctor->getArg(1);
    Argument* yArg = ctor->getArg(2);
    thisArg->setName("this");
    xArg->setName("x");
    yArg->setName("y");

    BasicBlock* entry = ctor->createBasicBlock("entry");
    IRBuilder builder(entry);

    // Store x into this->x (field 0)
    Value* xPtr = builder.CreateStructGEP(pointTy, thisArg, 0, "x.ptr");
    builder.CreateStore(xArg, xPtr);

    // Store y into this->y (field 1)
    Value* yPtr = builder.CreateStructGEP(pointTy, thisArg, 1, "y.ptr");
    builder.CreateStore(yArg, yPtr);

    // Return this
    builder.CreateRet(thisArg);

    return emitModule(module);
}

// Example: Show verification catching an error
std::string demonstrateVerification() {
    Module module("verification_example");
    TypeContext& ctx = module.getContext();

    // Create a function with mismatched types (intentional error)
    FunctionType* funcTy = ctx.getFunctionTy(ctx.getInt64Ty(), {});
    Function* func = module.createFunction(funcTy, "bad_function", GlobalValue::Linkage::External);

    BasicBlock* entry = func->createBasicBlock("entry");
    IRBuilder builder(entry);

    // Try to return void from a function that should return i64
    // This will be caught by the verifier
    builder.CreateRetVoid();

    // Try to verify - this should fail
    std::string error;
    if (!verifyModule(module, &error)) {
        return "; Verification correctly caught the error:\n" + error;
    }

    return emitModule(module);
}

// Run all examples
void runExamples() {
    std::cout << "=== Hello World Example ===\n";
    std::cout << generateHelloWorld() << "\n\n";

    std::cout << "=== Add Function Example ===\n";
    std::cout << generateAddFunction() << "\n\n";

    std::cout << "=== Max Function Example (with PHI) ===\n";
    std::cout << generateMaxFunction() << "\n\n";

    std::cout << "=== Class Constructor Example ===\n";
    std::cout << generateClassConstructor() << "\n\n";

    std::cout << "=== Verification Demo ===\n";
    std::cout << demonstrateVerification() << "\n";
}

} // namespace IR
} // namespace Backends
} // namespace XXML
