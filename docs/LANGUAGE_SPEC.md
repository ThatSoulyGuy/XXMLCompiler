# XXML Language Specification

Version 1.0

## Table of Contents

1. [Introduction](#introduction)
2. [Lexical Structure](#lexical-structure)
3. [Types and Ownership](#types-and-ownership)
4. [Declarations](#declarations)
5. [Statements](#statements)
6. [Expressions](#expressions)
7. [Standard Library](#standard-library)

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
Break, Continue, true, false
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

- `Integer` - 64-bit signed integer
- `String` - UTF-8 string
- `Bool` - Boolean value
- `None` - Void/null type

### Ownership Semantics

XXML uses three ownership modifiers:

#### Owned (`^`)

Represents unique ownership. The variable owns the value and is responsible for cleanup.

```xxml
Property <data> Types String^;  // Owned string
```

Transpiles to: `std::unique_ptr<std::string>`

#### Reference (`&`)

Represents a borrowed reference. Does not own the value.

```xxml
Parameter <str> Types String&;  // Borrowed reference
```

Transpiles to: `std::string&`

#### Copy (`%`)

Represents a copied value. Creates a new independent copy.

```xxml
Parameter <value> Types Integer%;  // Copied integer
```

Transpiles to: `int64_t`

### Type Declarations

```xxml
// Property with type and ownership
Property <name> Types TypeName^;

// Method return type
Method <getName> Returns String^ Parameters () -> { }

// Parameter type
Parameter <value> Types Integer%
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
