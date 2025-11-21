# IRBuilder Quick Reference - Common Patterns

## Table of Contents
1. [Variable Declaration](#variable-declaration)
2. [Loading Values](#loading-values)
3. [Arithmetic Operations](#arithmetic-operations)
4. [Comparisons](#comparisons)
5. [Function Calls](#function-calls)
6. [Control Flow](#control-flow)
7. [Type Conversions](#type-conversions)
8. [Memory Operations](#memory-operations)

---

## Variable Declaration

### Pattern: Instantiate a variable
```cpp
// Get LLVM type
LLVMType llvmType = typeConverter_->convertType(typeName);

// Allocate stack space
LLVMValue varPtr = irBuilder_->emitAlloca(llvmType, variableName);

// Track in ValueTracker
valueTracker_->registerValue(variableName, varPtr);

// Track ownership for cleanup
if (ownership == OwnershipKind::Owned) {
    destructorManager_->registerValue(variableName, varPtr);
}
```

---

## Loading Values

### Pattern: Load a value from a variable
```cpp
// Get variable from ValueTracker
LLVMValue varPtr = valueTracker_->getValue(variableName);

// Auto-load if needed
if (varPtr.needsLoad()) {
    LLVMValue loaded = irBuilder_->emitLoad(varPtr);
    valueTracker_->setLastExpression(loaded);
}
```

### Pattern: Store a value
```cpp
LLVMValue value = valueTracker_->getLastExpression();
LLVMValue varPtr = valueTracker_->getValue(variableName);
irBuilder_->emitStore(value, varPtr);
```

---

## Arithmetic Operations

### Pattern: Binary operation (add, sub, mul, sdiv)
```cpp
LLVMValue left = valueTracker_->getLastExpression();
// ... evaluate right operand ...
LLVMValue right = valueTracker_->getLastExpression();

LLVMValue result = irBuilder_->emitBinOp("add", left, right);
valueTracker_->setLastExpression(result);
```

### Pattern: Pointer arithmetic
```cpp
// Convert pointer to i64
LLVMValue ptrAsInt = irBuilder_->emitPtrToInt(ptrValue, LLVMType::getI64Type());

// Perform arithmetic
LLVMValue result = irBuilder_->emitBinOp("add", ptrAsInt, offset);

// Convert back to pointer
LLVMValue resultPtr = irBuilder_->emitIntToPtr(result, LLVMType::getPointerType());
```

---

## Comparisons

### Pattern: Integer comparison
```cpp
LLVMValue result = irBuilder_->emitICmp("eq", left, right);
// Operations: eq, ne, slt, sle, sgt, sge
valueTracker_->setLastExpression(result);
```

### Pattern: Float comparison
```cpp
LLVMValue result = irBuilder_->emitFCmp("oeq", left, right);
// Operations: oeq, one, olt, ole, ogt, oge
valueTracker_->setLastExpression(result);
```

---

## Function Calls

### Pattern: Non-void function call
```cpp
LLVMValue arg1 = LLVMValue::makeConstant("42", LLVMType::getI64Type());
LLVMValue arg2 = valueTracker_->getValue("x");

LLVMValue result = irBuilder_->emitCall(
    LLVMType::getPointerType(),  // return type
    "Integer_Constructor",        // function name (no @ prefix)
    {arg1, arg2}                 // arguments
);

valueTracker_->setLastExpression(result);
```

### Pattern: Void function call
```cpp
irBuilder_->emitCall(
    LLVMType::getVoidType(),
    "Console_printLine",
    {message}
);
```

---

## Control Flow

### Pattern: If statement
```cpp
// Emit condition
LLVMValue condition = valueTracker_->getLastExpression();

// Allocate labels
std::string thenLabel = irBuilder_->allocateLabel("if_then");
std::string elseLabel = irBuilder_->allocateLabel("if_else");
std::string endLabel = irBuilder_->allocateLabel("if_end");

// Conditional branch
irBuilder_->emitCondBr(condition, thenLabel, elseLabel);

// Then block
irBuilder_->emitLabel(thenLabel);
// ... then body ...
irBuilder_->emitBr(endLabel);

// Else block
irBuilder_->emitLabel(elseLabel);
// ... else body ...
irBuilder_->emitBr(endLabel);

// End
irBuilder_->emitLabel(endLabel);
```

### Pattern: While loop
```cpp
std::string condLabel = irBuilder_->allocateLabel("while_cond");
std::string bodyLabel = irBuilder_->allocateLabel("while_body");
std::string endLabel = irBuilder_->allocateLabel("while_end");

// Jump to condition check
irBuilder_->emitBr(condLabel);

// Condition block
irBuilder_->emitLabel(condLabel);
// ... evaluate condition ...
LLVMValue condition = valueTracker_->getLastExpression();
irBuilder_->emitCondBr(condition, bodyLabel, endLabel);

// Body block
irBuilder_->emitLabel(bodyLabel);
// ... loop body ...
irBuilder_->emitBr(condLabel);

// End
irBuilder_->emitLabel(endLabel);
```

---

## Type Conversions

### Pattern: Pointer to integer
```cpp
LLVMValue i64Value = irBuilder_->emitPtrToInt(ptrValue, LLVMType::getI64Type());
```

### Pattern: Integer to pointer
```cpp
LLVMValue ptrValue = irBuilder_->emitIntToPtr(i64Value, LLVMType::getPointerType());
```

### Pattern: Bitcast
```cpp
LLVMValue casted = irBuilder_->emitBitcast(value, targetType);
```

---

## Memory Operations

### Pattern: Get element pointer (struct field access)
```cpp
// Access field at index 2 of a struct
LLVMValue fieldPtr = irBuilder_->emitGEP(
    structPtr,
    {0, 2},              // indices: struct offset, field index
    fieldType            // type of the field
);
```

### Pattern: Constructor with malloc
```cpp
// Allocate memory
size_t classSize = calculateClassSize(className);
LLVMValue sizeValue = LLVMValue::makeConstant(
    std::to_string(classSize),
    LLVMType::getI64Type()
);

LLVMValue mallocResult = irBuilder_->emitCall(
    LLVMType::getPointerType(),
    "xxml_malloc",
    {sizeValue}
);
```

---

## Factory Methods Cheat Sheet

### LLVMValue Factories
```cpp
// Constant
LLVMValue::makeConstant("42", LLVMType::getI64Type())
LLVMValue::makeConstant("@.str0", LLVMType::getPointerType())

// Variable (pointer to stack memory)
LLVMValue::makeVariable("%0", type, LLVMValue::AllocationKind::Stack)

// Parameter
LLVMValue::makeParameter("%this", LLVMType::getPointerType())

// Register (SSA register)
LLVMValue::makeRegister("r0", type)

// Global
LLVMValue::makeGlobal(".str0", LLVMType::getPointerType())
```

### LLVMType Factories
```cpp
LLVMType::getI1Type()         // i1 (bool)
LLVMType::getI8Type()         // i8 (byte)
LLVMType::getI32Type()        // i32
LLVMType::getI64Type()        // i64
LLVMType::getFloatType()      // float
LLVMType::getDoubleType()     // double
LLVMType::getPointerType()    // ptr
LLVMType::getVoidType()       // void
```

---

## Common Idioms

### Dual-tracking for backward compatibility
```cpp
// NEW: Type-safe tracking
LLVMValue result = irBuilder_->emitBinOp("add", left, right);
valueTracker_->setLastExpression(result);

// LEGACY: Maintain old tracking
valueMap_["__last_expr"] = result.getRegister();
registerTypes_[result.getRegister()] = "Integer";
```

### Name mangling
```cpp
// Use NameMangler for consistent names
std::string mangledName = nameMangler_->mangleClassName("List<Integer>");
// Result: "List_Integer"

std::string methodName = nameMangler_->mangleMethod("Integer", "add");
// Result: "Integer_add"
```

### Comments for debugging
```cpp
irBuilder_->emitComment("Instantiate owned variable: " + varName);
irBuilder_->emitComment("ERROR: Type mismatch in operation");
```

### Raw IR when needed
```cpp
// For complex instructions not yet abstracted
irBuilder_->emitRaw("define i32 @main() #1 {");
irBuilder_->indent();
// ... body ...
irBuilder_->dedent();
irBuilder_->emitRaw("}");
```

---

## Error Handling Patterns

### Type safety check
```cpp
if (left.getType() != right.getType()) {
    irBuilder_->emitComment(format(
        "Type mismatch: {} vs {}",
        left.getType().toString(),
        right.getType().toString()
    ));
    return;
}
```

### Null check
```cpp
LLVMValue value = valueTracker_->getLastExpression();
if (value.getRegister() == "null" || value.getRegister().empty()) {
    irBuilder_->emitComment("Null value detected");
    return;
}
```

---

## Performance Tips

1. **Minimize loads**: Check `needsLoad()` before loading
2. **Reuse values**: Store frequently used values in local variables
3. **Use IRBuilder methods**: They handle type tracking automatically
4. **Batch operations**: Build complex expressions before emitting

---

**Quick Reference Version**: 1.0
**Last Updated**: January 2025
**Compatibility**: LLVM IR 17.0
