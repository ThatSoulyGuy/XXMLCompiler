# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Current Development Focus

XXML is evolving toward **Safe Reflective Metaprogramming**. See [docs/ROADMAP.md](docs/ROADMAP.md) for the full plan.

**Active Phase**: Phase 0 (Groundwork & Codebase Triage)
- Stabilizing test suite
- Completing documentation restructure
- Removing dead code paths

**Core Philosophy**: "The more explicit the syntax, the greater one's conceptual understanding is."

**Key Language Goals**:
- Explicit ownership semantics (`^`/`&`/`%`) for memory safety
- Rich runtime reflection that respects ownership rules
- Compile-time metaprogramming using reflection APIs (not macros)

## Build Commands

```bash
# Using CMake Presets (recommended)
cmake --preset release
cmake --build --preset release

# Compiler output: build/release/bin/xxml.exe
```

## Running the Compiler

```bash
# Compile XXML to native executable
build/release/bin/xxml.exe input.XXML -o output.exe

# Generate LLVM IR only (for debugging)
build/release/bin/xxml.exe input.XXML -o output.ll --ir

# Compile annotation processor to DLL
build/release/bin/xxml.exe --processor MyAnnot.XXML -o MyAnnot.dll

# Use annotation processor
build/release/bin/xxml.exe --use-processor=MyAnnot.dll App.XXML -o app.exe
```

## Testing

```bash
# Run CMake tests
cd build/release && ctest

# Test a specific XXML file
build/release/bin/xxml.exe tests/TestFile.XXML -o test.exe && ./test.exe
```

## Architecture

The XXML compiler is a multi-stage compiler producing native executables via LLVM IR:

```
Source (.XXML) → Lexer → Parser → SemanticAnalyzer → LLVMBackend → LLVM IR → Native Executable
```

### Key Directories

| Directory | Purpose |
|-----------|---------|
| `include/` | Headers organized by component (Lexer/, Parser/, Semantic/, Backends/) |
| `src/` | Implementation files matching include/ structure |
| `Language/` | Standard library written in XXML (Core/, Collections/, System/, etc.) |
| `runtime/` | C runtime library (xxml_llvm_runtime.c) linked into compiled programs |
| `tests/` | Test XXML files |
| `docs/` | Documentation organized by topic (language/, stdlib/, advanced/, tools/) |

### Compilation Pipeline Components

1. **Lexer** (`include/Lexer/`): Tokenizes source into tokens with location info
2. **Parser** (`include/Parser/`): Builds AST from tokens, recursive descent parser
3. **Semantic Analyzer** (`include/Semantic/`): Multi-pass analysis pipeline:
   - TypeCanonicalizer → SemanticAnalysis → TemplateExpander → OwnershipAnalyzer → LayoutComputer → ABILowering
4. **LLVM Backend** (`include/Backends/`): Generates LLVM IR from validated AST

### Code Generation Structure

The backend uses modular codegen in `src/Backends/Codegen/`:
- `ExprCodegen/` - Expression code generation
- `StmtCodegen/` - Statement code generation
- `DeclCodegen/` - Declaration code generation (classes, methods, constructors)
- `TemplateCodegen/` - Template instantiation
- `PreambleGen/` - Runtime preamble generation

### Key Files

- `include/Parser/AST.h` - All AST node definitions
- `include/Semantic/SemanticAnalyzer.h` - Main semantic analysis entry point
- `include/Backends/LLVMBackend.h` - LLVM IR generation entry point
- `include/Backends/Codegen/CodegenContext.h` - Shared state for code generation
- `src/main.cpp` - Compiler entry point and CLI handling

### Naming Conventions

- **C++ Code**: 4 spaces, PascalCase classes, camelCase methods/variables, UPPER_SNAKE_CASE constants
- **XXML Code**: Tab indentation, PascalCase for classes/methods
- **Name Mangling**: `ClassName_methodName` for generated LLVM functions

## XXML Language Quick Reference

### Ownership Modifiers
- `^` (owned) - Unique ownership, responsible for lifetime
- `&` (reference) - Borrowed reference, does not own
- `%` (copy) - Value copy

### Basic Syntax
```xxml
#import Language::Core;

[ Class <MyClass> Final Extends None
    [ Private <>
        Property <value> Types Integer^;
    ]
    [ Public <>
        Constructor Parameters (Parameter <v> Types Integer^) -> {
            Set value = v;
        }
        Method <getValue> Returns Integer^ Parameters () Do {
            Return value;
        }
    ]
]

[ Entrypoint
    {
        Instantiate MyClass^ As <obj> = MyClass::Constructor(Integer::Constructor(42));
        Run Console::printLine(obj.getValue().toString());
        Exit(0);
    }
]
```
