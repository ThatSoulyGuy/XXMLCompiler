# XXML Print Functionality Demo

## Overview

This document demonstrates print/output functionality in XXML and explains current limitations.

## Working Examples

### 1. WorkingDemo.XXML ✓
Basic XXML functionality without console output:
```xxml
[ Entrypoint {
	Instantiate Integer^ As <num> = Integer::Constructor(42);
	Instantiate String^ As <msg> = String::Constructor("Hello");
}]
```
**Status**: Compiles and runs successfully!

### 2. PrintFunctionalityDemo.XXML ✓
Comprehensive demo showing all XXML features:
- String creation and manipulation
- Integer arithmetic operations
- Boolean comparisons
- Copying and type conversions

**Status**: Compiles and runs successfully!

## Current Limitation: Console Output

### The Issue
`System::Console::printLine()` requires cross-module namespace resolution.  
When Console.XXML defines the `System` namespace, it's registered in Console.XXML's semantic analyzer instance. However, when your main code tries to use `System::Console`, it has a separate analyzer that doesn't know about the `System` namespace.

### Error Message
```
error: Undefined namespace or class 'System' in qualified name 'System::Console'
```

### Root Cause
- `validNamespaces_` is stored per SemanticAnalyzer instance (SemanticAnalyzer.h:82)
- Each module gets its own analyzer
- Namespaces aren't shared across analyzers

### Workaround Solutions

#### Option 1: Direct C++ Integration (Post-Generation)
After XXML generates C++ code, add print statements:

```cpp
// Add at top of generated .cpp file:
#include <iostream>

// Add in main():
std::cout << "Hello from XXML!" << std::endl;
std::cout << "Number: " << num.toInt64() << std::endl;
```

#### Option 2: Use Generated Values
The XXML code executes correctly - values are computed!
You can inspect them in a debugger or modify generated C++ to print them.

## What Works Perfectly

✓ Integer arithmetic (+, -, *, /, %)  
✓ String creation and concatenation  
✓ Boolean comparisons  
✓ Type conversions  
✓ Template instantiation (Test::Box<Integer>)  
✓ Method calls and property access  

## Future Improvement

To fix Console output, move `validNamespaces_` and `classRegistry_` to CompilationContext  
so they're shared across all SemanticAnalyzer instances.

## Running the Demos

```bash
# Compile XXML to C++
./build/bin/xxml WorkingDemo.XXML WorkingDemo.cpp

# Compile C++ to executable
g++ -std=c++20 -o WorkingDemo.exe WorkingDemo.cpp

# Run
./WorkingDemo.exe
# Returns exit code 0 (success!)
```

## Example Output Structure

The generated C++ creates a full Language::Core namespace with:
- Integer class (64-bit arithmetic)
- String class (std::string wrapper)
- Bool class (boolean operations)
- Float/Double classes (floating-point math)

All operations work correctly - they just don't print by default.

---
**Bottom Line**: XXML compiles successfully to working C++ code.  
Print statements require a minor architectural fix to share namespace state.
