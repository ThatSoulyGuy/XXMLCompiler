# Changelog

All notable changes to the XXML Compiler project will be documented in this file.

## [3.0.0] - 2025-12-08 - XXML 1.0 Stable Release

### Overview

This release marks the completion of all seven phases of the XXML roadmap, delivering the first stable release of the language with explicit ownership semantics, rich reflection capabilities, and compile-time metaprogramming.

### Added

#### Phase 6: Flagship Demos
- **JSON Library** (`Language/Format/JSON.XXML`): Complete JSON serialization/parsing with `@Derive(trait="JSON")` support
- **Test Framework** (`Language/Test/TestFramework.XXML`): Reflection-based test discovery and execution with `Assert` utilities
- **ORM Demo** (`demos/ORM/SimpleORM.XXML`): SQL generation via reflection using `QueryBuilder` and `Repository` patterns
- **RPC Framework** (`demos/RPC/SimpleRPC.XXML`): Client/server stub generation using reflection
- **Plugin System** (`demos/Plugin/PluginSystem.XXML`): Runtime plugin discovery, lifecycle management, and introspection

#### Phase 7: API Improvements
- `String.concat()`: New non-mutating string concatenation (returns new String)
- `String.startsWith()`: Check if string starts with a prefix
- `Float.lessOrEqual()`, `Float.greaterOrEqual()`: Added missing comparison operators
- `xxml_string_startsWith()`: New runtime function for string prefix checking

### Changed

#### API Cleanup
- Standardized value accessors: Removed duplicate `getValue()` methods from `Bool` and `Double`
  - Use `Bool.toBool()` instead of `Bool.getValue()`
  - Use `Double.toDouble()` instead of `Double.getValue()`

### Deprecated

- `AnnotationCodegen.getIR()`: Use Module-based emission via LLVMEmitter instead
- `ReflectionCodegen.getIR()`: Use Module-based emission via LLVMEmitter instead (now marked with `[[deprecated]]` attribute)

### Documentation

- Updated `docs/ROADMAP.md` to mark all phases complete
- All version milestones (0.3.0 through 1.0.0) now marked as complete

---

## [2.3.0] - 2025-12-07

### Added

#### Full Compile-Time Constant Folding
- **Method Call Folding**: Compile-time method calls like `x.add(y)` are now fully evaluated at compile-time and folded to constants
- **User-Defined Compiletime Classes**: User-defined classes marked with `Compiletime` now have full compile-time evaluation support:
  - Constructors execute at compile-time, setting properties
  - Methods execute at compile-time, returning folded values
  - Entire method chains like `p.getX().toString()` fold to string constants

```xxml
[ Class <Point> Compiletime Final Extends None
    [ Private <>
        Property <x> Types Integer^;
        Property <y> Types Integer^;
    ]
    [ Public <>
        Constructor Parameters (Parameter <px> Types Integer^, Parameter <py> Types Integer^) -> {
            Set x = px;
            Set y = py;
        }
        Method <getX> Returns Integer^ Parameters () -> { Return x; }
    ]
]

// p.getX().toString() is evaluated at compile-time to "10"
Instantiate Compiletime Point^ As <p> = Point::Constructor(Integer::Constructor(10), Integer::Constructor(20));
```

#### CompiletimeInterpreter Enhancements
- `executeMethod()`: Execute user-defined methods on CompiletimeObject at compile-time
- `findConstructor(classDecl, argCount)`: Find constructor matching parameter count
- `evalMethodCall()`: Extended to handle CompiletimeObject method dispatch
- `evalIdentifier()`: Extended to lookup properties on `this` object

### Documentation
- Updated `docs/COMPILETIME.md` with:
  - User-defined compile-time class documentation and examples
  - Method call folding documentation with LLVM IR examples
  - Updated limitations section
  - Supported statement types in compile-time methods

## [2.2.0] - 2025-12-02

### Added

#### Lambda Templates
- **Generic Lambda Functions**: Lambdas can now have their own type parameters using the `Templates` keyword
- **Monomorphization**: Lambda templates are instantiated on-demand with concrete type arguments
- **Two Call Syntaxes**: Both `lambda<Type>.call()` and `lambda@Type.call()` syntaxes are supported

```xxml
// Define a generic identity lambda
Instantiate __function^ As <identity> = [ Lambda [] Templates <T Constrains None> Returns T^ Parameters (Parameter <x> Types T^)
{
    Return x;
} ];

// Call with different types
Instantiate Integer^ As <r1> = identity<Integer>.call(intVal);
Instantiate String^ As <r2> = identity<String>.call(strVal);
```

#### LLVM Backend Changes
- `generateLambdaTemplateInstantiations()`: New function for generating monomorphized lambda template code
- Template lambda definition skipping in `visit(LambdaExpr&)`: Template lambdas are not compiled until instantiated
- Template lambda call handling in `visit(CallExpr&)`: Detects `lambda<Type>.call()` pattern and generates correct function calls
- Name mangling for lambda templates: `identity<Integer>` becomes `@lambda.template.identity_LT_Integer_GT_`

#### Parser Improvements
- Support for `@Type` syntax after identifiers (parsed in `parsePrimary()`)
- `AngleBracketId` token handling for template arguments in expressions

#### Semantic Analyzer Additions
- `TemplateLambdaInfo` structure for tracking template lambda definitions
- `LambdaTemplateInstantiation` structure for tracking instantiations
- `recordLambdaTemplateInstantiation()` function
- `getTemplateLambdas()` and `getLambdaTemplateInstantiations()` accessors

### Documentation
- Added comprehensive "Lambda Templates" section to `docs/TEMPLATES.md`
- Added Lambda Templates section to README.md

## [2.1.0] - 2025-11-26

### Added

#### LLVM Backend Improvements
- **Bool Constructor Wrapping**: Comparison operations (`==`, `!=`, `<`, `>`, `<=`, `>=`) now properly wrap `i1` results with `Bool_Constructor` when storing to Bool variables
- **Type tracking for comparisons**: CallExpr visitor now correctly sets `registerTypes_` to `"NativeType<\"bool\">"` for comparison methods

#### Runtime Library Additions
- `Bool_xor(ptr, ptr)` - XOR operation for Bool type
- `Bool_toInteger(ptr)` - Convert Bool to Integer (0 or 1)
- `String_isEmpty(ptr)` - Check if string is empty
- `Integer_negate(ptr)` - Negate an Integer
- `Integer_mod(ptr, ptr)` - Modulo operation for Integer

#### Function Name Mappings
- `Integer_subtract` -> `Integer_sub`
- `Integer_multiply` -> `Integer_mul`
- `Integer_divide` -> `Integer_div`
- `Integer_lessOrEqual` -> `Integer_le`
- `Integer_greaterOrEqual` -> `Integer_ge`

### Fixed
- **Type Mismatch Bug**: Fixed critical bug where comparison operations returning `i1` were incorrectly stored as `ptr` to Bool variables, causing linker errors
- Updated HelloWorld example to use correct API (`System::Console::printLine` instead of `System::PrintLine`)

### Changed
- LLVM IR preamble now includes all new runtime function declarations

## [2.0.0] - Previous Release

### Features
- Complete LLVM IR code generation backend
- Native executable generation via Clang
- Full template instantiation (monomorphization)
- Reflection system with runtime type introspection
- Ownership semantics (`^`, `&`, `%`)
- Self-hosting standard library

### Architecture
- Lexer -> Parser -> Semantic Analyzer -> LLVM Backend pipeline
- Platform-specific linkers (MSVC, GNU)
- C runtime library for LLVM IR execution
