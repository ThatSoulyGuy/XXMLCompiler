# XXML Templates

Version 2.0 - CORRECTED DOCUMENTATION

## Table of Contents

1. [Introduction](#introduction)
2. [Template Class Declaration](#template-class-declaration)
3. [Template Parameters](#template-parameters)
4. [Template Instantiation](#template-instantiation)
5. [Examples](#examples)
6. [Best Practices](#best-practices)

## Introduction

XXML supports generic programming through templates. Templates allow you to write code that works with multiple types while maintaining type safety.

### Key Features

- **Inline Template Parameters**: Template parameters are declared directly in the class declaration
- **Type and Non-Type Parameters**: Support for both type parameters and compile-time constant values
- **Constraints**: Restrict template parameters using the `Constrains` keyword
- **Automatic Instantiation**: The compiler generates concrete types as needed

## Template Class Declaration

### CORRECT Syntax

**IMPORTANT**: Template parameters are declared INLINE with the class name, NOT in a separate block!

```xxml
[ Class <ClassName> <T Constrains None> Final Extends None
    [ Public <>
        // members
    ]
]
```

### Real Example from stdlib (List.XXML)

```xxml
[ Namespace <Collections>
    [ Class <List> <T Constrains None> Final Extends None
        [ Private <>
            Property <dataPtr> Types NativeType<"ptr">^;
            Property <capacity> Types NativeType<"int64">^;
            Property <count> Types NativeType<"int64">^;
        ]

        [ Public <>
            Constructor () ->
            {
                Instantiate NativeType<"ptr">^ As <nullPtr> = 0;
                Instantiate NativeType<"int64">^ As <zero> = 0;
                Run Syscall::memcpy(&dataPtr, &nullPtr, 8);
                Run Syscall::memcpy(&capacity, &zero, 8);
                Run Syscall::memcpy(&count, &zero, 8);
            }

            Method <add> Returns None Parameters (Parameter <value> Types T^) Do
            {
                // Implementation
            }
        ]
    ]
]
```

### Multiple Template Parameters

Separate multiple parameters with commas:

```xxml
[ Class <Pair> <T Constrains None, U Constrains None> Final Extends None
    [ Public <>
        Property <first> Types T^;
        Property <second> Types U^;
    ]
]
```

### Template Parameters with Constraints

Use the `Constrains` keyword to specify requirements:

```xxml
[ Class <SortedList> <T Constrains Comparable> Final Extends None
    [ Public <>
        Property <items> Types Collections::List<T>^;
    ]
]
```

For no constraints, use `Constrains None`:

```xxml
[ Class <Box> <T Constrains None> Final Extends None
```

### Constraint Template Arguments

Constraints can take template arguments that bind to the class's template parameters. Two syntaxes are supported:

```xxml
// Angle bracket syntax
[ Class <HashSet> <T Constrains Hashable<T>> Final Extends None

// At-sign syntax
[ Class <HashSet> <T Constrains Hashable@T> Final Extends None
```

### Multiple Constraints with AND Semantics

Use parentheses for AND semantics - the type must satisfy **ALL** constraints:

```xxml
// K must satisfy BOTH Hashable AND Equatable
[ Class <HashMap> <K Constrains (Hashable<K>, Equatable@K), V Constrains None> Final Extends None
```

### Multiple Constraints with OR Semantics

Use pipe `|` for OR semantics - the type must satisfy **at least ONE** constraint:

```xxml
[ Class <MyClass> <T Constrains Printable | Comparable> Final Extends None
```

## Template Parameters

### Type Parameters

Type parameters represent any XXML type:

```xxml
<T Constrains None>
```

Can be:
- Built-in types: `Integer`, `String`, `Bool`
- User-defined classes
- Other template instantiations: `List<Integer>`
- Native types: `NativeType<"int64">`

### Non-Type Parameters

Non-type parameters are compile-time integer constants:

```xxml
<Size Constrains None>
```

Used for:
- Array sizes
- Buffer capacities
- Compile-time configuration

**Note**: The parser treats both type and non-type parameters the same way syntactically. The distinction is made during semantic analysis based on how they're used.

### Parameter Syntax Summary

```xxml
Single parameter:           <T Constrains None>
Multiple parameters:        <T Constrains None, U Constrains None>
With single constraint:     <T Constrains Printable>
Constraint with type arg:   <T Constrains Hashable<T>>
Constraint with @ syntax:   <T Constrains Hashable@T>
AND constraints (all):      <T Constrains (Hashable<T>, Equatable@T)>
OR constraints (any):       <T Constrains Printable | Comparable>
Non-type parameter:         <Size Constrains None>
```

## Template Instantiation

### Two Syntaxes Supported

XXML supports TWO different syntaxes for template instantiation:

#### 1. Angle Bracket Syntax (`< >`)

Used in type declarations:

```xxml
Instantiate Collections::List<Integer>^ As <list> = ...
```

#### 2. At-Sign Syntax (`@`)

Used in constructor calls and expressions:

```xxml
Collections::List@Integer::Constructor()
```

### Full Example

```xxml
// Type uses < >, constructor call uses @
Instantiate Collections::List<Integer>^ As <intList> =
    Collections::List@Integer::Constructor();

Instantiate Collections::List<String>^ As <strList> =
    Collections::List@String::Constructor();
```

### Why Two Syntaxes?

- **`< >` in types**: More readable for type declarations
- **`@` in expressions**: Avoids ambiguity with comparison operators (`<`, `>`)

### Nested Templates

```xxml
// Type declaration
Instantiate Collections::List<Collections::List<Integer>>^ As <nestedList> =
    // Constructor call
    Collections::List@Collections::List@Integer::Constructor();
```

### Non-Type Parameters

```xxml
// Fixed-size array with template type and size
Instantiate Collections::Array<Integer, 5>^ As <arr> =
    Collections::Array@Integer, 5::Constructor();
```

### Name Mangling

The compiler internally mangles template names:

- `List<Integer>` → `List_Integer`
- `Pair<String, Integer>` → `Pair_String_Integer`
- `Array<Integer, 5>` → `Array_Integer_5`

You don't need to use mangled names in your code - the compiler handles this automatically.

## Examples

### Example 1: Generic Stack

```xxml
#import Language::Core;

[ Class <Stack> <T Constrains None> Final Extends None
    [ Private <>
        Property <items> Types Collections::List<T>^;
    ]

    [ Public <>
        Constructor Parameters() ->
        {
            Set items = Collections::List@T::Constructor();
        }

        Method <push> Returns None Parameters (Parameter <value> Types T^) Do
        {
            Run items.add(value);
        }

        Method <pop> Returns T^ Parameters () Do
        {
            Instantiate Integer^ As <lastIdx> = items.size().subtract(Integer::Constructor(1));
            Instantiate T^ As <value> = items.get(lastIdx);
            Return value;
        }

        Method <isEmpty> Returns Bool^ Parameters () Do
        {
            Return items.size().equals(Integer::Constructor(0));
        }
    ]
]

[ Entrypoint
    {
        Instantiate Stack<Integer>^ As <stack> = Stack@Integer::Constructor();
        Run stack.push(Integer::Constructor(10));
        Run stack.push(Integer::Constructor(20));

        Instantiate Integer^ As <value> = stack.pop();
        Run Console::printLine(value.toString());  // Prints: 20
    }
]
```

### Example 2: Generic Pair

```xxml
[ Class <Pair> <First Constrains None, Second Constrains None> Final Extends None
    [ Public <>
        Property <first> Types First^;
        Property <second> Types Second^;

        Constructor Parameters (
            Parameter <f> Types First^,
            Parameter <s> Types Second^
        ) ->
        {
            Set first = f;
            Set second = s;
        }

        Method <getFirst> Returns First^ Parameters () Do
        {
            Return first;
        }

        Method <getSecond> Returns Second^ Parameters () Do
        {
            Return second;
        }
    ]
]

[ Entrypoint
    {
        Instantiate Pair<String, Integer>^ As <pair> = Pair@String, Integer::Constructor(
            String::Constructor("age"),
            Integer::Constructor(25)
        );

        Run Console::printLine(pair.getFirst());   // Prints: age
        Run Console::printLine(pair.getSecond().toString());  // Prints: 25
    }
]
```

### Example 3: Constrained Template

```xxml
#import MyConstraints;

[ Class <Printer> <T Constrains Printable> Final Extends None
    [ Public <>
        Method <print> Returns None Parameters (Parameter <value> Types T^) Do
        {
            Run Console::printLine(value.toString());
        }
    ]
]

[ Entrypoint
    {
        Instantiate Printer<Integer>^ As <printer> = Printer@Integer::Constructor();
        Run printer.print(Integer::Constructor(42));
    }
]
```

## Best Practices

### 1. Use Meaningful Parameter Names

```xxml
// Good
[ Class <Map> <Key Constrains None, Value Constrains None> Final Extends None

// Avoid
[ Class <Map> <T Constrains None, U Constrains None> Final Extends None
```

### 2. Document Template Requirements

If not using constraints, document what methods are needed:

```xxml
// T must have: toString() -> String^, equals(T^) -> Bool^
[ Class <UniqueList> <T Constrains None> Final Extends None
```

### 3. Use Constraints When Available

```xxml
// Better: Enforce requirements at compile time
[ Class <UniqueList> <T Constrains Printable | Comparable> Final Extends None
```

### 4. Prefer Composition Over Inheritance

```xxml
// Good: Use templates for generic containers
[ Class <Stack> <T Constrains None> Final Extends None

// Avoid: Deep template inheritance hierarchies
```

### 5. Keep Template Classes Focused

Each template should have a single, clear purpose:

```xxml
// Good: Single responsibility
[ Class <Queue> <T Constrains None> Final Extends None

// Avoid: Too many features in one template
[ Class <QueueStackMapSet> <T Constrains None> Final Extends None
```

### 6. Remember Both Instantiation Syntaxes

Always use:
- `<Type>` in type declarations
- `@Type` in constructor calls

```xxml
// Correct
Instantiate List<Integer>^ As <list> = List@Integer::Constructor();

// Wrong - will not parse correctly
Instantiate List@Integer^ As <list> = List<Integer>::Constructor();
```

## Common Patterns

### Container Pattern

```xxml
[ Class <Container> <T Constrains None> Final Extends None
    [ Private <>
        Property <data> Types Collections::List<T>^;
    ]
    [ Public <>
        Method <add> Returns None Parameters (Parameter <item> Types T^) Do { }
        Method <get> Returns T^ Parameters (Parameter <index> Types Integer^) Do { }
    ]
]
```

### Factory Pattern

```xxml
[ Class <Factory> <T Constrains None> Final Extends None
    [ Public <>
        Method <create> Returns T^ Parameters () Do
        {
            Return T::Constructor();
        }
    ]
]
```

### Iterator Pattern

```xxml
[ Class <Iterator> <T Constrains None> Final Extends None
    [ Private <>
        Property <collection> Types Collections::List<T>^;
        Property <currentIndex> Types Integer^;
    ]
    [ Public <>
        Method <hasNext> Returns Bool^ Parameters () Do { }
        Method <next> Returns T^ Parameters () Do { }
    ]
]
```

## See Also

- [CONSTRAINTS.md](CONSTRAINTS.md) - Template constraints
- [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) - Complete language specification
- [ADVANCED_FEATURES.md](ADVANCED_FEATURES.md) - Advanced language features
