#include "Backends/Codegen/StmtCodegen/StmtCodegen.h"
#include "Backends/TypeNormalizer.h"
#include "Backends/LLVMIR/TypedInstructions.h"
#include "Semantic/CompiletimeInterpreter.h"
#include <iostream>

namespace XXML {
namespace Backends {
namespace Codegen {

void StmtCodegen::generate(Parser::Statement* stmt) {
    if (!stmt) return;

    // Dispatch based on statement type
    if (auto* assign = dynamic_cast<Parser::AssignmentStmt*>(stmt)) {
        visitAssignment(assign);
    } else if (auto* inst = dynamic_cast<Parser::InstantiateStmt*>(stmt)) {
        visitInstantiate(inst);
    } else if (auto* ifStmt = dynamic_cast<Parser::IfStmt*>(stmt)) {
        visitIf(ifStmt);
    } else if (auto* whileStmt = dynamic_cast<Parser::WhileStmt*>(stmt)) {
        visitWhile(whileStmt);
    } else if (auto* forStmt = dynamic_cast<Parser::ForStmt*>(stmt)) {
        visitFor(forStmt);
    } else if (auto* breakStmt = dynamic_cast<Parser::BreakStmt*>(stmt)) {
        visitBreak(breakStmt);
    } else if (auto* contStmt = dynamic_cast<Parser::ContinueStmt*>(stmt)) {
        visitContinue(contStmt);
    } else if (auto* retStmt = dynamic_cast<Parser::ReturnStmt*>(stmt)) {
        visitReturn(retStmt);
    } else if (auto* exitStmt = dynamic_cast<Parser::ExitStmt*>(stmt)) {
        visitExit(exitStmt);
    } else if (auto* runStmt = dynamic_cast<Parser::RunStmt*>(stmt)) {
        visitRun(runStmt);
    }
}

// === Run Statement (expression statement) ===

void StmtCodegen::visitRun(Parser::RunStmt* stmt) {
    if (!stmt || !stmt->expression) return;
    // Simply evaluate the expression for side effects
    exprCodegen_.generate(stmt->expression.get());
    // Clean up any temporary objects created during expression evaluation
    ctx_.emitTemporaryCleanup();
}

// === Return Statement ===

void StmtCodegen::visitReturn(Parser::ReturnStmt* stmt) {
    if (!stmt) return;

    if (stmt->value) {
        // Check for void return (None is treated as void at IR level)
        std::string returnType = std::string(ctx_.currentReturnType());
        if (returnType == "void" || returnType == "None" || returnType.empty()) {
            // Void/None return - evaluate expression for side effects, then return void
            exprCodegen_.generate(stmt->value.get());
            // Emit destructors for all variables in scope before returning
            ctx_.emitAllDestructors();
            ctx_.builder().createRetVoid();
        } else {
            // Non-void return - save value, emit destructors, then return
            auto returnValue = exprCodegen_.generate(stmt->value.get());

            // Check if the return expression returns a reference type (T&) but the
            // method's return type is owned. In that case, we need to load from the reference.
            bool valueIsReference = (stmt->value->resolvedOwnership == Parser::OwnershipType::Reference);
            bool returnTypeIsReference = (!returnType.empty() && returnType.back() == '&');
            if (valueIsReference && !returnTypeIsReference && returnValue.isPtr()) {
                returnValue = LLVMIR::AnyValue(ctx_.builder().createLoad(
                    ctx_.builder().getPtrTy(),
                    returnValue.asPtr(),
                    "ref.load.return"
                ));
            }

            // Check if we're returning a simple variable - if so, skip its destructor
            // because ownership is being transferred to the caller
            std::string excludeVar;
            if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(stmt->value.get())) {
                excludeVar = identExpr->name;
            }

            // Emit destructors for all variables in scope before returning
            // Skip the returned variable to prevent destroying what we're returning
            if (!excludeVar.empty()) {
                ctx_.emitAllDestructorsExcept(excludeVar);
            } else {
                ctx_.emitAllDestructors();
            }
            ctx_.builder().createRet(returnValue);
        }
    } else {
        // No return value provided - emit destructors, then return void
        ctx_.emitAllDestructors();
        ctx_.builder().createRetVoid();
    }
}

// === Exit Statement ===

void StmtCodegen::visitExit(Parser::ExitStmt* stmt) {
    if (!stmt) return;

    // Exit is like return but typically from Entrypoint
    if (stmt->exitCode) {
        auto exitValue = exprCodegen_.generate(stmt->exitCode.get());
        // Emit destructors for all variables in scope before exiting
        ctx_.emitAllDestructors();
        // main() returns i32, so truncate if necessary
        if (exitValue.isInt() && exitValue.asInt().getBitWidth() > 32) {
            auto truncated = ctx_.builder().createTrunc(exitValue.asInt(), ctx_.module().getContext().getInt32Ty(), "");
            ctx_.builder().createRet(LLVMIR::AnyValue(truncated));
        } else {
            ctx_.builder().createRet(exitValue);
        }
    } else {
        // Emit destructors for all variables in scope before exiting
        ctx_.emitAllDestructors();
        // Return 0 for success
        auto zero = ctx_.builder().getInt32(0);
        ctx_.builder().createRet(LLVMIR::AnyValue(zero));
    }
}

// === Assignment Statement ===

void StmtCodegen::visitAssignment(Parser::AssignmentStmt* stmt) {
    if (!stmt || !stmt->target || !stmt->value) return;

    // Generate code for the value expression
    auto valueResult = exprCodegen_.generate(stmt->value.get());

    // Check if the value expression returns a reference type (T&) but we're
    // assigning to an owned variable. In that case, we need to load from the reference.
    bool valueIsReference = (stmt->value->resolvedOwnership == Parser::OwnershipType::Reference);
    if (valueIsReference && valueResult.isPtr()) {
        valueResult = LLVMIR::AnyValue(ctx_.builder().createLoad(
            ctx_.builder().getPtrTy(),
            valueResult.asPtr(),
            "ref.load.assign"
        ));
    }

    // Handle different types of lvalue expressions
    if (auto* identExpr = dynamic_cast<Parser::IdentifierExpr*>(stmt->target.get())) {
        // Simple variable assignment: Set x = value;
        const std::string& varName = identExpr->name;

        if (auto* varInfo = ctx_.getVariable(varName)) {
            if (varInfo->alloca) {
                if (varInfo->isReference) {
                    // For reference types, we need to store THROUGH the reference
                    // First load the pointer that the reference points to
                    auto refTarget = ctx_.builder().createLoad(
                        ctx_.builder().getPtrTy(),
                        LLVMIR::PtrValue(varInfo->alloca),
                        "ref.target"
                    );
                    // Then store the new value through that pointer
                    ctx_.builder().createStore(valueResult, refTarget.asPtr());
                } else {
                    // For owned/copy types, store directly to the alloca
                    ctx_.builder().createStore(valueResult, LLVMIR::PtrValue(varInfo->alloca));
                }
            }
        } else if (!ctx_.currentClassName().empty()) {
            // Check if it's a class property (implicit this.propertyName)
            storeToThisProperty(varName, valueResult);
        }
    } else if (auto* memberExpr = dynamic_cast<Parser::MemberAccessExpr*>(stmt->target.get())) {
        // Member access assignment: Set obj.property = value;
        if (dynamic_cast<Parser::ThisExpr*>(memberExpr->object.get())) {
            storeToThisProperty(memberExpr->member, valueResult);
        } else if (auto* objIdent = dynamic_cast<Parser::IdentifierExpr*>(memberExpr->object.get())) {
            storeToObjectProperty(objIdent->name, memberExpr->member, valueResult);
        }
    } else if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(stmt->target.get())) {
        // Method call assignment: Set obj.getRef() = value;
        // This works when the method returns a reference type (&)
        // The call returns a pointer to the storage location
        auto refTarget = exprCodegen_.generate(callExpr);
        if (refTarget.isPtr()) {
            // Store the value through the reference
            ctx_.builder().createStore(valueResult, refTarget.asPtr());
        }
    }

    // Clear temporaries without cleanup - the assigned value is now owned by the variable
    ctx_.clearTemporaries();
}

// === Instantiate Statement ===

void StmtCodegen::visitInstantiate(Parser::InstantiateStmt* stmt) {
    if (!stmt || !stmt->type) return;

    const std::string& varName = stmt->variableName;
    std::string typeName = stmt->type->typeName;

    // Include template arguments if present
    if (!stmt->type->templateArgs.empty()) {
        typeName += "<";
        for (size_t i = 0; i < stmt->type->templateArgs.size(); ++i) {
            if (i > 0) typeName += ", ";
            typeName += stmt->type->templateArgs[i].typeArg;
        }
        typeName += ">";
    }

    // Check if this is a pointer/object type (has ownership marker ^ or & or %)
    bool isObjectType = (stmt->type->ownership == Parser::OwnershipType::Owned ||
                         stmt->type->ownership == Parser::OwnershipType::Reference ||
                         stmt->type->ownership == Parser::OwnershipType::Copy);

    // Track if this is a reference type (for assignment semantics)
    bool isReference = (stmt->type->ownership == Parser::OwnershipType::Reference);

    // Strip ownership markers
    typeName = TypeNormalizer::stripOwnershipMarker(typeName);

    // Resolve to fully qualified name before mapping
    typeName = ctx_.resolveToQualifiedName(typeName);

    // NativeType should NEVER be treated as object type - it stores the value directly
    bool isNativeType = typeName.find("NativeType<") != std::string::npos;
    if (isNativeType) {
        isObjectType = false;
    }

    // Check if this is a value type (Structure)
    // Value types are stack-allocated directly, not as pointers
    auto* classInfo = ctx_.getClass(typeName);
    bool isValueType = classInfo && classInfo->isValueType;

    // Create alloca for the variable
    // For value types: allocate the struct type directly (stack-allocated)
    // For object types (Integer^, Bool^, etc.): use ptr type since they're heap-allocated
    // For NativeType: use the actual value type
    LLVMIR::Type* allocaType;
    if (isValueType) {
        // Value types: allocate the actual struct type on the stack
        allocaType = classInfo->structType;
    } else if (isObjectType) {
        // Reference types: allocate a pointer (object lives on heap)
        allocaType = ctx_.builder().getPtrTy();
    } else {
        // Primitive types or NativeType: allocate the actual type
        allocaType = ctx_.mapType(typeName);
    }
    // Use createEntryBlockAlloca to place alloca in the entry block
    // This prevents stack overflow from allocas inside loops
    auto allocaPtr = ctx_.builder().createEntryBlockAlloca(allocaType, varName);

    // Get raw AllocaInst* from PtrValue for registration
    auto* allocaInst = static_cast<LLVMIR::AllocaInst*>(allocaPtr.raw());

    // Register the variable using declareVariable
    ctx_.declareVariable(varName, typeName, LLVMIR::AnyValue(allocaPtr), allocaInst, isReference);

    // Register for RAII destruction if this is an object type
    // This ensures automatic cleanup when the variable goes out of scope
    if (isObjectType && !isNativeType) {
        ctx_.registerForDestruction(varName, typeName, allocaInst);
    }

    // If there's an initializer, generate it and store
    if (stmt->initializer) {
        auto initValue = exprCodegen_.generate(stmt->initializer.get());

        // Check if the initializer expression returns a reference type (T&)
        // If so, we need to load from the reference to get the actual value
        bool initializerIsReference = (stmt->initializer->resolvedOwnership == Parser::OwnershipType::Reference);
        if (initializerIsReference && initValue.isPtr()) {
            // The initializer returned T& (address of where T^ is stored)
            // Load from that address to get the actual T^ value
            initValue = LLVMIR::AnyValue(ctx_.builder().createLoad(
                ctx_.builder().getPtrTy(),
                initValue.asPtr(),
                "ref.load"
            ));
        }

        // For value types (Structures), the constructor returns a pointer to a temp alloca.
        // We need to copy the struct from the temp to our variable's alloca.
        if (isValueType && initValue.isPtr()) {
            // Use memcpy to copy the struct from temp to our alloca
            auto* memcpyFunc = ctx_.module().getFunction("xxml_memcpy");
            if (!memcpyFunc) {
                std::vector<LLVMIR::Type*> memcpyParams = {
                    ctx_.builder().getPtrTy(),
                    ctx_.builder().getPtrTy(),
                    ctx_.module().getContext().getInt64Ty()
                };
                auto* memcpyType = ctx_.module().getContext().getFunctionTy(
                    ctx_.builder().getPtrTy(), memcpyParams, false);
                memcpyFunc = ctx_.module().createFunction(memcpyType, "xxml_memcpy",
                    LLVMIR::Function::Linkage::External);
            }
            auto sizeVal = ctx_.builder().getInt64(classInfo->instanceSize);
            std::vector<LLVMIR::AnyValue> memcpyArgs = {
                LLVMIR::AnyValue(allocaPtr),
                initValue,
                LLVMIR::AnyValue(sizeVal)
            };
            ctx_.builder().createCall(memcpyFunc, memcpyArgs, "");
        } else {
            // For reference types, store the pointer
            ctx_.builder().createStore(initValue, allocaPtr);
        }

        // Register compiletime variables in the interpreter for constant folding
        if (stmt->isCompiletime && ctx_.hasCompiletimeInterpreter()) {
            auto* interp = ctx_.compiletimeInterpreter();
            // Evaluate the initializer at compile-time
            auto ctValue = interp->evaluate(stmt->initializer.get());
            if (ctValue) {
                interp->setVariable(varName, std::move(ctValue));
            }
        }

        // Clear temporaries without cleanup - the initialized value is now owned by the variable
        ctx_.clearTemporaries();
    }
}

// === Control Flow: If Statement ===

void StmtCodegen::visitIf(Parser::IfStmt* stmt) {
    if (!stmt || !stmt->condition) return;

    auto* func = ctx_.currentFunction();
    if (!func) return;

    // Check if we have an else branch
    bool hasElse = !stmt->elseBranch.empty();

    // Create basic blocks
    auto* thenBB = func->createBasicBlock("if.then");
    auto* elseBB = hasElse ? func->createBasicBlock("if.else") : nullptr;
    auto* mergeBB = func->createBasicBlock("if.end");

    // Evaluate condition
    auto condValue = exprCodegen_.generate(stmt->condition.get());

    // Convert to i1 if needed (condition might be a Bool object)
    auto condI1 = ensureBoolCondition(condValue);

    // Clean up temporaries from condition evaluation (e.g., Bool from comparisons)
    // Safe to do here because condI1 is a primitive i1, not the Bool object
    ctx_.emitTemporaryCleanup();

    // Create conditional branch
    if (elseBB) {
        ctx_.builder().createCondBr(condI1, thenBB, elseBB);
    } else {
        ctx_.builder().createCondBr(condI1, thenBB, mergeBB);
    }

    // Emit then block
    ctx_.setInsertPoint(thenBB);
    ctx_.pushScope();  // Push scope for then branch
    for (const auto& thenStmt : stmt->thenBranch) {
        generate(thenStmt.get());
    }
    ctx_.popScope();  // Pop scope - emits destructors/dispose calls
    // Only add branch to merge if block doesn't have a terminator
    if (ctx_.currentBlock() && !ctx_.currentBlock()->getTerminator()) {
        ctx_.builder().createBr(mergeBB);
    }

    // Emit else block if present
    if (elseBB) {
        ctx_.setInsertPoint(elseBB);
        ctx_.pushScope();  // Push scope for else branch
        for (const auto& elseStmt : stmt->elseBranch) {
            generate(elseStmt.get());
        }
        ctx_.popScope();  // Pop scope - emits destructors/dispose calls
        if (ctx_.currentBlock() && !ctx_.currentBlock()->getTerminator()) {
            ctx_.builder().createBr(mergeBB);
        }
    }

    // Continue at merge block
    ctx_.setInsertPoint(mergeBB);
}

// === Control Flow: While Statement ===

void StmtCodegen::visitWhile(Parser::WhileStmt* stmt) {
    if (!stmt || !stmt->condition) return;

    auto* func = ctx_.currentFunction();
    if (!func) return;

    // Create basic blocks
    auto* condBB = func->createBasicBlock("while.cond");
    auto* bodyBB = func->createBasicBlock("while.body");
    auto* endBB = func->createBasicBlock("while.end");

    // Push loop context for break/continue
    ctx_.pushLoop(condBB, endBB);

    // Branch to condition
    ctx_.builder().createBr(condBB);

    // Emit condition block
    ctx_.setInsertPoint(condBB);
    auto condValue = exprCodegen_.generate(stmt->condition.get());
    auto condI1 = ensureBoolCondition(condValue);
    // Clean up temporaries from condition evaluation (e.g., Bool from hasNext())
    // Safe to do here because condI1 is a primitive i1, not the Bool object
    ctx_.emitTemporaryCleanup();
    ctx_.builder().createCondBr(condI1, bodyBB, endBB);

    // Emit body block
    ctx_.setInsertPoint(bodyBB);

    // Push scope for loop body - ensures cleanup of variables each iteration
    ctx_.pushScope();

    for (const auto& bodyStmt : stmt->body) {
        generate(bodyStmt.get());
    }

    // Pop scope before branching back - this emits destructors/dispose calls
    ctx_.popScope();

    if (ctx_.currentBlock() && !ctx_.currentBlock()->getTerminator()) {
        ctx_.builder().createBr(condBB);
    }

    // Pop loop context
    ctx_.popLoop();

    // Continue at end block
    ctx_.setInsertPoint(endBB);
}

// === Control Flow: For Statement ===

void StmtCodegen::visitFor(Parser::ForStmt* stmt) {
    if (!stmt) return;

    auto* func = ctx_.currentFunction();
    if (!func) return;

    // Create basic blocks
    auto* condBB = func->createBasicBlock("for.cond");
    auto* bodyBB = func->createBasicBlock("for.body");
    auto* incBB = func->createBasicBlock("for.inc");
    auto* endBB = func->createBasicBlock("for.end");

    // Push loop context (continue goes to inc, break goes to end)
    ctx_.pushLoop(incBB, endBB);

    // Handle initialization based on loop type
    if (stmt->isCStyleLoop) {
        // C-style loop: rangeStart is the initializer expression
        if (stmt->rangeStart) {
            exprCodegen_.generate(stmt->rangeStart.get());
        }
    } else {
        // Range-based loop: create iterator variable
        // For x = start .. end becomes: alloca x; store start, x; while (x < end) { body; x++ }
        if (stmt->iteratorType && stmt->rangeStart) {
            std::string iterName = stmt->iteratorName;
            std::string typeName = stmt->iteratorType->typeName;
            typeName = TypeNormalizer::stripOwnershipMarker(typeName);

            // Check if this is an object type (Integer^, String^, etc.)
            // Object types store pointers, not raw values
            // Also treat bare Integer/String/etc. as object types (default to owned)
            bool hasOwnership = (stmt->iteratorType->ownership == Parser::OwnershipType::Owned ||
                                 stmt->iteratorType->ownership == Parser::OwnershipType::Reference ||
                                 stmt->iteratorType->ownership == Parser::OwnershipType::Copy);
            bool isKnownObjectType = (typeName == "Integer" || typeName == "Language::Core::Integer" ||
                                      typeName == "String" || typeName == "Language::Core::String" ||
                                      typeName == "Bool" || typeName == "Language::Core::Bool" ||
                                      typeName == "Float" || typeName == "Language::Core::Float" ||
                                      typeName == "Double" || typeName == "Language::Core::Double");
            bool isObjectType = hasOwnership || isKnownObjectType;

            // Use ptr type for object types, mapped type for primitives
            auto* allocaType = isObjectType ? ctx_.builder().getPtrTy() : ctx_.mapType(typeName);
            // Use createEntryBlockAlloca to prevent stack overflow from allocas inside loops
            auto allocaPtr = ctx_.builder().createEntryBlockAlloca(allocaType, iterName);
            auto* allocaInst = static_cast<LLVMIR::AllocaInst*>(allocaPtr.raw());
            ctx_.declareVariable(iterName, typeName, LLVMIR::AnyValue(allocaPtr), allocaInst);

            auto startVal = exprCodegen_.generate(stmt->rangeStart.get());

            // If this is an Integer object type and the start value is a raw integer,
            // we need to wrap it with Integer::Constructor()
            if (isObjectType && (typeName == "Integer" || typeName == "Language::Core::Integer") &&
                startVal.isInt()) {
                // Call Integer::Constructor to wrap the raw i64
                auto* intCtorFunc = ctx_.module().getFunction("Integer_Constructor");
                if (!intCtorFunc) {
                    std::vector<LLVMIR::Type*> ctorParams = { ctx_.module().getContext().getInt64Ty() };
                    auto* ctorType = ctx_.module().getContext().getFunctionTy(
                        ctx_.builder().getPtrTy(), ctorParams, false);
                    intCtorFunc = ctx_.module().createFunction(ctorType, "Integer_Constructor",
                        LLVMIR::Function::Linkage::External);
                }
                std::vector<LLVMIR::AnyValue> ctorArgs = { startVal };
                startVal = ctx_.builder().createCall(intCtorFunc, ctorArgs, "");
            }

            ctx_.builder().createStore(startVal, allocaPtr);
        }
    }

    // Branch to condition
    ctx_.builder().createBr(condBB);

    // Emit condition block
    ctx_.setInsertPoint(condBB);
    if (stmt->isCStyleLoop && stmt->condition) {
        auto condValue = exprCodegen_.generate(stmt->condition.get());
        auto condI1 = ensureBoolCondition(condValue);
        // Clean up temporaries from condition evaluation (e.g., Bool from comparisons)
        ctx_.emitTemporaryCleanup();
        ctx_.builder().createCondBr(condI1, bodyBB, endBB);
    } else if (!stmt->isCStyleLoop && stmt->rangeEnd) {
        // Range-based: iterator < rangeEnd
        // Load iterator, compare with rangeEnd
        auto* varInfo = ctx_.getVariable(stmt->iteratorName);
        if (varInfo && varInfo->alloca) {
            // Check if this is an Integer^ type (object wrapper)
            bool isIntegerObject = varInfo->xxmlType == "Integer" ||
                                   varInfo->xxmlType == "Language::Core::Integer";

            // Use ptr type for object types, mapped type for primitives
            auto* loadType = isIntegerObject ? ctx_.builder().getPtrTy() : ctx_.mapType(varInfo->xxmlType);
            auto iterVal = ctx_.builder().createLoad(
                loadType,
                LLVMIR::PtrValue(varInfo->alloca),
                ""
            );
            auto endVal = exprCodegen_.generate(stmt->rangeEnd.get());

            if (isIntegerObject && iterVal.isPtr() && endVal.isPtr()) {
                // Extract raw int64 values directly for comparison (more efficient, no Bool allocation)
                auto* getValFunc = ctx_.module().getFunction("Integer_getValue");
                if (!getValFunc) {
                    std::vector<LLVMIR::Type*> getValParams = { ctx_.builder().getPtrTy() };
                    auto* getValType = ctx_.module().getContext().getFunctionTy(
                        ctx_.module().getContext().getInt64Ty(), getValParams, false);
                    getValFunc = ctx_.module().createFunction(getValType, "Integer_getValue",
                        LLVMIR::Function::Linkage::External);
                }
                // Get raw values from both Integer objects
                std::vector<LLVMIR::AnyValue> iterArgs = { iterVal };
                auto iterRaw = ctx_.builder().createCall(getValFunc, iterArgs, "iter.raw");
                std::vector<LLVMIR::AnyValue> endArgs = { endVal };
                auto endRaw = ctx_.builder().createCall(getValFunc, endArgs, "end.raw");

                // Direct i64 comparison
                auto cond = ctx_.builder().createICmpSLT(iterRaw.asInt(), endRaw.asInt(), "for.cmp");
                ctx_.builder().createCondBr(cond, bodyBB, endBB);
            } else if (iterVal.isInt() && endVal.isInt()) {
                // Raw integer comparison
                auto cond = ctx_.builder().createICmpSLT(iterVal.asInt(), endVal.asInt(), "for.cmp");
                ctx_.builder().createCondBr(cond, bodyBB, endBB);
            } else {
                ctx_.builder().createBr(bodyBB); // fallback
            }
        } else {
            ctx_.builder().createBr(bodyBB);
        }
    } else {
        ctx_.builder().createBr(bodyBB); // Infinite loop if no condition
    }

    // Emit body block
    ctx_.setInsertPoint(bodyBB);

    // Push scope for loop body - ensures cleanup of variables each iteration
    ctx_.pushScope();

    for (const auto& bodyStmt : stmt->body) {
        generate(bodyStmt.get());
    }

    // Pop scope before branching to increment - this emits destructors/dispose calls
    ctx_.popScope();

    if (ctx_.currentBlock() && !ctx_.currentBlock()->getTerminator()) {
        ctx_.builder().createBr(incBB);
    }

    // Emit increment block
    ctx_.setInsertPoint(incBB);
    if (stmt->isCStyleLoop && stmt->increment) {
        exprCodegen_.generate(stmt->increment.get());
    } else if (!stmt->isCStyleLoop) {
        // Range-based: iterator++
        auto* varInfo = ctx_.getVariable(stmt->iteratorName);
        if (varInfo && varInfo->alloca) {
            // Check if this is an Integer^ type (object wrapper)
            bool isIntegerObject = varInfo->xxmlType == "Integer" ||
                                   varInfo->xxmlType == "Language::Core::Integer";

            // Use ptr type for object types, mapped type for primitives
            auto* loadType = isIntegerObject ? ctx_.builder().getPtrTy() : ctx_.mapType(varInfo->xxmlType);
            auto iterVal = ctx_.builder().createLoad(
                loadType,
                LLVMIR::PtrValue(varInfo->alloca),
                ""
            );

            if (isIntegerObject && iterVal.isPtr()) {
                // Extract raw value, increment, create new Integer (more efficient, no Integer_add call)
                auto* getValFunc = ctx_.module().getFunction("Integer_getValue");
                if (!getValFunc) {
                    std::vector<LLVMIR::Type*> getValParams = { ctx_.builder().getPtrTy() };
                    auto* getValType = ctx_.module().getContext().getFunctionTy(
                        ctx_.module().getContext().getInt64Ty(), getValParams, false);
                    getValFunc = ctx_.module().createFunction(getValType, "Integer_getValue",
                        LLVMIR::Function::Linkage::External);
                }
                std::vector<LLVMIR::AnyValue> getArgs = { iterVal };
                auto rawVal = ctx_.builder().createCall(getValFunc, getArgs, "iter.raw");

                // Add 1 to raw value
                auto one = ctx_.builder().getInt64(1);
                auto incRaw = ctx_.builder().createAdd(rawVal.asInt(), one, "inc.raw");

                // Create new Integer with incremented value
                auto* intCtorFunc = ctx_.module().getFunction("Integer_Constructor");
                if (!intCtorFunc) {
                    std::vector<LLVMIR::Type*> ctorParams = { ctx_.module().getContext().getInt64Ty() };
                    auto* ctorType = ctx_.module().getContext().getFunctionTy(
                        ctx_.builder().getPtrTy(), ctorParams, false);
                    intCtorFunc = ctx_.module().createFunction(ctorType, "Integer_Constructor",
                        LLVMIR::Function::Linkage::External);
                }
                std::vector<LLVMIR::AnyValue> ctorArgs = { LLVMIR::AnyValue(incRaw) };
                auto incVal = ctx_.builder().createCall(intCtorFunc, ctorArgs, "inc.result");
                ctx_.builder().createStore(incVal, LLVMIR::PtrValue(varInfo->alloca));
            } else if (iterVal.isInt()) {
                // Raw integer increment
                auto one = ctx_.builder().getIntN(iterVal.asInt().getBitWidth(), 1);
                auto incVal = ctx_.builder().createAdd(iterVal.asInt(), one, "");
                ctx_.builder().createStore(LLVMIR::AnyValue(incVal), LLVMIR::PtrValue(varInfo->alloca));
            }
        }
    }
    ctx_.builder().createBr(condBB);

    // Pop loop context
    ctx_.popLoop();

    // Continue at end block
    ctx_.setInsertPoint(endBB);
}

// === Control Flow: Break Statement ===

void StmtCodegen::visitBreak(Parser::BreakStmt*) {
    auto* loopCtx = ctx_.currentLoop();
    if (loopCtx && loopCtx->endBlock) {
        ctx_.builder().createBr(loopCtx->endBlock);
    }
}

// === Control Flow: Continue Statement ===

void StmtCodegen::visitContinue(Parser::ContinueStmt*) {
    auto* loopCtx = ctx_.currentLoop();
    if (loopCtx && loopCtx->condBlock) {
        ctx_.builder().createBr(loopCtx->condBlock);
    }
}

// === Private Helper Methods ===

void StmtCodegen::storeToThisProperty(const std::string& propName, LLVMIR::AnyValue value) {
    auto* func = ctx_.currentFunction();
    if (!func || func->getNumParams() == 0) return;

    auto* thisArg = func->getArg(0);
    if (!thisArg) return;

    std::string className = std::string(ctx_.currentClassName());
    auto* classInfo = ctx_.getClass(className);
    if (!classInfo || !classInfo->structType) return;

    for (const auto& prop : classInfo->properties) {
        if (prop.name == propName) {
            auto thisPtrValue = LLVMIR::PtrValue(thisArg);
            auto propPtr = ctx_.builder().createStructGEP(
                classInfo->structType,
                thisPtrValue,
                static_cast<unsigned>(prop.index),
                propName + ".ptr"
            );
            ctx_.builder().createStore(value, propPtr);
            return;
        }
    }
}

void StmtCodegen::storeToObjectProperty(const std::string& objName, const std::string& propName, LLVMIR::AnyValue value) {
    auto* varInfo = ctx_.getVariable(objName);
    if (!varInfo || !varInfo->alloca) return;

    // Get the object's type and class info
    std::string typeName = TypeNormalizer::stripOwnershipMarker(varInfo->xxmlType);
    auto* classInfo = ctx_.getClass(typeName);
    if (!classInfo || !classInfo->structType) return;

    // Load the object pointer
    auto objPtr = ctx_.builder().createLoad(
        ctx_.module().getContext().getPtrTy(),
        LLVMIR::PtrValue(varInfo->alloca),
        ""
    );

    for (const auto& prop : classInfo->properties) {
        if (prop.name == propName) {
            auto propPtr = ctx_.builder().createStructGEP(
                classInfo->structType,
                objPtr.asPtr(),
                static_cast<unsigned>(prop.index),
                propName + ".ptr"
            );
            ctx_.builder().createStore(value, propPtr);
            return;
        }
    }
}

LLVMIR::BoolValue StmtCodegen::ensureBoolCondition(LLVMIR::AnyValue value) {
    // If it's an integer, check if already i1 or compare to 0
    if (value.isInt()) {
        auto intVal = value.asInt();
        if (intVal.getBitWidth() == 1) {
            return intVal;  // BoolValue is IntValue alias
        }
        // Non-boolean integer - compare to 0
        auto zero = ctx_.builder().getIntN(intVal.getBitWidth(), 0);
        return ctx_.builder().createICmpNE(intVal, zero, "tobool");
    }

    // If it's a pointer, assume it's a Bool^ object and call Bool_getValue
    // In XXML, conditions like "If (someBool)" expect the Bool VALUE to be checked,
    // not just whether the pointer is non-null. For null checks, users write
    // explicit comparisons like "If (ptr != null)".
    if (value.isPtr()) {
        // Get or declare Bool_getValue function
        auto* getBoolFunc = ctx_.module().getFunction("Bool_getValue");
        if (!getBoolFunc) {
            auto* funcType = ctx_.module().getContext().getFunctionTy(
                ctx_.builder().getInt1Ty(),
                {ctx_.builder().getPtrTy()},
                false
            );
            getBoolFunc = ctx_.module().createFunction(funcType, "Bool_getValue",
                                                       LLVMIR::Function::Linkage::External);
        }
        // Call Bool_getValue to extract the i1 value
        auto boolResult = ctx_.builder().createCall(getBoolFunc, {value.asPtr()}, "boolval");
        return boolResult.asInt();
    }

    // Default: treat as true
    return ctx_.builder().getInt1(true);
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
