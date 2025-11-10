# XXML Compiler

A production-ready, self-hosting compiler for the XXML programming language that transpiles to C++.

## Features

- **Complete Compiler Pipeline**: Lexer → Parser → Semantic Analyzer → C++ Code Generator
- **Ownership Semantics**: Explicit memory management with `^` (owned), `&` (reference), and `%` (copy)
- **Object-Oriented**: Classes, inheritance, access modifiers, methods, and properties
- **Type-Safe**: Static type checking with comprehensive error reporting
- **Modular Architecture**: Separate libraries for each compilation phase
- **Self-Hosting**: Standard library written in XXML itself
- **Production-Ready**: Comprehensive error handling, diagnostics, and testing

## Quick Start

### Building the Compiler

**Windows:**
```bash
build.bat
```

**Linux/macOS:**
```bash
chmod +x build.sh
./build.sh
```

**Or using CMake directly:**
```bash
mkdir build && cd build
cmake ..
cmake --build . --config Release
```

### Compiling XXML Code

```bash
# Compile an XXML file to C++
xxml input.xxml output.cpp

# Compile the generated C++ code
g++ -std=c++17 output.cpp -o program
# or
cl /EHsc /std:c++17 output.cpp
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

- `^` **Owned** - Unique ownership (transpiles to `std::unique_ptr`)
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
├── include/          # Header files
│   ├── Common/       # Error reporting, source locations
│   ├── Lexer/        # Tokenization
│   ├── Parser/       # AST and parsing
│   ├── Semantic/     # Type checking, symbol tables
│   └── CodeGen/      # C++ code generation
├── src/              # Implementation files
│   ├── Common/
│   ├── Lexer/
│   ├── Parser/
│   ├── Semantic/
│   ├── CodeGen/
│   └── main.cpp      # Compiler driver
├── runtime/          # XXML standard library
│   ├── Language/
│   │   └── Core.XXML
│   ├── Integer.XXML
│   ├── String.XXML
│   └── System.XXML
├── Test.XXML         # Test file
├── CMakeLists.txt    # CMake build configuration
└── README.md         # This file
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
- Type checking
- Ownership semantics validation
- Name resolution

### 4. Code Generation
- Transpiles AST to C++ code
- Maps ownership types to C++ smart pointers
- Generates readable, commented C++ output

## Standard Library

The XXML runtime library is written in XXML itself, demonstrating self-hosting capabilities:

- **Language::Core** - Core language functionality
- **Integer** - Integer type with arithmetic operations
- **String** - String manipulation and utilities
- **System** - I/O and system functions

See [runtime/README.md](runtime/README.md) for details.

## Documentation

- [Language Specification](docs/LANGUAGE_SPEC.md) - Complete language reference
- [Compiler Architecture](docs/ARCHITECTURE.md) - Detailed compiler design
- [API Documentation](docs/API.md) - Library and API reference
- [Contributing Guide](CONTRIBUTING.md) - How to contribute

## Examples

See the `Test.XXML` file for a comprehensive example demonstrating:
- Imports
- Namespaces
- Classes with inheritance
- Properties with ownership semantics
- Constructors and methods
- Entrypoint with statements
- For loops and method calls

## Error Handling

The compiler provides detailed error messages with:
- Source file location (file:line:column)
- Error context with source code snippet
- Color-coded output (error/warning/note)
- Helpful suggestions

Example error output:
```
test.xxml:5:20: error: Undeclared identifier 'foo' [3000]
    Run System::Print(foo);
                      ^
```

## Requirements

- C++17 compatible compiler (GCC 7+, Clang 5+, MSVC 2017+)
- CMake 3.15 or higher
- Standard C++ libraries

## Testing

```bash
# Run CMake tests
cd build
ctest

# Compile Test.XXML
./bin/xxml ../Test.XXML test_output.cpp

# Compile and run the generated C++
g++ -std=c++17 test_output.cpp -o test
./test
```

## Contributing

Contributions are welcome! Please read [CONTRIBUTING.md](CONTRIBUTING.md) for:
- Code style guidelines
- Development workflow
- Testing requirements
- Pull request process

## License

This project is licensed under the MIT License - see LICENSE file for details.

## Authors

- Built with Claude Code by Anthropic

## Roadmap

### Completed ✓
- [x] Complete lexer with all token types
- [x] Parser with full AST
- [x] Semantic analyzer with type checking
- [x] C++ code generator
- [x] Runtime library in XXML
- [x] Build system integration
- [x] Comprehensive error handling
- [x] Documentation

### Future Enhancements
- [ ] Generic/template support
- [ ] Advanced optimizations
- [ ] Debugger integration
- [ ] IDE language server protocol
- [ ] Package manager
- [ ] More collection types
- [ ] Async/await support
- [ ] Foreign function interface (FFI)

## Support

For issues, questions, or contributions:
- Open an issue on GitHub
- Check existing documentation
- Review example code in Test.XXML

---

**XXML Compiler v1.0** - A modern, safe, and efficient programming language
