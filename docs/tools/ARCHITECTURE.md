# XXML Compiler Architecture

## Overview

The XXML compiler is a multi-stage compiler that converts XXML source code to native executables via LLVM IR. It follows a traditional compiler architecture with distinct phases.

## Compilation Pipeline

```
Source Code (.XXML)
       ↓
   [Lexer] ← Error Reporter
       ↓
    Tokens
       ↓
   [Parser] ← Error Reporter
       ↓
     AST
       ↓
[Semantic Analyzer] ← Symbol Table ← Error Reporter
       ↓
  Validated AST
       ↓
[LLVM Backend] ← Error Reporter
       ↓
  LLVM IR (.ll)
       ↓
[Clang/LLVM]
       ↓
  Object Code (.obj)
       ↓
[Platform Linker] + Runtime Library
       ↓
  Native Executable
```

## LLVM Backend Architecture

The LLVM backend generates LLVM IR from the validated AST using a modular, type-safe architecture.

### Modular Codegen System (`include/Backends/Codegen/`)

The code generation is split into specialized modules, coordinated by `ModularCodegen`:

```
ModularCodegen (orchestrator)
├── ExprCodegen     - Expression code generation
│   ├── BinaryCodegen       - Binary operations (+, -, *, /, comparisons)
│   ├── CallCodegen         - Method/function calls
│   ├── IdentifierCodegen   - Variable references
│   ├── LiteralCodegen      - Integer, float, string, bool literals
│   └── MemberAccessCodegen - Property access, method resolution
├── StmtCodegen     - Statement code generation
│   ├── AssignmentCodegen   - Variable assignments
│   ├── ControlFlowCodegen  - If/else, while, for loops
│   └── ReturnCodegen       - Return statements
├── DeclCodegen     - Declaration code generation
│   ├── ClassCodegen        - Class structure generation
│   ├── ConstructorCodegen  - Constructor methods
│   ├── MethodCodegen       - Regular methods
│   ├── NativeMethodCodegen - FFI native methods
│   └── EntrypointCodegen   - Main function generation
├── FFICodegen      - Foreign function interface
├── MetadataGen     - Reflection metadata
├── PreambleGen     - Runtime preamble generation
└── TemplateGen     - Template instantiation
```

### CodegenContext

Shared context for all codegen modules, providing:

- **Variable Management**: Scoped variable tracking with allocas
- **Class Registry**: Struct types and property information
- **Loop Stack**: Break/continue target blocks
- **Lambda Tracking**: Closure types and function names
- **Native Method Info**: FFI parameter/return type mappings
- **String Literals**: Global string constant deduplication

### Type-Safe LLVM IR (`include/Backends/LLVMIR/`)

A compile-time type-safe abstraction over LLVM IR:

| Component | Description |
|-----------|-------------|
| `TypedValue<T>` | Type-safe value wrappers (`IntValue`, `FloatValue`, `PtrValue`, `BoolValue`) |
| `AnyValue` | Runtime type variant when compile-time type is unknown |
| `IRBuilder` | Type-safe instruction builder preventing invalid IR |
| `TypedModule` | Module with type context and constant factories |
| `TypedInstructions` | Type-safe instruction abstractions |

**Key Design Principles:**
- Integer operations only accept/return `IntValue`
- Float operations only accept/return `FloatValue`
- Pointer operations return `PtrValue`
- Comparisons return `BoolValue` (i1)
- Conditional branches require `BoolValue`, not generic integer

**Example:**
```cpp
// Type-safe: compiler prevents passing float to integer add
IntValue result = builder.createAdd(intA, intB);  // OK
IntValue wrong = builder.createAdd(intA, floatB); // Compile error!
```

### Legacy IR Infrastructure (`include/Backends/IR/`)

Lower-level IR representation (used internally):

- **Types** (`Types.h`) - Type system (VoidType, IntegerType, PointerType, StructType, etc.)
- **Values** (`Values.h`) - Value hierarchy (Constant, GlobalVariable, Function, Argument)
- **Instructions** (`Instructions.h`) - All IR instructions (Load, Store, Call, Branch, etc.)
- **BasicBlock** (`Function.h`) - Basic blocks with terminators
- **Function** (`Function.h`) - Functions with arguments and basic blocks
- **Module** (`Module.h`) - Top-level container
- **Emitter** (`Emitter.h`) - LLVM IR text generation

### Runtime Integration

Compiled programs link against `libXXMLLLVMRuntime`:
- Memory management (`xxml_malloc`, `xxml_free`)
- Core types (Integer, String, Bool, Float, Double)
- Console I/O (`Console_printLine`, etc.)
- Reflection runtime for type introspection

## Phase 1: Lexical Analysis

**Location:** `include/Lexer/`, `src/Lexer/`

### Components

- **Token** - Represents a lexical token with type, lexeme, and location
- **TokenType** - Enumeration of all token types
- **Lexer** - Tokenizes source code into a stream of tokens

### Responsibilities

1. Read source characters
2. Group characters into tokens
3. Recognize keywords, identifiers, literals, operators
4. Handle comments
5. Track source locations (file, line, column)
6. Report lexical errors

### Token Types

- **Keywords**: `import`, `Namespace`, `Class`, `Method`, etc.
- **Identifiers**: Regular and angle-bracketed (`<name>`)
- **Literals**: Integer (`42i`), String (`"text"`), Boolean
- **Operators**: `+`, `-`, `*`, `/`, `::`, `.`, `->`, etc.
- **Delimiters**: `[`, `]`, `{`, `}`, `(`, `)`, `;`, `,`
- **Special**: `^`, `&`, `%` (ownership modifiers)

### Key Algorithms

**Identifier Lexing:**
```
if current is letter or underscore:
    collect alphanumeric characters
    if text is keyword:
        return keyword token
    else:
        return identifier token
```

**Angle Bracket Identifier:**
```
if current is '<' and next is letter:
    advance past '<'
    collect characters until '>'
    return angle bracket identifier token
```

## Phase 2: Syntax Analysis

**Location:** `include/Parser/`, `src/Parser/`

### Components

- **AST Node Classes** - Hierarchy of abstract syntax tree nodes
- **Parser** - Recursive descent parser building AST

### AST Hierarchy

```
ASTNode (abstract)
├── Declaration
│   ├── ImportDecl
│   ├── NamespaceDecl
│   ├── ClassDecl
│   ├── PropertyDecl
│   ├── ConstructorDecl
│   ├── MethodDecl
│   ├── ParameterDecl
│   └── EntrypointDecl
├── Statement
│   ├── InstantiateStmt
│   ├── RunStmt
│   ├── ForStmt
│   ├── ExitStmt
│   └── ReturnStmt
├── Expression
│   ├── IntegerLiteralExpr
│   ├── StringLiteralExpr
│   ├── BoolLiteralExpr
│   ├── IdentifierExpr
│   ├── ReferenceExpr
│   ├── MemberAccessExpr
│   ├── CallExpr
│   └── BinaryExpr
└── TypeRef
```

### Parsing Strategy

**Recursive Descent** with predictive parsing:

```cpp
Declaration parseDeclaration() {
    if (match('#')) {
        if (match('import'))
            return parseImport();
    }
    if (match('[')) {
        if (check('Namespace'))
            return parseNamespace();
        if (check('Class'))
            return parseClass();
        // ...
    }
}
```

**Precedence Climbing** for expressions:

```
Expression (lowest precedence)
├── Logical OR (||)
├── Logical AND (&&)
├── Equality (==, !=)
├── Comparison (<, >, <=, >=)
├── Addition (+, -)
├── Multiplication (*, /, %)
├── Unary (-, !, &)
└── Primary (literals, identifiers, calls)
```

### Error Recovery

- **Synchronization Points**: After `;`, `]`, `}`
- **Error Messages**: Include source location and context
- **Continue Parsing**: Attempt to recover and find more errors

## Phase 3: Semantic Analysis

**Location:** `include/Semantic/`, `src/Semantic/`

### Components

- **Symbol** - Represents a declared entity
- **Scope** - Manages symbols in a lexical scope
- **SymbolTable** - Hierarchical scope management
- **SemanticAnalyzer** - AST visitor performing semantic checks

### Symbol Table Structure

```
Global Scope
├── Namespace: RenderStar
│   └── Namespace: Default
│       └── Class: MyClass
│           ├── Property: x (Integer^)
│           ├── Property: someString (String^)
│           ├── Constructor: default
│           └── Method: someMethod
│               ├── Parameter: int1 (Integer%)
│               └── Parameter: str (String&)
└── Entrypoint Scope
    ├── Variable: myClass
    ├── Variable: myString
    └── For Loop Scope
        └── Variable: x
```

### Semantic Checks

1. **Name Resolution**
   - Identifier declared before use
   - Qualified names resolve correctly
   - No duplicate declarations in same scope

2. **Type Checking**
   - Variable initialization type matches declaration
   - Method arguments match parameters
   - Return types match method signatures
   - Binary operations use compatible types

3. **Ownership Validation**
   - Owned (`^`) values are not aliased
   - References (`&`) point to valid owned values
   - Copies (`%`) are properly duplicated

4. **Class Validation**
   - Base classes exist
   - No circular inheritance
   - Access modifiers respected

5. **Method Validation**
   - Method exists on class
   - Correct number of arguments
   - Argument types compatible with parameters

### Visitor Pattern

The semantic analyzer uses the Visitor pattern to traverse the AST:

```cpp
class SemanticAnalyzer : public ASTVisitor {
    void visit(ClassDecl& node) override {
        enterScope(node.name);
        defineSymbol(node.name, CLASS, ...);
        for (auto& section : node.sections)
            section->accept(*this);
        exitScope();
    }
};
```

## Phase 4: Code Generation (LLVM IR)

**Location:** `include/Backends/`, `src/Backends/`

### Components

- **LLVMBackend** - Main entry point for LLVM IR generation
- **ModularCodegen** - Orchestrates specialized codegen modules
- **CodegenContext** - Shared state across all modules

### Translation to LLVM IR

**Classes → Structs:**
```xxml
[ Class <MyClass> Final Extends None
    [ Public <>
        Property <x> Types Integer^;
        Property <name> Types String^;
    ]
]
```
↓
```llvm
%MyClass = type { ptr, ptr }  ; x, name as pointers
```

**Ownership Types → Pointers:**
```xxml
Property <data> Types String^;  // Owned
Property <ref> Types String&;   // Reference
Property <val> Types Integer%;  // Copy
```
↓
```llvm
; All become ptr in LLVM IR (opaque pointers)
; Ownership semantics enforced at semantic analysis phase
```

**Methods → Functions:**
```xxml
Method <foo> Returns Integer^ Parameters (Parameter <x> Types Integer%) Do
{
    Return x.add(Integer::Constructor(1));
}
```
↓
```llvm
define ptr @MyClass_foo(ptr %this, ptr %x) {
entry:
    ; ... method body
    ret ptr %result
}
```

**For Loops → Basic Blocks:**
```xxml
For (Integer^ <i> = 0 .. 10) -> { ... }
```
↓
```llvm
for.init:
    %i = alloca ptr
    store ptr %zero, ptr %i
    br label %for.cond
for.cond:
    %cmp = icmp slt i64 %i.val, 10
    br i1 %cmp, label %for.body, label %for.end
for.body:
    ; ... loop body
    br label %for.inc
for.inc:
    ; increment i
    br label %for.cond
for.end:
    ; continue after loop
```

**Entrypoint → main:**
```xxml
[ Entrypoint { Exit(0); } ]
```
↓
```llvm
define i32 @main() {
entry:
    call void @__xxml_init()
    ; ... entrypoint body
    ret i32 0
}
```

### Code Generation Strategy

1. **Multi-Pass Generation**: Preamble → Classes → Methods → Entrypoint
2. **Scope Tracking**: Variable scopes with proper lifetime management
3. **Type Mapping**: XXML types → LLVM types via `CodegenContext::mapType()`
4. **Name Mangling**: `ClassName_methodName` convention

## Common Infrastructure

**Location:** `include/Common/`, `src/Common/`

### Error Reporting

**ErrorReporter** accumulates errors during compilation:

```cpp
class ErrorReporter {
    void reportError(ErrorCode code, string message, SourceLocation loc);
    void reportWarning(ErrorCode code, string message, SourceLocation loc);
    bool hasErrors();
    void printErrors();
};
```

**Error Format:**
```
filename.xxml:10:5: error: Undeclared identifier 'foo' [3000]
    Run System::Print(foo);
                      ^
```

### Source Locations

**SourceLocation** tracks position in source:

```cpp
struct SourceLocation {
    string filename;
    size_t line;
    size_t column;
    size_t position;
};
```

## Data Flow

### Compilation Process

1. **Read Source** → String
2. **Tokenize** → Vector<Token>
3. **Parse** → unique_ptr<Program>
4. **Analyze** → Validated AST + SymbolTable
5. **Generate IR** → LLVM IR Module
6. **Emit IR** → LLVM IR text (.ll)
7. **Compile** → Object file (.obj) via Clang
8. **Link** → Native executable

### Error Handling

Errors can occur at any phase:

```
Lexer Errors:
- Unexpected character
- Unterminated string
- Invalid number literal

Parser Errors:
- Unexpected token
- Missing delimiter
- Invalid syntax

Semantic Errors:
- Undeclared identifier
- Type mismatch
- Duplicate declaration
- Undefined type

Code Gen Errors:
- Internal errors (should not occur in production)
```

## Design Patterns

### Visitor Pattern

Used for AST traversal in semantic analysis and code generation:

```cpp
class ASTVisitor {
    virtual void visit(ClassDecl& node) = 0;
    virtual void visit(MethodDecl& node) = 0;
    // ...
};
```

### Builder Pattern

Used in Parser to construct AST:

```cpp
auto classDecl = make_unique<ClassDecl>(name, isFinal, baseClass, loc);
for (auto& section : sections) {
    classDecl->sections.push_back(move(section));
}
return classDecl;
```

### Strategy Pattern

Different code generation backends:
- **LLVM Backend** (current) - Native executable via LLVM IR
- Interpreter (future) - Direct AST interpretation

## Performance Considerations

### Time Complexity

- **Lexing**: O(n) where n = source size
- **Parsing**: O(n) where n = number of tokens
- **Semantic Analysis**: O(n * m) where n = nodes, m = average scope depth
- **Code Generation**: O(n) where n = AST nodes

### Space Complexity

- **Tokens**: O(n) where n = number of tokens
- **AST**: O(n) where n = AST nodes
- **Symbol Table**: O(s) where s = number of symbols
- **Generated Code**: O(n) where n = AST nodes

### Optimization Opportunities

1. **String Interning**: Reduce memory for repeated identifiers
2. **Arena Allocation**: Faster AST node allocation
3. **Lazy Symbol Resolution**: Delay expensive lookups
4. **Incremental Compilation**: Recompile only changed modules

## Testing Strategy

### Unit Tests

- **Lexer**: Token recognition, error handling
- **Parser**: Each grammar rule, error recovery
- **Semantic**: Symbol resolution, type checking
- **CodeGen**: Each translation rule

### Integration Tests

- **End-to-End**: .xxml → LLVM IR → native executable
- **Runtime Library**: Compile and run XXML standard library
- **Example Programs**: Test.XXML and others

### Test Coverage

Target: >90% code coverage across all modules

## Future Enhancements

### Optimization Pass

The LLVM backend can leverage LLVM's optimization passes:

```
AST → LLVM IR → LLVM Optimized IR → Native Code
```

Current optimizations via LLVM:
- Constant folding (also done at compile-time evaluation)
- Dead code elimination
- Inline expansion
- Common subexpression elimination

### Additional Features

- **Interpreter**: Direct AST interpretation for debugging
- **JIT Compiler**: Runtime compilation for dynamic scenarios
- **Debug Information**: DWARF debug info in generated binaries

### Language Server Protocol

Implement LSP for IDE integration:
- Syntax highlighting
- Auto-completion
- Go to definition
- Find references
- Refactoring

---

## See Also

- [Language Specification](LANGUAGE_SPEC.md) - Complete language syntax
- [Reflection System](REFLECTION_SYSTEM.md) - Runtime type introspection architecture
- [Threading](THREADING.md) - Concurrency implementation
- [Limitations](LIMITATIONS.md) - Known compiler limitations

---

**XXML Compiler Architecture v2.0**

> **Note**: The LLVM backend is the sole code generation target. All XXML programs compile to native executables via LLVM IR.
