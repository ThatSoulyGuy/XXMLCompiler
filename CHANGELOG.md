# Changelog

All notable changes to the XXML Compiler project will be documented in this file.

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
