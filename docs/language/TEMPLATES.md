# XXML Templates

Version 2.0 - CORRECTED DOCUMENTATION

## Table of Contents

1. [Introduction](#introduction)
2. [Template Class Declaration](#template-class-declaration)
3. [Method Templates](#method-templates)
4. [Lambda Templates](#lambda-templates)
5. [Template Parameters](#template-parameters)
6. [Template Instantiation](#template-instantiation)
7. [Examples](#examples)
8. [Best Practices](#best-practices)

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

## Method Templates

In addition to class templates, XXML supports **method templates** - methods that have their own type parameters independent of their containing class.

### Syntax

Method templates use the `Templates` keyword after the method name:

```xxml
Method <methodName> Templates <T Constrains None> Returns ReturnType^ Parameters (...) Do
{
    // Method body using type parameter T
}
```

### Basic Example

```xxml
[ Class <Container> Final Extends None
    [ Public <>
        Constructor = default;

        // Method template - identity function that works with any type
        Method <identity> Templates <T Constrains None> Returns T^ Parameters (Parameter <value> Types T^) Do
        {
            Return value;
        }
    ]
]

[ Entrypoint
{
    Instantiate Container^ As <c> = Container::Constructor();

    // Call with Integer
    Instantiate Integer^ As <intVal> = Integer::Constructor(42);
    Instantiate Integer^ As <result1> = c.identity<Integer>(intVal);
    // result1 is 42

    // Call with String
    Instantiate String^ As <strVal> = String::Constructor("Hello");
    Instantiate String^ As <result2> = c.identity<String>(strVal);
    // result2 is "Hello"
}
]
```

### Key Features

1. **Independent Type Parameters**: Method templates have their own type parameters that are separate from any class template parameters.

2. **Call Syntax**: When calling a method template, specify the type argument using angle brackets:
   ```xxml
   object.methodName<TypeArg>(arguments)
   ```

3. **Type Inference**: The compiler generates a separate instantiation for each unique combination of type arguments used.

4. **Constraints**: Method template parameters support the same constraint syntax as class templates:
   ```xxml
   Method <sort> Templates <T Constrains Comparable> Returns None Parameters (...) Do
   ```

### Multiple Type Parameters

Method templates can have multiple type parameters:

```xxml
Method <convert> Templates <From Constrains None, To Constrains None> Returns To^ Parameters (Parameter <value> Types From^) Do
{
    // Conversion logic
}
```

### Combining with Class Templates

Method templates can be used inside template classes, with both class and method type parameters available:

```xxml
[ Class <Processor> <T Constrains None> Final Extends None
    [ Public <>
        // Method template inside a template class
        // Can use both T (class param) and U (method param)
        Method <transform> Templates <U Constrains None> Returns U^ Parameters (Parameter <input> Types T^) Do
        {
            // Implementation
        }
    ]
]
```

## Lambda Templates

In addition to class and method templates, XXML supports **lambda templates** - anonymous functions that have their own type parameters.

### Syntax

Lambda templates use the `Templates` keyword after the capture list:

```xxml
[ Lambda [captures] Templates <T Constrains None> Returns ReturnType^ Parameters (Parameter <param> Types T^)
{
    // Lambda body using type parameter T
} ]
```

### Basic Example

```xxml
[ Entrypoint
{
    // Define a lambda template - identity function that works with any type
    Instantiate __function^ As <identity> = [ Lambda [] Templates <T Constrains None> Returns T^ Parameters (Parameter <x> Types T^)
    {
        Return x;
    } ];

    // Call with Integer using angle bracket syntax
    Instantiate Integer^ As <intVal> = Integer::Constructor(42);
    Instantiate Integer^ As <result1> = identity<Integer>.call(intVal);
    Run System::Console::printLine(result1.toString());  // Prints: 42

    // Call with String
    Instantiate String^ As <strVal> = String::Constructor("Hello");
    Instantiate String^ As <result2> = identity<String>.call(strVal);
    Run System::Console::printLine(result2);  // Prints: Hello

    Exit(0);
}
]
```

### Call Syntax

Lambda templates support two syntaxes for specifying type arguments:

#### 1. Angle Bracket Syntax (`< >`)

```xxml
lambdaVar<TypeArg>.call(arguments)
```

#### 2. At-Sign Syntax (`@`)

```xxml
lambdaVar@TypeArg.call(arguments)
```

Both syntaxes are equivalent:

```xxml
// These are equivalent
Instantiate Integer^ As <r1> = identity<Integer>.call(intVal);
Instantiate Integer^ As <r2> = identity@Integer.call(intVal);
```

### Key Features

1. **Type Parameters**: Lambda templates have their own type parameters declared with the `Templates` keyword.

2. **Monomorphization**: The compiler generates a separate function for each unique type argument used. For example, `identity<Integer>` and `identity<String>` result in two different generated functions.

3. **Type-Safe Calls**: The return type and parameter types are determined by the template arguments at the call site.

4. **Constraints Support**: Lambda template parameters support the same constraint syntax as class and method templates:
   ```xxml
   [ Lambda [] Templates <T Constrains Printable> Returns None Parameters (Parameter <x> Types T^)
   {
       Run System::Console::printLine(x.toString());
   } ]
   ```

### Multiple Type Parameters

Lambda templates can have multiple type parameters:

```xxml
Instantiate __function^ As <swap> = [ Lambda [] Templates <T Constrains None, U Constrains None> Returns U^ Parameters (Parameter <first> Types T^, Parameter <second> Types U^)
{
    Return second;
} ];

// Call with different types
Instantiate String^ As <result> = swap<Integer, String>.call(
    Integer::Constructor(42),
    String::Constructor("Hello")
);
```

### Lambda Templates with Captures

Lambda templates can capture variables from their enclosing scope, just like regular lambdas:

```xxml
Instantiate Integer^ As <offset> = Integer::Constructor(10);

Instantiate __function^ As <addOffset> = [ Lambda [^offset] Templates <T Constrains None> Returns T^ Parameters (Parameter <x> Types T^)
{
    Return x.add(offset);
} ];

// Note: The captured variable is available in all instantiations
Instantiate Integer^ As <result> = addOffset<Integer>.call(Integer::Constructor(5));
// result is 15
```

### Differences from Method Templates

| Feature | Method Templates | Lambda Templates |
|---------|------------------|------------------|
| Declaration | Inside a class | Anywhere (local or global) |
| Syntax | `Method <name> Templates <T>` | `Lambda [] Templates <T>` |
| Call syntax | `obj.method<T>(args)` | `lambda<T>.call(args)` |
| Captures | N/A (uses `this`) | Explicit capture list |
| Storage | Class method table | Closure pointer |

### Implementation Notes

1. **Deferred Generation**: Template lambdas are not compiled when defined. Instead, the compiler records the template definition and generates concrete implementations only when specific type arguments are used.

2. **Name Mangling**: The compiler internally mangles lambda template instantiations:
   - `identity<Integer>` → `lambda.template.identity_LT_Integer_GT_`
   - `identity<String>` → `lambda.template.identity_LT_String_GT_`

3. **Closure Structure**: Each lambda template instantiation has its own closure structure containing:
   - A function pointer to the generated implementation
   - Captured variables (if any)

### Complete Example

```xxml
#import Language::Core;

[ Entrypoint
{
    Run System::Console::printLine(String::Constructor("Testing Lambda Templates"));

    // Lambda template - identity function
    Instantiate __function^ As <identity> = [ Lambda [] Templates <T Constrains None> Returns T^ Parameters (Parameter <x> Types T^)
    {
        Return x;
    } ];

    // Call with Integer
    Instantiate Integer^ As <intVal> = Integer::Constructor(42);
    Instantiate Integer^ As <result1> = identity<Integer>.call(intVal);
    Run System::Console::print(String::Constructor("identity<Integer>(42) = "));
    Run System::Console::printLine(result1.toString());

    // Call with String
    Instantiate String^ As <strVal> = String::Constructor("Hello Lambda");
    Instantiate String^ As <result2> = identity<String>.call(strVal);
    Run System::Console::print(String::Constructor("identity<String> = "));
    Run System::Console::printLine(result2);

    Run System::Console::printLine(String::Constructor("Lambda template test passed!"));

    Exit(0);
}
]
```

**Output:**
```
Testing Lambda Templates
identity<Integer>(42) = 42
identity<String> = Hello Lambda
Lambda template test passed!
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

## Template Structures

Structures also support template parameters with the same syntax as classes:

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
```

See [Structures](STRUCTURES.md) for complete documentation on stack-allocated value types.

## See Also

- [Structures](STRUCTURES.md) - Stack-allocated value types with templates
- [CONSTRAINTS.md](CONSTRAINTS.md) - Template constraints
- [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) - Complete language specification
- [ADVANCED_FEATURES.md](ADVANCED_FEATURES.md) - Advanced language features
