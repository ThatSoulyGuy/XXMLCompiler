# XXML Known Limitations

This document lists known limitations, incomplete features, and planned improvements for the XXML compiler and language.

## Table of Contents

1. [Language Features](#language-features)
2. [Code Generation](#code-generation)
3. [Reflection System](#reflection-system)
4. [Standard Library](#standard-library)
5. [Tooling](#tooling)
6. [Workarounds](#workarounds)

---

## Language Features

### No Exception Handling

XXML does not currently have a try/catch/throw exception system. Error handling must be done through return values.

**Workaround**: Use return types that can represent success or failure, such as returning `None` for errors or using a result wrapper class.

```xxml
// Pattern: Return None^ to indicate failure
Method <findUser> Returns User^ Parameters (Parameter <id> Types Integer^) Do
{
    // Return None::Constructor() on failure
    // Return User::Constructor(...) on success
}
```

### No Interface/Trait System

There is no `interface` or `trait` keyword. Only class inheritance with `Extends` is supported.

**Workaround**: Use [template constraints](CONSTRAINTS.md) to enforce method requirements on types.

```xxml
// Define constraint requiring specific methods
[ Constraint <Printable> <T Constrains None> (T a)
    Require (F(String^)(toString)(*) On a)
]

// Use constraint in template class
[ Class <Logger> <T Constrains Printable> Final Extends None
    // T is guaranteed to have toString()
]
```

### No Operator Overloading

Operators like `+`, `-`, `*`, `/` cannot be overloaded for custom types. The standard library types use method calls.

**Current behavior**: Use methods instead of operators.

```xxml
// Instead of: a + b
Instantiate Integer^ As <sum> = a.add(b);

// Instead of: a == b
Instantiate Bool^ As <equal> = a.equals(b);

// Instead of: a < b
Instantiate Bool^ As <less> = a.lessThan(b);
```

### No Pattern Matching / Switch Statement

There is no `switch`, `match`, or pattern matching construct. Use if/else chains.

```xxml
// Use if/else chain instead of switch
If (value.equals(Integer::Constructor(1))) -> {
    // case 1
} Else -> {
    If (value.equals(Integer::Constructor(2))) -> {
        // case 2
    } Else -> {
        // default
    }
}
```

### No Async/Await or Coroutines

Asynchronous programming must be done with explicit threading primitives. See [THREADING.md](THREADING.md).

### No Multi-line Comments

Only single-line comments (`//`) are supported.

```xxml
// This is a comment
// This is another comment line
```

### Limited Float/Double Literal Syntax

Float literals require the `f` suffix, doubles require `d` suffix or use constructor.

```xxml
Instantiate Float^ As <f> = 3.14f;
Instantiate Double^ As <d> = 3.14159d;
Instantiate Double^ As <d2> = Double::Constructor(3.14159);
```

---

## Code Generation

### Destructor Support for Heap Objects

Destructor support for heap-allocated objects that are not directly owned by stack variables is incomplete.

**Source**: `src/Backends/LLVMBackend.cpp:1370`

**Impact**: Objects stored in collections or transferred via complex ownership paths may not have their destructors called automatically.

**Workaround**: Manually call cleanup methods before releasing references.

### Lambda Code Generation (Legacy Backend)

Lambda code generation in the legacy `CodeGenerator.cpp` is marked as TODO.

**Source**: `src/CodeGen/CodeGenerator.cpp:1922`

**Note**: The LLVM backend (`LLVMBackend.cpp`) fully supports lambdas. This only affects the legacy C++ backend which is not the default.

---

## Reflection System

### Dynamic Method Invocation Not Implemented

The reflection system can introspect types, methods, and properties, but cannot invoke methods dynamically at runtime.

```xxml
// This works - getting method information
Instantiate MethodInfo^ As <method> = type.getMethod(String::Constructor("greet"));
Instantiate String^ As <sig> = method.getSignature();

// NOT implemented - dynamic invocation
// method.invoke(instance, args);  // Does not exist
```

### Property Get/Set via Reflection Not Implemented

Cannot read or write property values using reflection. Only metadata (name, type, offset) is available.

### O(n) Type Registry Lookup

Type lookups by name use linear search through the registry.

**Impact**: Programs with many types may experience slower reflection lookups.

**Future**: Hash map implementation planned for O(1) lookup.

### Static Method Detection Not Implemented

The `isStatic` flag in `MethodInfo` always returns `false`. XXML does not have a `static` keyword - methods are conventionally "static" when called via `Class::methodName` syntax but this is a calling convention, not a declaration modifier.

**Status**: By design. Infrastructure is in place for future language extension.

**Workaround**: Methods that don't access instance state can be treated as static by the programmer.

### Constructor Metadata - RESOLVED

~~Constructor count and constructor metadata arrays are always empty in reflection data.~~

**Status**: Fixed. Use `Type.getConstructorCount()` and `Type.getConstructorAt(index)` to enumerate constructors.

### Base Class Name - RESOLVED

~~The `baseClassName` field in reflection type info is always null.~~

**Status**: Fixed. Use `Type.hasBaseType()` and `Type.getBaseType()` to query inheritance.

---

## Standard Library

### Collections Are Not Thread-Safe

`List`, `HashMap`, `Set`, etc. are not thread-safe. Use explicit locking with mutexes when accessing from multiple threads.

```xxml
// Protect collection access with mutex
Run Syscall::Mutex_lock(mutex);
Run list.add(item);
Run Syscall::Mutex_unlock(mutex);
```

### No Iterator Protocol

Collections don't have a standard iterator interface. Use index-based access.

```xxml
For (Integer^ <i> = 0 .. list.size().toInt64()) ->
{
    Instantiate T^ As <item> = list.get(i);
    // use item
}
```

### HTTP Client Limited

The `Language::Network::HTTP` module has basic functionality. Advanced features like HTTPS certificate validation, connection pooling, and request streaming may be limited.

### JSON Parser Limitations

The `Language::Format::JSON` module parses basic JSON. Deeply nested structures or very large files may have issues.

---

## Tooling

### No Integrated Debugger Support

No debug information (DWARF, PDB) is generated. Use print statements for debugging.

### No Language Server Protocol (LSP)

No IDE integration via LSP. Syntax highlighting may be available via generic bracket-matching.

### No REPL

No interactive Read-Eval-Print-Loop. All code must be compiled and executed.

### Limited Error Recovery

The parser stops at the first syntax error rather than attempting to continue and report multiple errors.

---

## Workarounds

### Error Handling Pattern

```xxml
// Define a Result wrapper
[ Class <Result> <T Constrains None> Final Extends None
    [ Public <>
        Property <value> Types T^;
        Property <hasError> Types Bool^;
        Property <errorMessage> Types String^;

        // Success factory
        Method <ok> Returns Result<T>^ Parameters (Parameter <v> Types T^) Do
        {
            Instantiate Result<T>^ As <r> = Result@T::Constructor();
            Set r.value = v;
            Set r.hasError = Bool::Constructor(false);
            Return r;
        }

        // Error factory
        Method <error> Returns Result<T>^ Parameters (Parameter <msg> Types String^) Do
        {
            Instantiate Result<T>^ As <r> = Result@T::Constructor();
            Set r.hasError = Bool::Constructor(true);
            Set r.errorMessage = msg;
            Return r;
        }
    ]
]
```

---

## Planned Improvements

The following improvements are planned for future versions:

1. **Short Term**
   - Dynamic method invocation via reflection
   - Property get/set via reflection
   - Hash map type registry for O(1) lookup
   - Complete reflection metadata (constructors, base class)

2. **Medium Term**
   - Exception handling system
   - Debug information generation
   - Multi-error reporting in parser

3. **Long Term**
   - Interface/trait system
   - Pattern matching
   - Async/await
   - Language Server Protocol support

---

## Reporting Issues

If you encounter limitations not listed here or have suggestions for improvements, please report them at:

https://github.com/anthropics/claude-code/issues

Include:
- XXML version
- Minimal code example demonstrating the limitation
- Expected behavior
- Actual behavior

---

## See Also

- [Language Specification](LANGUAGE_SPEC.md) - Complete language syntax
- [Templates](TEMPLATES.md) - Generic programming
- [Constraints](CONSTRAINTS.md) - Template constraints (workaround for interfaces)
- [Threading](THREADING.md) - Concurrency primitives
- [Reflection System](REFLECTION_SYSTEM.md) - Runtime type introspection
