# XXML Ownership System

XXML uses explicit ownership modifiers to express memory and value semantics. Every type reference must include one of three ownership modifiers.

---

## Overview

| Modifier | Symbol | Meaning |
|----------|--------|---------|
| Owned | `^` | Unique ownership - responsible for value lifetime |
| Reference | `&` | Borrowed reference - does not own the value |
| Copy | `%` | Value copy - creates an independent copy |

The compiler enforces move semantics for owned (`^`) captures and parameters. Use-after-move and double-move errors are detected at compile time.

---

## Owned (`^`)

Represents **unique ownership**. The variable owns the value and is responsible for its lifetime.

```xxml
Property <data> Types String^;  // Owned string property
Instantiate String^ As <name> = String::Constructor("Alice");
```

### Semantics

- Indicates the variable "owns" the value
- Passing to an owned parameter or capturing with `^` transfers ownership
- The original variable **cannot** be used after transfer (compile-time error)
- Double-move (moving the same variable twice) is a compile-time error

### Usage

- Properties typically use owned types
- Return types for constructors and factory methods
- Local variables that own their data

### Example

```xxml
Instantiate String^ As <name> = String::Constructor("Alice");

// Ownership transfer - name is now invalid
Run takeOwnership(name);

// ERROR: Cannot use 'name' after ownership transfer
// Run Console::printLine(name);  // Compile-time error
```

---

## Reference (`&`)

Represents a **borrowed reference**. The variable can access the value but does not own it.

```xxml
Parameter <str> Types String&;  // Borrowed reference parameter

Method <printMessage> Returns None Parameters (
    Parameter <msg> Types String&
) Do {
    Run Console::printLine(msg);
}
```

### Semantics

- The callee borrows the value without taking ownership
- The caller retains ownership and the value remains valid after the call
- At the LLVM level, passes the object pointer directly

### Usage

- Method parameters when you don't need ownership
- Efficient passing without copying
- Must not outlive the referenced value

### Example

```xxml
Instantiate String^ As <message> = String::Constructor("Hello");

// Borrow - message is still valid after call
Run printMessage(message);

// Still valid!
Run Console::printLine(message);
```

---

## Copy (`%`)

Represents a **copied value**. Creates an independent copy of the value.

```xxml
Parameter <value> Types Integer%;  // Copied integer parameter

Method <increment> Returns Integer^ Parameters (
    Parameter <n> Types Integer%
) Do {
    Return n.add(Integer::Constructor(1));
}
```

### Semantics

- Creates a copy of the value for the callee
- Modifications to the parameter don't affect the original
- The original value remains unchanged after the call

### Usage

- When you need an independent copy
- Parameters where modification shouldn't affect caller
- Small value types

### Example

```xxml
Instantiate Integer^ As <x> = Integer::Constructor(10);

// Copy passed - x is unchanged
Instantiate Integer^ As <y> = increment(x);

// x is still 10, y is 11
```

---

## Ownership Compatibility Rules

The compiler enforces type compatibility based on ownership:

| Parameter Type | Can Accept |
|---------------|------------|
| Owned (`^`) | Only owned (`^`) values |
| Reference (`&`) | Owned (`^`) or reference (`&`) values |
| Copy (`%`) | Any ownership (`^`, `&`, or `%`) |

### Compatibility Matrix

| Actual \ Expected | Owned (`^`) | Reference (`&`) | Copy (`%`) |
|-------------------|-------------|-----------------|------------|
| Owned (`^`) | OK | OK (borrow) | OK (copy) |
| Reference (`&`) | ERROR | OK | OK (copy) |
| Copy (`%`) | ERROR | ERROR | OK |

### Examples

```xxml
// Given these functions:
// takeOwned(Integer^)
// takeRef(Integer&)
// takeCopy(Integer%)

Instantiate Integer^ As <owned> = Integer::Constructor(42);

// Owned -> Owned: OK (ownership transfer)
Run takeOwned(owned);

// After this point, owned is invalid

Instantiate Integer^ As <another> = Integer::Constructor(100);

// Owned -> Reference: OK (temporary borrow)
Run takeRef(another);

// another is still valid

// Owned -> Copy: OK (creates copy)
Run takeCopy(another);

// another is still valid
```

---

## Ownership in Different Contexts

| Context | Common Ownership | Example |
|---------|-----------------|---------|
| Properties | Owned (`^`) | `Property <name> Types String^;` |
| Return types | Owned (`^`) | `Returns Integer^` |
| Parameters | Reference (`&`) or Copy (`%`) | `Parameter <x> Types Integer%` |
| Local variables | Owned (`^`) | `Instantiate String^ As <s> = ...` |
| Lambda captures | Any (`^`, `&`, `%`) | `[%copy, &ref, ^owned]` |
| Reference bindings | Reference (`&`) | `Instantiate Integer& As <ref> = existingVar;` |

---

## Reference Bindings

You can create a reference alias to an existing variable:

```xxml
Instantiate Integer^ As <original> = Integer::Constructor(42);
Instantiate Integer& As <ref> = original;

// ref and original refer to the same value
Run Console::printLine(ref.toString());  // Prints 42
```

---

## LLVM Code Generation

At the LLVM IR level, ownership affects code generation:

| Ownership | LLVM Representation | Notes |
|-----------|-------------------|-------|
| Owned (`^`) | `ptr` (heap pointer) | Object allocated via runtime |
| Reference (`&`) | `ptr` (same pointer) | Passed by pointer, no copy |
| Copy (`%`) | `ptr` (same pointer) | Currently same as reference at IR level |

All XXML objects are heap-allocated through the runtime library. The ownership modifier affects semantic validation but not the underlying pointer representation.

---

## Best Practices

1. **Use `&` for read-only parameters** - Avoids unnecessary copies
2. **Use `%` for small value types** - Clear semantics, no dangling references
3. **Use `^` for ownership transfer** - Factory methods, returning new objects
4. **Avoid holding references beyond their scope** - References must not outlive their source

---

## Move Semantics and Errors

The compiler tracks ownership to prevent common errors:

### Use After Move

```xxml
Instantiate String^ As <data> = String::Constructor("Hello");
Run consumeData(data);  // Takes ownership

// ERROR: data has been moved
Run Console::printLine(data);  // Compile-time error
```

### Double Move

```xxml
Instantiate String^ As <data> = String::Constructor("Hello");
Run consumeData(data);  // First move

// ERROR: data has already been moved
Run consumeData(data);  // Compile-time error
```

---

## See Also

- [Classes](CLASSES.md) - Class declarations with properties
- [Lambdas](LAMBDAS.md) - Lambda capture ownership
- [Structures](STRUCTURES.md) - Stack-allocated value types

