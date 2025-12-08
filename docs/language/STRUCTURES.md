# XXML Structures

Version 2.0

## Table of Contents

1. [Overview](#overview)
2. [Syntax](#syntax)
3. [Structure vs Class](#structure-vs-class)
4. [Stack Allocation](#stack-allocation)
5. [Properties](#properties)
6. [Constructors and Destructors](#constructors-and-destructors)
7. [Methods](#methods)
8. [Template Structures](#template-structures)
9. [Limitations](#limitations)
10. [Best Practices](#best-practices)
11. [See Also](#see-also)

## Overview

Structures are **value types** in XXML that are allocated on the stack rather than the heap. They provide efficient, lightweight data containers with automatic RAII cleanup when they go out of scope.

### Key Features

- **Stack Allocation**: Memory allocated with `alloca`, not `malloc`
- **Value Semantics**: Properties store values directly (not pointers)
- **RAII Cleanup**: Automatic destructor call when leaving scope
- **No Inheritance**: Structures cannot extend other types
- **Template Support**: Full template parameter support

### When to Use Structures

Use structures for:
- Small, short-lived data aggregates
- Performance-critical code (avoid heap allocation overhead)
- Simple data containers without inheritance needs
- Interop with C/C++ value types

## Syntax

### Basic Structure

```xxml
[ Structure <StructureName>
    [ Public <>
        Property <field1> Types Integer^;
        Property <field2> Types String^;

        Constructor Parameters (Parameter <f1> Types Integer%, Parameter <f2> Types String%) -> {
            Set field1 = f1;
            Set field2 = f2;
        }

        Destructor Parameters () -> {
            // Cleanup code (optional)
        }

        Method <someMethod> Returns Integer^ Parameters () -> {
            Return field1;
        }
    ]
]
```

### Structure with Compiletime

```xxml
[ Structure <Config> Compiletime
    [ Public <>
        Property <value> Types Integer^;

        Constructor Parameters (Parameter <v> Types Integer%) -> {
            Set value = v;
        }
    ]
]
```

### Instantiation

```xxml
// Stack-allocated structure instance
Instantiate Point As <p> = Point::Constructor(Integer::Constructor(10), Integer::Constructor(20));

// Access properties
Run Console::printLine(p.x.toString());
```

## Structure vs Class

| Feature | Class | Structure |
|---------|-------|-----------|
| **Storage** | Heap (malloc) | Stack (alloca) |
| **Inheritance** | Supported (`Extends`) | Not supported |
| **Properties** | Store pointers to objects | Store values directly |
| **Default Passing** | By reference | By value (copy) |
| **RAII Cleanup** | Yes | Yes |
| **Templates** | Supported | Supported |
| **Compiletime** | Supported | Supported |
| **Access Modifiers** | Public/Private/Protected | Public/Private/Protected |
| **Methods** | Supported | Supported |
| **Constructors** | Supported | Supported |
| **Destructors** | Supported | Supported |

### Memory Model Comparison

**Class (Heap Allocation):**
```
Variable: [ptr] ──────> Heap: [vtable_ptr][field1_ptr][field2_ptr]...
                                   │            │
                                   v            v
                              [vtable]    [field value]
```

**Structure (Stack Allocation):**
```
Stack Frame: [field1_value][field2_value]...
              └──── embedded directly ────┘
```

## Stack Allocation

Structures are allocated on the stack using LLVM's `alloca` instruction. This provides:

- **No Heap Overhead**: No malloc/free calls
- **Automatic Cleanup**: Memory reclaimed when function returns
- **Cache Locality**: Data stored contiguously on the stack

### Example

```xxml
[ Structure <Vec2>
    [ Public <>
        Property <x> Types Double^;
        Property <y> Types Double^;

        Constructor Parameters (Parameter <px> Types Double%, Parameter <py> Types Double%) -> {
            Set x = px;
            Set y = py;
        }

        Method <length> Returns Double^ Parameters () -> {
            // Calculate length
            Return x.multiply(x).add(y.multiply(y)).sqrt();
        }
    ]
]

[ Entrypoint <>
    // v is allocated on the stack, not the heap
    Instantiate Vec2 As <v> = Vec2::Constructor(Double::Constructor(3.0), Double::Constructor(4.0));

    // v.length() returns 5.0
    Run Console::printLine(v.length().toString());

    // v is automatically cleaned up when the entrypoint exits
]
```

## Properties

Structure properties store values directly (embedded), not pointers to heap-allocated objects.

### Value Embedding

For structures containing other value types:
```xxml
[ Structure <Rectangle>
    [ Public <>
        Property <topLeft> Types Point^;      // Point embedded directly
        Property <bottomRight> Types Point^;  // Not a pointer
    ]
]
```

### Reference Type Properties

When a structure contains a reference type (class), the property stores a pointer:
```xxml
[ Structure <Container>
    [ Public <>
        Property <name> Types String^;    // String^ is a reference type
        Property <count> Types Integer^;  // Integer^ wraps int64
    ]
]
```

## Constructors and Destructors

### Constructor

Constructors initialize structure fields. The syntax is identical to class constructors:

```xxml
Constructor Parameters (Parameter <arg1> Types T1%, Parameter <arg2> Types T2%) -> {
    Set field1 = arg1;
    Set field2 = arg2;
}
```

### Destructor

Destructors clean up resources when the structure goes out of scope:

```xxml
Destructor Parameters () -> {
    // Free resources, close handles, etc.
}
```

> **Note**: The destructor is automatically called by RAII when the structure variable leaves scope.

### Default Initialization

All properties are initialized to default values (zero/null) before constructor code runs.

## Methods

Structures support methods with the same syntax as classes:

```xxml
[ Structure <Counter>
    [ Public <>
        Property <value> Types Integer^;

        Constructor Parameters () -> {
            Set value = Integer::Constructor(0);
        }

        Method <increment> Returns None Parameters () -> {
            Set value = value.add(Integer::Constructor(1));
        }

        Method <getValue> Returns Integer^ Parameters () -> {
            Return value;
        }
    ]
]
```

## Template Structures

Structures support template parameters just like classes:

```xxml
[ Structure <Pair> <T Constrains None, U Constrains None>
    [ Public <>
        Property <first> Types T^;
        Property <second> Types U^;

        Constructor Parameters (Parameter <f> Types T%, Parameter <s> Types U%) -> {
            Set first = f;
            Set second = s;
        }
    ]
]

// Usage
Instantiate Pair<Integer, String> As <p> = Pair<Integer, String>::Constructor(
    Integer::Constructor(42),
    String::Constructor("hello")
);
```

### With Constraints

```xxml
[ Structure <SortedPair> <T Constrains Comparable>
    [ Public <>
        Property <min> Types T^;
        Property <max> Types T^;

        Constructor Parameters (Parameter <a> Types T%, Parameter <b> Types T%) -> {
            If (a.lessThan(b)) {
                Set min = a;
                Set max = b;
            } Else {
                Set min = b;
                Set max = a;
            }
        }
    ]
]
```

## Limitations

1. **No Inheritance**: Structures cannot use `Extends` to inherit from other types
2. **No Polymorphism**: No virtual methods or dynamic dispatch
3. **Stack Size**: Large structures may cause stack overflow; use classes for large objects
4. **No `Final` Modifier**: The `Final` keyword is not needed (structures are implicitly final)

### What Structures Cannot Do

```xxml
// ERROR: Structures cannot inherit
[ Structure <Derived> Extends Base  // Not allowed!
    ...
]

// ERROR: Structures cannot be extended
[ Class <Child> Extends MyStructure  // Not allowed!
    ...
]
```

## Best Practices

### Do

- Use structures for small, simple data aggregates
- Prefer structures for performance-critical inner loops
- Use structures for interop with C/C++ value types
- Keep structures small (< 64 bytes recommended)

### Don't

- Don't use structures for objects requiring inheritance
- Don't use structures for large objects (use classes instead)
- Don't store structures in collections that expect reference types

### Example: Good Structure Usage

```xxml
// Good: Small coordinate pair
[ Structure <Point>
    [ Public <>
        Property <x> Types Integer^;
        Property <y> Types Integer^;
        Constructor Parameters (...) -> { ... }
    ]
]

// Good: RGB color value
[ Structure <Color>
    [ Public <>
        Property <r> Types Integer^;
        Property <g> Types Integer^;
        Property <b> Types Integer^;
    ]
]
```

### Example: Use Class Instead

```xxml
// Use Class: Needs inheritance
[ Class <Shape> Final Extends None
    [ Public <>
        Method <area> Returns Double^ Parameters () -> { ... }
    ]
]

// Use Class: Large data structure
[ Class <LargeBuffer> Final Extends None
    [ Private <>
        Property <data> Types Array<Integer>^;  // Large array
    ]
]
```

## See Also

- [Language Specification](LANGUAGE_SPEC.md) - Core language syntax
- [Templates](TEMPLATES.md) - Generic programming with templates
- [Advanced Features](ADVANCED_FEATURES.md) - Destructors, native types
- [Constraints](CONSTRAINTS.md) - Template constraints

---
