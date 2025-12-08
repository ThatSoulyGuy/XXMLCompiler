# XXML Syntax Reference

Core syntax reference for the XXML language.

---

## Table of Contents

1. [Comments](#comments)
2. [Keywords](#keywords)
3. [Identifiers](#identifiers)
4. [Literals](#literals)
5. [Operators](#operators)
6. [Statements](#statements)
7. [Expressions](#expressions)
8. [Grammar](#grammar)

---

## Comments

```xxml
// Single-line comment
```

Multi-line comments are not currently supported.

---

## Keywords

```
import, Namespace, Class, Structure, Enumeration, Value, Final, Extends, None,
Public, Private, Protected, Property, Types, Constructor, Destructor, default,
Method, Returns, Parameters, Parameter, Entrypoint, Instantiate, As, Run, Set,
For, While, If, Else, Exit, Return, Break, Continue, Lambda, true, false,
Compiletime, Templates, Constrains, NativeType, NativeStructure, CallbackType
```

---

## Identifiers

Identifiers can take several forms:

| Form | Pattern | Example |
|------|---------|---------|
| Regular | `[a-zA-Z_][a-zA-Z0-9_]*` | `myVariable` |
| Angle-bracketed | `<identifier>` | `<name>` |
| Qualified | `Namespace::Class::Member` | `Collections::List::Constructor` |

---

## Literals

### Integer Literals

```xxml
42       // Integer without suffix
42i      // Integer with suffix
-10      // Negative integer
```

### String Literals

```xxml
"hello"
"hello\nworld"    // With newline
"tab\there"       // With tab
"quote: \""       // Escaped quote
```

### Boolean Literals

```xxml
true
false
```

---

## Operators

### Arithmetic Operators

| Operator | Description |
|----------|-------------|
| `+` | Addition |
| `-` | Subtraction |
| `*` | Multiplication |
| `/` | Division |
| `%` | Modulo |

### Comparison Operators

| Operator | Description |
|----------|-------------|
| `==` | Equality |
| `!=` | Inequality |
| `<` | Less than |
| `>` | Greater than |
| `<=` | Less or equal |
| `>=` | Greater or equal |

### Logical Operators

| Operator | Description |
|----------|-------------|
| `&&` | Logical AND |
| `\|\|` | Logical OR |
| `!` | Logical NOT |

### Other Operators

| Operator | Description |
|----------|-------------|
| `=` | Assignment |
| `.` | Member access |
| `::` | Namespace/static access |
| `&` | Reference operator |
| `..` | Range |
| `->` | Block arrow |
| `^` `%` `&` | Ownership modifiers |

---

## Statements

### Instantiate Statement

Creates a new variable with initialization.

```xxml
Instantiate Type^ As <variableName> = initializer;

// Examples
Instantiate Integer^ As <x> = Integer::Constructor(42);
Instantiate String^ As <name> = String::Constructor("Alice");

// Reference binding (alias to existing variable)
Instantiate Integer& As <ref> = existingVar;
```

### Set Statement

Assigns a value to an existing variable.

```xxml
Set variableName = expression;

// Examples
Set counter = counter.add(Integer::Constructor(1));
Set name = String::Constructor("Updated");
```

### Run Statement

Executes an expression (typically a method call).

```xxml
Run expression;

// Examples
Run Console::printLine(message);
Run object.method(args);
```

### If/Else Statement

Conditional execution.

```xxml
If (condition) -> {
    // Then branch
}

If (condition) -> {
    // Then branch
} Else -> {
    // Else branch
}

// Example
If (age.greaterThan(Integer::Constructor(18))) -> {
    Run Console::printLine(String::Constructor("Adult"));
} Else -> {
    Run Console::printLine(String::Constructor("Minor"));
}
```

### For Loop

Range-based for loop. The loop variable goes from `start` (inclusive) to `end` (exclusive).

```xxml
For (Type <variable> = start .. end) -> {
    // Loop body
}

// Example (prints 0 to 9)
For (Integer <i> = 0 .. 10) -> {
    Run Console::printLine(i.toString());
}
```

### While Loop

Executes a block while a condition is true.

```xxml
While (condition) -> {
    // Loop body
}

// Example
While (counter.lessThan(Integer::Constructor(10))) -> {
    Run Console::printLine(counter.toString());
    Set counter = counter.add(Integer::Constructor(1));
}
```

### Break Statement

Exits the innermost loop.

```xxml
While (Bool::Constructor(true)) -> {
    If (shouldExit) -> {
        Break;
    }
}
```

### Continue Statement

Skips to the next iteration of the innermost loop.

```xxml
For (Integer <i> = 0 .. 10) -> {
    If (i.equals(Integer::Constructor(5))) -> {
        Continue;  // Skip 5
    }
    Run Console::printLine(i.toString());
}
```

### Return Statement

Returns from a method.

```xxml
Return value;
Return;  // For None return type
```

### Exit Statement

Exits the program with a status code.

```xxml
Exit(0);
Exit(exitCode);
```

---

## Expressions

### Literals

```xxml
42           // Integer
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
Integer::Constructor(42)
```

### Reference Operator

Pass a variable by reference:

```xxml
Run method(&variable);
```

### Method Chaining

```xxml
object.method1().method2().method3();
string.copy().append(other);
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

---

## Grammar

### EBNF Grammar

```ebnf
program ::= declaration*

declaration ::= import_decl | namespace_decl | class_decl | structure_decl
              | enum_decl | entrypoint_decl

import_decl ::= "#import" qualified_id ";"

namespace_decl ::= "[" "Namespace" "<" qualified_id ">" declaration* "]"

enum_decl ::= "[" "Enumeration" "<" id ">" enum_value* "]"

enum_value ::= "Value" "<" id ">" ("=" integer_literal)? ";"

class_decl ::= "[" "Class" "<" id ">" template_params? "Final"? "Extends" (id | "None")
               constraint_clause? access_section* "]"

structure_decl ::= "[" "Structure" "<" id ">" template_params?
                   constraint_clause? access_section* "]"

template_params ::= "Templates" "(" template_param ("," template_param)* ")"

template_param ::= "T" "<" id ">" | "V" "<" id ">" ":" type_ref

constraint_clause ::= "Constrains" constraint_list

access_section ::= "[" access_modifier "<>" member_decl* "]"

access_modifier ::= "Public" | "Private" | "Protected"

member_decl ::= property_decl | constructor_decl | destructor_decl | method_decl

property_decl ::= "Property" "<" id ">" "Types" type_ref ";"

constructor_decl ::= "Constructor" ("=" "default" | parameters? "->" block) ";"

destructor_decl ::= "Destructor" parameters? "->" block ";"

method_decl ::= "Method" "<" id ">" template_params? "Returns" type_ref
                parameters? constraint_clause? ("->" | "Do") block

parameters ::= "Parameters" "(" (parameter ("," parameter)*)? ")"

parameter ::= "Parameter" "<" id ">" "Types" type_ref

entrypoint_decl ::= "[" "Entrypoint" block "]"

type_ref ::= qualified_id ownership_modifier | "NativeType" "<" string ">" ownership_modifier

ownership_modifier ::= "^" | "&" | "%"

statement ::= instantiate_stmt | set_stmt | run_stmt | for_stmt | while_stmt
            | if_stmt | return_stmt | exit_stmt | break_stmt | continue_stmt

instantiate_stmt ::= "Instantiate" type_ref "As" "<" id ">" "=" expression ";"

set_stmt ::= "Set" id "=" expression ";"

run_stmt ::= "Run" expression ";"

for_stmt ::= "For" "(" type_ref "<" id ">" "=" expression ".." expression ")" "->" block

while_stmt ::= "While" "(" expression ")" "->" block

if_stmt ::= "If" "(" expression ")" "->" block ("Else" "->" block)?

return_stmt ::= "Return" expression? ";"

exit_stmt ::= "Exit" "(" expression ")" ";"

break_stmt ::= "Break" ";"

continue_stmt ::= "Continue" ";"

block ::= "{" statement* "}"

expression ::= /* standard expression grammar with precedence */

qualified_id ::= id ("::" id)*

id ::= identifier | "<" identifier ">"
```

---

## See Also

- [Ownership](OWNERSHIP.md) - Memory ownership semantics
- [Classes](CLASSES.md) - Class declarations
- [Structures](STRUCTURES.md) - Value types
- [Lambdas](LAMBDAS.md) - Lambda expressions

