# XXML Compiler

https://www.xxml-language.com

A production-ready compiler for the XXML programming language that compiles to LLVM IR with native executable generation.

## Philosophy

> **"The more explicit the syntax, the greater one's conceptual understanding is. The more implicit the syntax, the lesser one's conceptual understanding is."**

XXML is designed around **radical explicitness**. Every ownership transfer, every memory allocation, every type relationship is visible in the syntax. This is not verbosity for its own sake—it is clarity that builds understanding.

When you read XXML code, you see exactly what happens:
- `String^` tells you this value is **owned** and will be freed when it goes out of scope
- `String&` tells you this is a **borrowed reference** that must not outlive its owner
- `String%` tells you this is a **copy** with independent lifetime
- `MyClass::Constructor(...)` tells you a constructor is being called, not hidden behind syntactic sugar

This explicitness serves a deeper purpose: **code that teaches**. A developer reading XXML code doesn't just learn what the program does—they learn *how* memory, ownership, and types work. The syntax is the documentation.

## Why XXML?

### The Gap We Fill

Modern programming languages force uncomfortable tradeoffs:

| Language | Memory Safety | Reflection | Compile-Time Meta | Explicitness |
|----------|--------------|------------|-------------------|--------------|
| **C++** | Manual | Limited RTTI | Templates (complex) | Low |
| **Rust** | Ownership | None | Macros (DSL) | Medium |
| **Java/C#** | GC | Rich | Limited | Low |
| **Zig** | Manual | None | comptime | High |
| **XXML** | **Ownership** | **Rich + Safe** | **Reflection-based** | **Very High** |

XXML is the first language to unify:
1. **Explicit ownership semantics** for memory safety without garbage collection
2. **Rich runtime reflection** that respects ownership rules
3. **Compile-time metaprogramming** using reflection APIs (not macros or DSLs)

### Why Adopt XXML?

**For Learning**: XXML's explicit syntax teaches memory management, ownership, and type systems. Code written in XXML serves as its own tutorial.

**For Safety**: Ownership rules are enforced at compile time. Reflection operations that would violate ownership are rejected. Thread-safety constraints (`Sendable`, `Sharable`) prevent data races.

**For Productivity**: Derive implementations (like JSON serialization, equality, hashing) are generated at compile time using normal language constructs—no macro languages to learn.

**For Introspection**: Runtime reflection lets you build plugin systems, serializers, debuggers, and test frameworks that work with any type while still respecting ownership.

## Features

- **Complete Compiler Pipeline**: Lexer → Parser → Semantic Analyzer → LLVM IR Code Generator
- **Native Compilation**: Direct compilation to native executables via LLVM
- **Ownership Semantics**: Explicit memory management with `^` (owned), `&` (reference), and `%` (copy)
- **Generic Templates**: Full template support with [constraints](docs/language/CONSTRAINTS.md)
- **Compile-Time Evaluation**: Constant expressions evaluated at compile-time ([details](docs/language/COMPILETIME.md))
- **Object-Oriented**: Classes, inheritance, access modifiers, methods, properties, and enumerations
- **Type-Safe**: Static type checking with comprehensive error reporting
- **Reflection System**: Runtime type introspection ([details](docs/advanced/REFLECTION.md))
- **Self-Hosting Standard Library**: Standard library written in XXML itself

## Quick Start

### Building the Compiler

```bash
# Using CMake Presets (recommended)
cmake --preset x64-release
cmake --build --preset x64-release

# Or manually
cmake -B build
cmake --build build --config Release
```

### Compiling XXML Code

```bash
# Compile to native executable
xxml input.XXML output.exe

# Compile with verbose output (level 2)
xxml input.XXML output.exe 2
```

### Hello World

```xxml
#import Language::Core;

[ Entrypoint
    {
        Instantiate String^ As <message> = String::Constructor("Hello, World!");
        Run Console::printLine(message);
        Exit(0);
    }
]
```

## Language Overview

### Ownership System

XXML uses explicit ownership semantics for memory safety:

| Modifier | Symbol | Meaning |
|----------|--------|---------|
| Owned | `^` | Unique ownership, responsible for lifetime |
| Reference | `&` | Borrowed reference, does not own |
| Copy | `%` | Value copy, creates independent copy |

```xxml
Property <ownedString> Types String^;    // Owned
Parameter <refString> Types String&;     // Reference
Parameter <copiedInt> Types Integer%;    // Copy
```

See [Ownership System](docs/language/OWNERSHIP.md) for details.

### Classes

```xxml
[ Class <Person> Final Extends None
    [ Private <>
        Property <name> Types String^;
        Property <age> Types Integer^;
    ]

    [ Public <>
        Constructor Parameters (
            Parameter <n> Types String^,
            Parameter <a> Types Integer^
        ) ->
        {
            Set name = n;
            Set age = a;
        }

        Method <greet> Returns String^ Parameters () Do
        {
            Return String::Constructor("Hello, ").concat(name);
        }
    ]
]
```

### Enumerations

```xxml
[ Enumeration <Color>
    Value <RED> = 1;
    Value <GREEN> = 2;
    Value <BLUE> = 3;
]

[ Enumeration <Key>
    Value <SPACE> = 32;
    Value <A> = 65;
    Value <B>;  // Auto-increments to 66
    Value <C>;  // Auto-increments to 67
]

// Access enum values
If (keyCode.equals(Key::SPACE)) -> {
    Run Console::printLine(String::Constructor("Space pressed"));
}
```

### Templates

```xxml
[ Class <Box> <T Constrains None> Final Extends None
    [ Public <>
        Property <value> Types T^;
        Constructor = default;
    ]
]

// Usage: type uses <>, constructor uses @
Instantiate Box<Integer>^ As <box> = Box@Integer::Constructor(Integer::Constructor(42));
```

See [Templates](docs/language/TEMPLATES.md) and [Constraints](docs/language/CONSTRAINTS.md).

### Constraints

Constraints restrict template parameters to types with specific capabilities:

```xxml
// Define a constraint requiring a 'getValue' method
[ Constraint <HasValue> <T>
    Require (F(Integer^)(*)() On a);  // Must have method returning Integer^
]

// Use constraint on template class
[ Class <ValuePrinter> <T Constrains HasValue<T>> Final Extends None
    [ Public <>
        Property <item> Types T^;

        Method <printValue> Returns None Parameters () Do
        {
            Run Console::printLine(item.getValue().toString());
        }
    ]
]

// Combine constraints with AND (,) or OR (|)
[ Class <Container> <T Constrains (Hashable<T>, Equatable<T>)> Final Extends None
    // T must satisfy BOTH Hashable AND Equatable
]

[ Class <Printable> <T Constrains Stringable<T> | Debuggable<T>> Final Extends None
    // T must satisfy EITHER Stringable OR Debuggable
]
```

See [Constraints](docs/language/CONSTRAINTS.md) for full documentation.

### Lambdas

```xxml
Instantiate Integer^ As <multiplier> = Integer::Constructor(5);

Instantiate F(Integer^)(Integer&)^ As <multiply> = [ Lambda [%multiplier] Returns Integer^ Parameters (
    Parameter <n> Types Integer&
) {
    Return n.multiply(multiplier);
}];

Instantiate Integer^ As <result> = multiply.call(Integer::Constructor(3));  // 15
```

See [Lambdas](docs/language/LAMBDAS.md).

### Lambda Templates

Lambda templates are generic anonymous functions with their own type parameters:

```xxml
// Define a generic identity function
Instantiate __function^ As <identity> = [ Lambda [] Templates <T Constrains None> Returns T^ Parameters (Parameter <x> Types T^)
{
    Return x;
} ];

// Call with different types
Instantiate Integer^ As <intResult> = identity<Integer>.call(Integer::Constructor(42));
Instantiate String^ As <strResult> = identity<String>.call(String::Constructor("Hello"));
```

Both `<Type>` and `@Type` syntaxes work for template arguments:

```xxml
identity<Integer>.call(x)   // Angle bracket syntax
identity@Integer.call(x)    // At-sign syntax
```

See [Lambda Templates](docs/language/TEMPLATES.md#lambda-templates) for full documentation.

### Compile-Time Evaluation

```xxml
// Compile-time constants are evaluated at compilation
Instantiate Compiletime Integer^ As <x> = Integer::Constructor(10);
Instantiate Compiletime Integer^ As <y> = Integer::Constructor(5);
Instantiate Compiletime Integer^ As <sum> = x.add(y);  // Computed at compile-time

// Compile-time strings
Instantiate Compiletime String^ As <greeting> = String::Constructor("Hello");

// Compile-time booleans for conditional compilation
Instantiate Compiletime Bool^ As <debug> = Bool::Constructor(true);
```

See [Compile-Time Evaluation](docs/language/COMPILETIME.md).

### Control Flow

```xxml
// Range-based for loop (start inclusive, end exclusive)
For (Integer^ <i> = 0 .. 10) ->
{
    Run Console::printLine(i.toString());
}

// While loop
While (condition) ->
{
    // body
}

// If/Else
If (condition) -> {
    // then
} Else -> {
    // else
}
```

## Project Structure

```
XXMLCompiler/
├── include/              # Header files
│   ├── Backends/         # Code generation backends (LLVM)
│   ├── CodeGen/          # Code generator interface
│   ├── Common/           # Error reporting, source locations
│   ├── Core/             # Type registry, operators, contexts
│   ├── Driver/           # Compilation orchestration
│   ├── Import/           # Module/import resolution
│   ├── Lexer/            # Tokenization
│   ├── Linker/           # Platform linkers (MSVC, GNU)
│   ├── Parser/           # AST and parsing
│   ├── Semantic/         # Type checking, symbol tables
│   └── Utils/            # Process utilities
├── src/                  # Implementation files
├── Language/             # XXML Standard Library (source)
│   ├── Collections/      # Array, List, HashMap, Set, Stack, Queue
│   ├── Core/             # Bool, Double, Float, Integer, String, Mem, None
│   ├── IO/               # File operations
│   ├── System/           # Console, System
│   ├── Math/             # Math functions
│   ├── Text/             # Regex, StringUtils
│   ├── Time/             # DateTime
│   ├── Network/          # HTTP
│   ├── Format/           # JSON
│   ├── Reflection/       # Runtime type introspection
│   └── Concurrent/       # Threading
├── runtime/              # C runtime for LLVM IR
├── examples/             # Example XXML programs
├── tests/                # Test files
├── docs/                 # Documentation
└── CMakeLists.txt        # CMake build configuration
```

## Standard Library

| Module | Description |
|--------|-------------|
| `Language::Core` | Primitives: Integer, String, Bool, Float, Double, None, Mem |
| `Language::Collections` | Array, List, HashMap, Set, Stack, Queue |
| `Language::System` | Console I/O, System utilities |
| `Language::IO` | File operations |
| `Language::Math` | Mathematical functions |
| `Language::Text` | Regex, String utilities |
| `Language::Time` | DateTime handling |
| `Language::Format` | JSON parsing |
| `Language::Concurrent` | Threading, mutexes, atomics |
| `Language::Reflection` | Runtime type introspection |
| `Language::Network` | HTTP client |

## Documentation

See the [Documentation Index](docs/INDEX.md) for complete documentation.

### Language Reference

| Document | Description |
|----------|-------------|
| [Syntax](docs/language/SYNTAX.md) | Keywords, operators, statements |
| [Ownership](docs/language/OWNERSHIP.md) | Memory management with `^`, `&`, `%` |
| [Classes](docs/language/CLASSES.md) | Class declarations and inheritance |
| [Structures](docs/language/STRUCTURES.md) | Stack-allocated value types |
| [Templates](docs/language/TEMPLATES.md) | Generic programming |
| [Constraints](docs/language/CONSTRAINTS.md) | Template constraints system |
| [Lambdas](docs/language/LAMBDAS.md) | Anonymous functions and closures |
| [Compile-Time](docs/language/COMPILETIME.md) | Compile-time evaluation |

### Standard Library

| Document | Description |
|----------|-------------|
| [Core Types](docs/stdlib/CORE.md) | Integer, String, Bool, Float, Double |
| [Collections](docs/stdlib/COLLECTIONS.md) | List, HashMap, Set, Array, Stack, Queue |
| [Console](docs/stdlib/CONSOLE.md) | Console I/O |
| [File I/O](docs/stdlib/FILE_IO.md) | File operations |
| [Math](docs/stdlib/MATH.md) | Mathematical functions |

### Advanced Topics

| Document | Description |
|----------|-------------|
| [FFI](docs/advanced/FFI.md) | Foreign function interface |
| [Native Types](docs/advanced/NATIVE_TYPES.md) | Low-level types and syscalls |
| [Reflection](docs/advanced/REFLECTION.md) | Runtime type introspection |
| [Threading](docs/advanced/THREADING.md) | Concurrency and synchronization |
| [Destructors](docs/advanced/DESTRUCTORS.md) | RAII and resource cleanup |

### Tools & Other

| Document | Description |
|----------|-------------|
| [CLI Reference](docs/tools/CLI.md) | Compiler command-line options |
| [Import System](docs/tools/IMPORTS.md) | Module resolution |
| [Limitations](docs/LIMITATIONS.md) | Known limitations |
| [Quick Start](QUICKSTART.md) | Getting started guide |
| [Contributing](CONTRIBUTING.md) | Contribution guidelines |

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- CMake 3.20 or higher
- LLVM (for code generation)

## Testing

```bash
cd build
ctest

# Or run specific test
./bin/Release/xxml ../tests/Hello.XXML hello.exe
./hello.exe
```

## Known Limitations

See [Limitations](docs/LIMITATIONS.md) for a complete list including:
- No exception handling system
- No interface/trait system
- No operator overloading (use methods like `.add()`)
- No pattern matching or switch statements
- Reflection: dynamic method invocation not yet implemented

## Roadmap

XXML is evolving toward **Safe Reflective Metaprogramming**—the ability to introspect and generate code at compile-time while respecting ownership rules. See [ROADMAP.md](docs/ROADMAP.md) for the full plan.

### Current Focus

| Phase | Goal | Status |
|-------|------|--------|
| **Phase 0** | Groundwork & Codebase Triage | In Progress |
| **Phase 1** | Runtime Reflection Completion | Planned |
| **Phase 2** | Compile-Time Reflection & Metaprogramming | Planned |
| **Phase 3** | Ownership Introspection & Diagnostics | Planned |

### Vision

- **Derive macros without macros**: Generate `Eq`, `Hash`, `ToString`, `JSON` implementations using compile-time reflection
- **Ownership-aware introspection**: Reflection APIs that enforce `^`/`&`/`%` semantics
- **Sendable/Sharable constraints**: Compiler-enforced thread-safety guarantees

## License

This project is licensed under the MIT License - see LICENSE file for details.

---

**XXML Compiler v3.0.0**
