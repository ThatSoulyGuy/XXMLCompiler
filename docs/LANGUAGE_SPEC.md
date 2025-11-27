# XXML Language Specification

Version 1.0

## Table of Contents

1. [Introduction](#introduction)
2. [Lexical Structure](#lexical-structure)
3. [Types and Ownership](#types-and-ownership)
4. [Declarations](#declarations)
5. [Statements](#statements)
6. [Expressions](#expressions)
7. [Lambdas and Function References](#lambdas-and-function-references)
8. [Standard Library](#standard-library)

## Introduction

XXML is a statically-typed, object-oriented programming language with explicit ownership semantics. It compiles to C++ for high performance while providing modern language features and memory safety.

### Design Goals

- **Memory Safety**: Explicit ownership prevents memory leaks and dangling pointers
- **Performance**: Compiles to efficient C++ code
- **Readability**: Clear, bracket-based syntax
- **Type Safety**: Strong static typing with compile-time checking

## Lexical Structure

### Comments

```xxml
// Single-line comment
```

### Keywords

```
import, Namespace, Class, Final, Extends, None, Public, Private, Protected,
Property, Types, Constructor, default, Method, Returns, Parameters, Parameter,
Entrypoint, Instantiate, As, Run, For, While, If, Else, Exit, Return,
Break, Continue, Lambda, true, false
```

### Identifiers

Identifiers can be:
- Regular: `[a-zA-Z_][a-zA-Z0-9_]*`
- Angle-bracketed: `<identifier>`
- Qualified: `Namespace::Class::Member`

### Literals

**Integer Literals:**
```xxml
42i      // Integer with suffix
123      // Integer without suffix
```

**String Literals:**
```xxml
"hello"
"hello\nworld"  // Escape sequences supported
```

**Boolean Literals:**
```xxml
true
false
```

### Operators

```
Arithmetic: + - * / %
Comparison: == != < > <= >=
Logical: && || !
Assignment: =
Member access: . ::
Reference: &
Range: ..
Arrow: ->
Ownership: ^ % &
```

## Types and Ownership

### Primitive Types

- `Integer` - 64-bit signed integer (wrapper around `int64`)
- `String` - UTF-8 string (heap-allocated)
- `Bool` - Boolean value (wrapper around `bool`)
- `Float` - 32-bit floating point
- `Double` - 64-bit floating point
- `None` - Void/null type

### Ownership Semantics

XXML uses explicit ownership modifiers to express memory and value semantics. Every type reference must include one of three ownership modifiers:

| Modifier | Symbol | Meaning |
|----------|--------|---------|
| Owned | `^` | Unique ownership - responsible for value lifetime |
| Reference | `&` | Borrowed reference - does not own the value |
| Copy | `%` | Value copy - creates an independent copy |

> **Implementation Note**: The compiler enforces move semantics for owned (`^`) captures and parameters. Use-after-move and double-move errors are detected at compile time.

#### Owned (`^`)

Represents **unique ownership**. The variable owns the value and is responsible for its lifetime.

```xxml
Property <data> Types String^;  // Owned string property
Instantiate String^ As <name> = String::Constructor("Alice");
```

**Semantics:**
- Indicates the variable "owns" the value
- Passing to an owned parameter or capturing with `^` transfers ownership
- The original variable **cannot** be used after transfer (compile-time error)
- Double-move (moving the same variable twice) is a compile-time error

**Usage:**
- Properties typically use owned types
- Return types for constructors and factory methods
- Local variables that own their data

#### Reference (`&`)

Represents a **borrowed reference**. The variable can access the value but does not own it.

```xxml
Parameter <str> Types String&;  // Borrowed reference parameter

Method <printMessage> Returns None Parameters (
    Parameter <msg> Types String&
) -> {
    Run Console::printLine(msg);
}
```

**Semantics:**
- The callee borrows the value without taking ownership
- The caller retains ownership and the value remains valid after the call
- At the LLVM level, passes the object pointer directly

**Usage:**
- Method parameters when you don't need ownership
- Efficient passing without copying
- Must not outlive the referenced value

#### Copy (`%`)

Represents a **copied value**. Creates an independent copy of the value.

```xxml
Parameter <value> Types Integer%;  // Copied integer parameter

Method <increment> Returns Integer^ Parameters (
    Parameter <n> Types Integer%
) -> {
    Return n.add(Integer::Constructor(1));
}
```

**Semantics:**
- Creates a copy of the value for the callee
- Modifications to the parameter don't affect the original
- The original value remains unchanged after the call

**Usage:**
- When you need an independent copy
- Parameters where modification shouldn't affect caller
- Small value types

### Ownership Compatibility Rules

The compiler enforces type compatibility based on ownership. The rules are:

| Parameter Type | Can Accept |
|---------------|------------|
| Owned (`^`) | Only owned (`^`) values |
| Reference (`&`) | Owned (`^`) or reference (`&`) values |
| Copy (`%`) | Any ownership (`^`, `&`, or `%`) |

**Compatibility Examples:**

```xxml
// Given these functions:
// takeOwned(Integer^)
// takeRef(Integer&)
// takeCopy(Integer%)

Instantiate Integer^ As <owned> = Integer::Constructor(42);

// Owned -> Owned: OK (ownership transfer)
Run takeOwned(owned);

// Owned -> Reference: OK (temporary borrow)
Run takeRef(owned);

// Owned -> Copy: OK (creates copy)
Run takeCopy(owned);
```

**Detailed Compatibility Matrix:**

| Actual \ Expected | Owned (`^`) | Reference (`&`) | Copy (`%`) |
|-------------------|-------------|-----------------|------------|
| Owned (`^`) | ✓ | ✓ (borrow) | ✓ (copy) |
| Reference (`&`) | ✗ | ✓ | ✓ (copy) |
| Copy (`%`) | ✗ | ✗ | ✓ |

### Ownership in Different Contexts

| Context | Common Ownership | Example |
|---------|-----------------|---------|
| Properties | Owned (`^`) | `Property <name> Types String^;` |
| Return types | Owned (`^`) | `Returns Integer^` |
| Parameters | Reference (`&`) or Copy (`%`) | `Parameter <x> Types Integer%` |
| Local variables | Owned (`^`) | `Instantiate String^ As <s> = ...` |
| Lambda captures | Any (`^`, `&`, `%`) | `[%copy, &ref, ^owned]` |
| Reference bindings | Reference (`&`) | `Instantiate Integer& As <ref> = existingVar;` |

### Lambda Capture Ownership

Lambda captures follow specific ownership semantics:

| Capture | Syntax | Storage | Access |
|---------|--------|---------|--------|
| Copy | `%var` | Stores value at capture time | Direct read |
| Owned | `^var` | Stores value (conceptual move) | Direct read |
| Reference | `&var` | Stores address of variable | Dereferences each access |

**Important Behaviors:**

1. **Copy capture (`%var`)**: Takes a snapshot of the value when the lambda is created. The lambda sees the captured value, not any subsequent changes.

2. **Owned capture (`^var`)**: Transfers ownership into the lambda. The original variable **cannot** be used after the capture (compile-time error). Multiple owned captures of the same variable are also compile-time errors.

3. **Reference capture (`&var`)**: Stores the address of the variable. Each access in the lambda body dereferences this address, seeing the current value. Multiple lambdas can reference-capture the same variable.

```xxml
Instantiate Integer^ As <x> = Integer::Constructor(10);

// Copy capture - sees 10 forever
Instantiate F(Integer^)()^ As <getCopy> = [ Lambda [%x] Returns Integer^ Parameters () {
    Return x;  // Always returns the captured value (10)
}];

// Reference capture - sees current value
Instantiate F(Integer^)()^ As <getRef> = [ Lambda [&x] Returns Integer^ Parameters () {
    Return x;  // Returns current value of x
}];
```

### Code Generation Details

At the LLVM IR level, ownership affects code generation as follows:

| Ownership | LLVM Representation | Notes |
|-----------|-------------------|-------|
| Owned (`^`) | `ptr` (heap pointer) | Object allocated via runtime |
| Reference (`&`) | `ptr` (same pointer) | Passed by pointer, no copy |
| Copy (`%`) | `ptr` (same pointer) | Currently same as reference at IR level |

All XXML objects are heap-allocated through the runtime library. The ownership modifier affects semantic validation but not the underlying pointer representation.

### Type Declarations

```xxml
// Property with type and ownership
Property <name> Types TypeName^;

// Method return type
Method <getName> Returns String^ Parameters () -> { }

// Parameter type
Parameter <value> Types Integer%

// Reference binding (alias to existing variable)
Instantiate Integer& As <ref> = existingVar;
```

## Declarations

### Import Statements

```xxml
#import Module::Name;
#import Language::Core;
```

### Namespace Declarations

```xxml
[ Namespace <Name>
    // Declarations
]

// Nested namespaces
[ Namespace <Outer::Inner>
    // Declarations
]
```

### Class Declarations

```xxml
[ Class <ClassName> Final Extends BaseClass
    [ Public <>
        // Public members
    ]
    [ Private <>
        // Private members
    ]
    [ Protected <>
        // Protected members
    ]
]
```

**Modifiers:**
- `Final` - Class cannot be inherited from
- `Extends BaseClass` - Inherits from BaseClass
- `Extends None` - No inheritance

### Property Declarations

```xxml
Property <propertyName> Types TypeName^;
Property <x> Types Integer^;
Property <name> Types String&;
```

### Constructor Declarations

**Default Constructor:**
```xxml
Constructor = default;
```

The default constructor initializes public properties from top to bottom using positional arguments.

**Custom Constructor:**
```xxml
Constructor Parameters (Parameter <value> Types Integer%) ->
{
    // Constructor body
}
```

### Method Declarations

```xxml
Method <methodName> Returns ReturnType Parameters (ParameterList) ->
{
    // Method body
}
```

**Example:**
```xxml
Method <add> Returns Integer^ Parameters (
    Parameter <a> Types Integer%,
    Parameter <b> Types Integer%
) ->
{
    // Implementation
}
```

### Entrypoint

The program entry point:

```xxml
[ Entrypoint
    {
        // Program starts here
        Exit(0);
    }
]
```

## Statements

### Instantiate Statement

Creates a new variable with initialization:

```xxml
Instantiate Type As <variableName> = initializer;

// Examples
Instantiate Integer As <x> = 42i;
Instantiate String As <name> = String::Constructor("Alice");
```

### Run Statement

Executes an expression (typically a method call):

```xxml
Run expression;

// Examples
Run System::Print(message);
Run object.method(args);
```

### For Loop

Range-based for loop:

```xxml
For (Type <variable> = start .. end) ->
{
    // Loop body
}

// Example
For (Integer <i> = 0 .. 10) ->
{
    Run System::Print(String::Convert(i));
}
```

The loop variable goes from `start` (inclusive) to `end` (exclusive).

### Exit Statement

Exits the program with a code:

```xxml
Exit(0);
Exit(exitCode);
```

### Return Statement

Returns from a method:

```xxml
Return value;
Return;  // For None return type
```

## Expressions

### Literals

```xxml
42i          // Integer
"string"     // String
true, false  // Boolean
```

### Identifiers

```xxml
variableName
className
```

### Member Access

```xxml
object.member
object.method()
```

### Qualified Names

```xxml
Namespace::Class
Class::StaticMethod
Namespace::Class::Member
```

### Method Calls

```xxml
function(arg1, arg2)
object.method(arg1, arg2)
Class::StaticMethod(args)
```

### Constructor Calls

```xxml
ClassName::Constructor(arguments)
String::Constructor("hello")
```

### Reference Operator

Pass a variable by reference:

```xxml
Run method(&variable);
```

### Binary Operations

```xxml
a + b    // Addition
a - b    // Subtraction
a * b    // Multiplication
a / b    // Division
a % b    // Modulo
a == b   // Equality
a != b   // Inequality
a < b    // Less than
a > b    // Greater than
a <= b   // Less or equal
a >= b   // Greater or equal
a && b   // Logical AND
a || b   // Logical OR
```

### Method Chaining

```xxml
object.method1().method2().method3();
string.Copy().Append(other);
```

## Lambdas and Function References

XXML supports lambda expressions and function reference types for first-class functions.

### Lambda Expression Syntax

```xxml
[ Lambda [captures] Returns ReturnType Parameters (parameters) {
    // body
}]
```

**Components:**
- `Lambda` keyword starts the lambda
- `[captures]` optional capture list (explicit captures only)
- `Returns ReturnType` specifies the return type with ownership
- `Parameters (...)` optional parameter list (same syntax as methods)
- `{ ... }` the lambda body

### Capture Semantics

Lambdas use explicit capture lists with **required ownership modifiers** that match XXML's ownership semantics. Each captured variable must specify one of:

| Modifier | Name | Closure Storage | Lambda Body Access |
|----------|------|-----------------|-------------------|
| `%var` | Copy | Stores value directly | Reads stored value |
| `^var` | Owned | Stores value directly | Reads stored value |
| `&var` | Reference | Stores address of variable | Dereferences to get current value |

#### Copy Capture (`%`)

The closure stores a **copy of the value** at the time the lambda is created. The lambda works with this snapshot.

```xxml
Instantiate Integer^ As <x> = Integer::Constructor(10);

Instantiate F(Integer^)()^ As <getCopy> = [ Lambda [%x] Returns Integer^ Parameters () {
    // x is the value that was captured when the lambda was created
    Return x;
}];

// Even if x could change, the lambda sees the original value (10)
Instantiate Integer^ As <result> = getCopy.call();  // Returns 10
```

#### Owned Capture (`^`)

The closure stores the value, and **ownership is conceptually transferred** to the lambda. The original variable should not be used after capture.

```xxml
Instantiate Integer^ As <data> = Integer::Constructor(42);

Instantiate F(Integer^)()^ As <consume> = [ Lambda [^data] Returns Integer^ Parameters () {
    // data's ownership moved into the lambda
    Return data;
}];

// data should not be used after this point (ownership transferred)
Instantiate Integer^ As <result> = consume.call();  // Returns 42
```

#### Reference Capture (`&`)

The closure stores the **address of the variable**. Each time the lambda accesses the captured variable, it dereferences this address to get the current value.

```xxml
Instantiate Integer^ As <counter> = Integer::Constructor(0);

Instantiate F(Integer^)()^ As <readCounter> = [ Lambda [&counter] Returns Integer^ Parameters () {
    // Reads counter through the stored reference
    Return counter;
}];

// The lambda sees the current value of counter
Instantiate Integer^ As <val> = readCounter.call();  // Returns current value
```

#### Multiple Captures

You can mix capture modes in a single lambda:

```xxml
Instantiate Integer^ As <a> = Integer::Constructor(5);
Instantiate Integer^ As <b> = Integer::Constructor(10);
Instantiate Integer^ As <ref> = Integer::Constructor(100);

Instantiate F(Integer^)()^ As <compute> = [ Lambda [%a, ^b, &ref] Returns Integer^ Parameters () {
    // a: copy of value at capture time
    // b: owned value (moved in)
    // ref: reference to original variable
    Instantiate Integer^ As <sum> = a.add(b);
    Return sum.add(ref);
}];
```

#### No Captures

Lambdas with empty capture lists access no external variables:

```xxml
Instantiate F(Integer^)(Integer&)^ As <double> = [ Lambda [] Returns Integer^ Parameters (
    Parameter <n> Types Integer&
) {
    Return n.multiply(Integer::Constructor(2));
}];
```

### Function Reference Types

Function reference types use the `F(...)` syntax:

```xxml
F(ReturnType)(ParamType1, ParamType2, ...)
```

**Example:**
```xxml
// Declare a function reference variable
Instantiate F(Integer^)(Integer&)^ As <doubler> = [ Lambda [%x] Returns Integer^ Parameters (
    Parameter <n> Types Integer&
) {
    Return n.add(x);
}];
```

### Calling Lambdas

Use the `.call()` method to invoke a lambda:

```xxml
Instantiate Integer^ As <result> = doubler.call(Integer::Constructor(10));
```

### Complete Lambda Example

```xxml
#import Language::Core;

[ Entrypoint
    {
        // Create a variable to capture
        Instantiate Integer^ As <multiplier> = Integer::Constructor(5);

        // Create a lambda that captures the multiplier by copy
        Instantiate F(Integer^)(Integer&)^ As <multiply> = [ Lambda [%multiplier] Returns Integer^ Parameters (
            Parameter <n> Types Integer&
        ) {
            Return n.multiply(multiplier);
        }];

        // Call the lambda
        Instantiate Integer^ As <result> = multiply.call(Integer::Constructor(3));

        // result is now 15
        Run Console::printLine(result.toString());

        Exit(0);
    }
]
```

### Multiple Parameters

Lambdas can accept multiple parameters:

```xxml
// Lambda with two parameters
Instantiate F(Integer^)(Integer&, Integer&)^ As <addThem> = [ Lambda [] Returns Integer^ Parameters (
    Parameter <x> Types Integer&,
    Parameter <y> Types Integer&
) {
    Return x.add(y);
}];

// Call with two arguments
Instantiate Integer^ As <sum> = addThem.call(Integer::Constructor(15), Integer::Constructor(7));
// sum is now 22
```

### Lambdas Returning None

Lambdas that perform side effects can return `None`:

```xxml
// Lambda that prints a message
Instantiate F(None)(String&)^ As <printer> = [ Lambda [] Returns None Parameters (
    Parameter <msg> Types String&
) {
    Run Console::printLine(msg);
}];

// Call with Run statement
Run printer.call(String::Constructor("Hello from lambda!"));
```

### Lambda Implementation Details

#### Closure Structure

Lambdas compile to closure structs with the following layout:

```
{ ptr (function_pointer), ptr (capture_0), ptr (capture_1), ... }
```

- **Slot 0**: Function pointer to the generated lambda function
- **Slots 1+**: Captured values or references

#### Capture Storage (at lambda creation)

| Mode | What's Stored | Code Generated |
|------|---------------|----------------|
| `%var` (Copy) | Value loaded from variable | `load ptr, ptr %var` then store |
| `^var` (Owned) | Value loaded from variable | `load ptr, ptr %var` then store |
| `&var` (Reference) | Address of variable's alloca | Store `%var` (the alloca ptr) directly |

#### Capture Access (in lambda body)

| Mode | How Value is Retrieved |
|------|----------------------|
| `%var` / `^var` | Single load: `load ptr, ptr %capture.var.ptr` |
| `&var` | Double load: First load gets alloca address, second load gets value |

#### Generated Lambda Function

Each lambda generates a function with signature:

```llvm
define ptr @lambda.N(ptr %closure, <param_types>...) {
    ; Load captures from closure struct
    ; Execute lambda body
    ; Return result
}
```

#### The `.call()` Method

Invoking a lambda via `.call()`:

1. Load the closure pointer
2. Extract the function pointer from slot 0
3. Perform indirect call: `call ptr %func_ptr(ptr %closure, <args>...)`

#### Type Preservation

Captured variables retain their original type information in the lambda body. This allows method calls like `capturedInteger.add(...)` to resolve correctly.

#### Ownership Semantics Summary

- **Copy (`%`)** and **Owned (`^`)** currently generate identical code (both store the value)
- The distinction is semantic: `^` indicates the original should not be used after capture
- **Reference (`&`)** stores the address, enabling access to the variable's current value

## Standard Library

### Language::Core

Base module with core functionality.

### Integer

```xxml
Method <Add> Returns Integer^ Parameters (Parameter <other> Types Integer%)
Method <Subtract> Returns Integer^ Parameters (Parameter <other> Types Integer%)
Method <Multiply> Returns Integer^ Parameters (Parameter <other> Types Integer%)
Method <Divide> Returns Integer^ Parameters (Parameter <other> Types Integer%)
Method <ToString> Returns String^ Parameters ()
```

### String

```xxml
Method <Copy> Returns String^ Parameters ()
Method <Append> Returns String^ Parameters (Parameter <other> Types String&)
Method <Length> Returns Integer^ Parameters ()
Method <CharAt> Returns String^ Parameters (Parameter <index> Types Integer%)
Method <Substring> Returns String^ Parameters (
    Parameter <start> Types Integer%,
    Parameter <end> Types Integer%
)
Method <Equals> Returns None Parameters (Parameter <other> Types String&)

// Static methods
String::Convert(Integer) -> String^
String::Constructor(String) -> String^
```

### System

```xxml
Method <Print> Returns None Parameters (Parameter <message> Types String&)
Method <PrintLine> Returns None Parameters (Parameter <message> Types String&)
Method <ReadLine> Returns String^ Parameters ()
Method <GetTime> Returns Integer^ Parameters ()
```

## Complete Example

```xxml
#import Language::Core;

[ Namespace <MyApp>
    [ Class <Calculator> Final Extends None
        [ Public <>
            Constructor = default;

            Method <add> Returns Integer^ Parameters (
                Parameter <a> Types Integer%,
                Parameter <b> Types Integer%
            ) ->
            {
                Return a + b;
            }
        ]
    ]
]

[ Entrypoint
    {
        Instantiate MyApp::Calculator As <calc> = MyApp::Calculator::Constructor();

        Instantiate Integer As <result> = calc.add(5i, 3i);

        Run System::Print(String::Convert(result));

        Exit(0);
    }
]
```

## Grammar (EBNF)

```ebnf
program ::= declaration*

declaration ::= import_decl | namespace_decl | class_decl | entrypoint_decl

import_decl ::= "#import" qualified_id ";"

namespace_decl ::= "[" "Namespace" "<" qualified_id ">" declaration* "]"

class_decl ::= "[" "Class" "<" id ">" "Final"? "Extends" (id | "None") access_section* "]"

access_section ::= "[" access_modifier "<>" member_decl* "]"

access_modifier ::= "Public" | "Private" | "Protected"

member_decl ::= property_decl | constructor_decl | method_decl

property_decl ::= "Property" "<" id ">" "Types" type_ref ";"

constructor_decl ::= "Constructor" ("=" "default" | parameters? "->" block) ";"

method_decl ::= "Method" "<" id ">" "Returns" type_ref parameters? "->" block

parameters ::= "Parameters" "(" (parameter ("," parameter)*)? ")"

parameter ::= "Parameter" "<" id ">" "Types" type_ref

entrypoint_decl ::= "[" "Entrypoint" block "]"

type_ref ::= qualified_id ownership_modifier

ownership_modifier ::= "^" | "&" | "%"

statement ::= instantiate_stmt | run_stmt | for_stmt | exit_stmt | return_stmt

instantiate_stmt ::= "Instantiate" type_ref "As" "<" id ">" "=" expression ";"

run_stmt ::= "Run" expression ";"

for_stmt ::= "For" "(" type_ref "<" id ">" "=" expression ".." expression ")" "->" block

exit_stmt ::= "Exit" "(" expression ")" ";"

return_stmt ::= "Return" expression? ";"

block ::= "{" statement* "}"

expression ::= /* standard expression grammar with precedence */

qualified_id ::= id ("::" id)*

id ::= identifier | "<" identifier ">"
```

---

**XXML Language Specification v1.0**
