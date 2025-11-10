# XXML Compiler Architecture

## Overview

The XXML compiler is a multi-stage transpiler that converts XXML source code to C++. It follows a traditional compiler architecture with distinct phases.

## Compilation Pipeline

```
Source Code (.xxml)
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
[Code Generator] ← Error Reporter
       ↓
  C++ Code (.cpp)
       ↓
[C++ Compiler]
       ↓
  Executable
```

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

## Phase 4: Code Generation

**Location:** `include/CodeGen/`, `src/CodeGen/`

### Components

- **CodeGenerator** - AST visitor generating C++ code

### Translation Rules

**Namespaces:**
```xxml
[ Namespace <Foo::Bar> ... ]
```
↓
```cpp
namespace Foo::Bar { ... }
```

**Classes:**
```xxml
[ Class <MyClass> Final Extends None
    [ Public <> ... ]
    [ Private <> ... ]
]
```
↓
```cpp
class MyClass {
public:
    ...
private:
    ...
};
```

**Ownership Types:**
```xxml
Property <data> Types String^;  // Owned
Property <ref> Types String&;   // Reference
Property <val> Types Integer%;  // Copy
```
↓
```cpp
std::unique_ptr<std::string> data;  // Owned
std::string& ref;                   // Reference
int64_t val;                        // Copy
```

**Methods:**
```xxml
Method <foo> Returns Integer^ Parameters (Parameter <x> Types Integer%) ->
{
    Return x + 1;
}
```
↓
```cpp
std::unique_ptr<int64_t> foo(int64_t x) {
    return x + 1;
}
```

**For Loops:**
```xxml
For (Integer <i> = 0 .. 10) -> { ... }
```
↓
```cpp
for (int64_t i = 0; i < 10; i++) { ... }
```

**Entrypoint:**
```xxml
[ Entrypoint { Exit(0); } ]
```
↓
```cpp
int main() { return 0; }
```

### Code Generation Strategy

1. **Top-Down Traversal**: Visit AST nodes in declaration order
2. **Indentation Tracking**: Maintain proper C++ indentation
3. **Type Conversion**: Map XXML types to C++ types
4. **Comment Generation**: Add comments for generated code

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
5. **Generate** → C++ Source String
6. **Write** → .cpp File

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

Different code generation strategies could be swapped:
- C++ Generator (current)
- LLVM IR Generator (future)
- Interpreter (future)

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

- **End-to-End**: .xxml → .cpp → executable
- **Runtime Library**: Compile and run XXML standard library
- **Example Programs**: Test.XXML and others

### Test Coverage

Target: >90% code coverage across all modules

## Future Enhancements

### Optimization Pass

Add IR optimization between semantic analysis and code generation:

```
AST → IR → Optimized IR → C++
```

Potential optimizations:
- Constant folding
- Dead code elimination
- Inline expansion
- Common subexpression elimination

### Alternative Backends

- **LLVM Backend**: Generate LLVM IR for better optimizations
- **Interpreter**: Direct AST interpretation for debugging
- **JIT Compiler**: Runtime compilation for dynamic scenarios

### Language Server Protocol

Implement LSP for IDE integration:
- Syntax highlighting
- Auto-completion
- Go to definition
- Find references
- Refactoring

---

**XXML Compiler Architecture v1.0**
