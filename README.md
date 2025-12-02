# XXML Compiler

A production-ready compiler for the XXML programming language that compiles to LLVM IR with native executable generation.

## Features

- **Complete Compiler Pipeline**: Lexer → Parser → Semantic Analyzer → LLVM IR Code Generator
- **Native Compilation**: Direct compilation to native executables via LLVM
- **Ownership Semantics**: Explicit memory management with `^` (owned), `&` (reference), and `%` (copy)
- **Generic Templates**: Full template support with [constraints](docs/CONSTRAINTS.md)
- **Compile-Time Evaluation**: Constant expressions evaluated at compile-time ([details](docs/COMPILETIME.md))
- **Object-Oriented**: Classes, inheritance, access modifiers, methods, and properties
- **Type-Safe**: Static type checking with comprehensive error reporting
- **Reflection System**: Runtime type introspection ([details](docs/REFLECTION_SYSTEM.md))
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

See [Types and Ownership](docs/LANGUAGE_SPEC.md#types-and-ownership) for details.

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

See [Templates](docs/TEMPLATES.md) and [Constraints](docs/CONSTRAINTS.md).

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

See [Constraints](docs/CONSTRAINTS.md) for full documentation.

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

See [Lambdas](docs/LANGUAGE_SPEC.md#lambdas-and-function-references).

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

See [Lambda Templates](docs/TEMPLATES.md#lambda-templates) for full documentation.

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

See [Compile-Time Evaluation](docs/COMPILETIME.md).

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

| Document | Description |
|----------|-------------|
| [Language Specification](docs/LANGUAGE_SPEC.md) | Complete language syntax and semantics |
| [Templates](docs/TEMPLATES.md) | Generic programming with templates |
| [Constraints](docs/CONSTRAINTS.md) | Template constraints system |
| [Compile-Time Evaluation](docs/COMPILETIME.md) | Compile-time constant evaluation |
| [Foreign Function Interface](docs/FFI.md) | Foreign function interface system |
| [Imports](docs/IMPORTS.md) | Imports (#import ...) system |
| [Advanced Features](docs/ADVANCED_FEATURES.md) | Destructors, native types, syscalls |
| [Reflection System](docs/REFLECTION_SYSTEM.md) | Runtime type introspection |
| [Threading](docs/THREADING.md) | Concurrency and synchronization |
| [Architecture](docs/ARCHITECTURE.md) | Compiler architecture |
| [Limitations](docs/LIMITATIONS.md) | Known limitations and TODOs |
| [Quick Start](QUICKSTART.md) | Getting started guide |
| [Contributing](CONTRIBUTING.md) | Contribution guidelines |
| [Testing](TESTING.md) | Test suite information |

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

See [docs/LIMITATIONS.md](docs/LIMITATIONS.md) for a complete list including:
- No exception handling system
- No interface/trait system
- No operator overloading (use methods like `.add()`)
- No pattern matching or switch statements
- Reflection: dynamic method invocation not yet implemented

## License

This project is licensed under the MIT License - see LICENSE file for details.

---

**XXML Compiler v2.0**
