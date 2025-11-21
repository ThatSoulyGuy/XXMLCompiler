# XXML Compiler

A production-ready compiler for the XXML programming language that compiles to LLVM IR with native executable generation.

## Features

- **Complete Compiler Pipeline**: Lexer → Parser → Semantic Analyzer → LLVM IR Code Generator
- **Native Compilation**: Direct compilation to native executables via LLVM
- **Ownership Semantics**: Explicit memory management with `^` (owned), `&` (reference), and `%` (copy)
- **Generic Templates**: Full template support with constraints
- **Object-Oriented**: Classes, inheritance, access modifiers, methods, and properties
- **Type-Safe**: Static type checking with comprehensive error reporting
- **Self-Hosting Standard Library**: Standard library written in XXML itself

## Quick Start

### Building the Compiler

```bash
cmake -B build
cmake --build build --config Release
```

### Compiling XXML Code

```bash
# Compile an XXML file to LLVM IR
xxml input.XXML output.ll 2

# The compiler can also produce native executables directly
```

### Example: Hello World

```xxml
#import System;

[ Entrypoint
    {
        Instantiate String As <message> = String::Constructor("Hello, World!");
        Run System::Print(message);
        Exit(0);
    }
]
```

## Language Features

### Ownership System

XXML uses explicit ownership semantics:

- `^` **Owned** - Unique ownership
- `&` **Reference** - Borrowed reference
- `%` **Copy** - Value copy

```xxml
Property <ownedString> Types String^;    // Owned
Property <refString> Types String&;      // Reference
Property <copiedInt> Types Integer%;     // Copy
```

### Classes and Inheritance

```xxml
[ Class <MyClass> Final Extends BaseClass
    [ Public <>
        Property <x> Types Integer^;
        Constructor = default;

        Method <doSomething> Returns None Parameters () ->
        {
            // Method body
        }
    ]
    [ Private <>
        Property <privateData> Types String^;
    ]
]
```

### Templates with Constraints

```xxml
[ Template <T> Where T Numeric
    [ Class <Container> Final Extends None
        [ Public <>
            Property <value> Types T^;
        ]
    ]
]
```

### Namespaces

```xxml
[ Namespace <MyNamespace::Nested>
    [ Class <MyClass> Final Extends None
        // ...
    ]
]
```

### Control Flow

```xxml
// For loops with ranges
For (Integer <i> = 0 .. 10) ->
{
    Run System::Print(String::Convert(i));
}

// Method calls
Run myObject.someMethod(arg1, &arg2);

// Variable instantiation
Instantiate Integer As <x> = 42i;
```

## Project Structure

```
XXMLCompiler/
├── include/           # Header files
│   ├── Backends/      # Code generation backends (LLVM, C++)
│   ├── CodeGen/       # Code generator interface
│   ├── Common/        # Error reporting, source locations
│   ├── Core/          # Type registry, operators, contexts
│   ├── Driver/        # Compilation orchestration
│   ├── Import/        # Module/import resolution
│   ├── Lexer/         # Tokenization
│   ├── Linker/        # Platform linkers (MSVC, GNU)
│   ├── Parser/        # AST and parsing
│   ├── Semantic/      # Type checking, symbol tables
│   └── Utils/         # Process utilities
├── src/               # Implementation files (mirrors include/)
├── Language/          # XXML Standard Library
│   ├── Collections/   # Array, List, HashMap, Set, Stack, Queue
│   ├── Core/          # Bool, Double, Float, Integer, String, Mem, None
│   ├── IO/            # File operations
│   ├── System/        # Console, System
│   ├── Math/          # Math functions
│   ├── Text/          # Regex, StringUtils
│   ├── Time/          # DateTime
│   ├── Network/       # HTTP
│   ├── Format/        # JSON
│   └── Concurrent/    # Threading
├── runtime/           # C runtime for LLVM IR
├── examples/          # Example XXML programs
├── tests/             # Test files
├── docs/              # Documentation
└── CMakeLists.txt     # CMake build configuration
```

## Compiler Architecture

### 1. Lexical Analysis (Lexer)
- Tokenizes source code into tokens
- Handles keywords, identifiers, literals, operators
- Tracks source locations for error reporting

### 2. Syntax Analysis (Parser)
- Builds Abstract Syntax Tree (AST)
- Recursive descent parser
- Error recovery and detailed error messages

### 3. Semantic Analysis
- Symbol table management
- Type checking and inference
- Ownership semantics validation
- Template instantiation
- Name resolution

### 4. Code Generation (LLVM Backend)
- Generates LLVM IR
- Supports multiple target platforms
- Links with platform-specific linkers (MSVC, GNU)

## Standard Library

The XXML standard library is written in XXML itself:

- **Language::Core** - Primitive types (Integer, String, Bool, Float, Double)
- **Language::Collections** - Array, List, HashMap, Set, Stack, Queue
- **Language::System** - Console I/O, System utilities
- **Language::IO** - File operations
- **Language::Math** - Mathematical functions
- **Language::Text** - Regex, String utilities
- **Language::Time** - DateTime handling
- **Language::Format** - JSON parsing
- **Language::Concurrent** - Threading support

## Requirements

- C++20 compatible compiler (GCC 10+, Clang 10+, MSVC 2019+)
- CMake 3.20 or higher

## Building

**Using CMake Presets (recommended):**
```bash
cmake --preset x64-release
cmake --build --preset x64-release
```

**Or manually:**
```bash
cmake -B build
cmake --build build --config Release
```

## Testing

```bash
cd build
ctest

# Or manually:
./bin/Release/xxml ../examples/Hello.XXML output.ll 2
```

## Documentation

- [Language Specification](docs/LANGUAGE_SPEC.md)
- [Compiler Architecture](docs/ARCHITECTURE.md)
- [Quick Start Guide](QUICKSTART.md)
- [Contributing Guide](CONTRIBUTING.md)
- [Testing Guide](TESTING.md)

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

This project is licensed under the MIT License - see LICENSE file for details.

---

**XXML Compiler v2.0** - A modern, safe, and efficient programming language
