# XXML Template Constraints

Version 2.0 - CORRECTED DOCUMENTATION

## Table of Contents

1. [Introduction](#introduction)
2. [Constraint Declaration Syntax](#constraint-declaration-syntax)
3. [Requirement Types](#requirement-types)
4. [Using Constraints](#using-constraints)
5. [Examples](#examples)
6. [Best Practices](#best-practices)

## Introduction

Template constraints allow you to specify requirements that template type parameters must satisfy. This provides compile-time guarantees that template arguments have the necessary methods, properties, or characteristics.

### Benefits

- **Type Safety**: Catch errors at compile time instead of link time
- **Better Error Messages**: Clear messages about what's missing
- **Self-Documenting**: Constraints serve as documentation
- **Interface Enforcement**: Ensure types conform to expected interfaces

## Constraint Declaration Syntax

```xxml
[ Constraint <ConstraintName> <T Constrains None> (T a)
    Require (F(ReturnType)(methodName)(*) On a)
]
```

### Real Example from Test Files

```xxml
[ Constraint <Runnable> <T Constrains None> (T a)
    Require (F(Integer^)(run)(*) On a)
]
```

### Syntax Breakdown

1. **`[ Constraint <Name>`**: Start constraint declaration
2. **`<T Constrains None>`**: Template parameters for the constraint itself
3. **`(T a)`**: Parameter bindings - maps template param `T` to identifier `a`
4. **`Require (...)`**: One or more requirement statements
5. **Close with `]`**

## Requirement Types

### 1. Method Requirements

Require that a type has specific methods with particular signatures:

#### Basic Method Requirement

```xxml
Require (F(ReturnType)(methodName)(*) On paramName)
```

- **`F`**: Indicates a function/method requirement
- **First parentheses**: Return type
- **Second parentheses**: Method name
- **Third parentheses**: `*` for any parameters, or specific parameter types
- **`On paramName`**: Which template parameter this applies to

#### Examples

```xxml
// Require a method named "toString" returning String^
Require (F(String^)(toString)(*) On a)

// Require a method named "run" returning Integer^
Require (F(Integer^)(run)(*) On a)

// Require a method with specific parameter types
Require (F(Bool^)(equals)(T^) On a)

// Require a method returning None (void)
Require (F(None)(doSomething)(*) On a)
```

### 2. Constructor Requirements

Require that a type has specific constructors:

```xxml
Require (C(ParamTypes...) On paramName)
```

- **`C`**: Indicates a constructor requirement
- **Parentheses**: Parameter types, or `None` for default constructor

#### Examples

```xxml
// Require a default constructor
Require (C(None) On a)

// Require a constructor taking an Integer
Require (C(Integer^) On a)

// Require a constructor taking multiple parameters
Require (C(String^, Integer^) On a)
```

### 3. Truth Requirements

Require that a boolean expression is true at compile time:

```xxml
Require (Truth(expression))
```

#### Examples

```xxml
// Require that type T is exactly SomeClass
Require (Truth(TypeOf<T>() == TypeOf<SomeClass>()))

// Can use complex boolean expressions
Require (Truth(someCompileTimeCheck))
```

## Using Constraints

### Step 1: Define a Constraint

```xxml
[ Constraint <Printable> <T Constrains None> (T a)
    Require (F(String^)(toString)(*) On a)
]
```

### Step 2: Apply Constraint to Template Class

```xxml
[ Class <Printer> <T Constrains Printable> Final Extends None
    [ Public <>
        Method <print> Returns None Parameters (Parameter <value> Types T^) Do
        {
            // Safe to call toString() because Printable constraint guarantees it exists
            Run Console::printLine(value.toString());
        }
    ]
]
```

### Step 3: Instantiate with Valid Types

```xxml
// Integer has toString(), so this works
Instantiate Printer<Integer>^ As <intPrinter> = Printer@Integer::Constructor();

// String has toString(), so this works
Instantiate Printer<String>^ As <strPrinter> = Printer@String::Constructor();
```

### Compilation Error with Invalid Types

If you try to use a type that doesn't satisfy the constraint:

```xxml
// Compile error: MyClass doesn't have toString()
Instantiate Printer<MyClass>^ As <printer> = Printer@MyClass::Constructor();
```

Error message:
```
Error: Type 'MyClass' does not satisfy constraint 'Printable'
Required method: toString
```

## Examples

### Example 1: Printable Constraint

```xxml
[ Constraint <Printable> <T Constrains None> (T a)
    Require (F(String^)(toString)(*) On a)
]

[ Class <Logger> <T Constrains Printable> Final Extends None
    [ Public <>
        Property <items> Types Collections::List<T>^;

        Constructor Parameters() ->
        {
            Set items = Collections::List@T::Constructor();
        }

        Method <add> Returns None Parameters (Parameter <item> Types T^) Do
        {
            Run items.add(item);
        }

        Method <logAll> Returns None Parameters () Do
        {
            For (Integer <i> = 0 .. items.size()) ->
            {
                Instantiate T^ As <item> = items.get(i);
                Run Console::printLine(item.toString());
            }
        }
    ]
]
```

### Example 2: Comparable Constraint

```xxml
[ Constraint <Comparable> <T Constrains None> (T a)
    Require (F(Bool^)(lessThan)(T^) On a)
    Require (F(Bool^)(equals)(T^) On a)
]

[ Class <MinFinder> <T Constrains Comparable> Final Extends None
    [ Public <>
        Method <findMin> Returns T^ Parameters (
            Parameter <a> Types T^,
            Parameter <b> Types T^
        ) Do
        {
            If (a.lessThan(b)) ->
            {
                Return a;
            }
            Else ->
            {
                Return b;
            }
        }
    ]
]
```

### Example 3: Multiple Requirements

```xxml
[ Constraint <Sortable> <T Constrains None> (T a)
    Require (F(Bool^)(lessThan)(T^) On a)
    Require (F(Bool^)(greaterThan)(T^) On a)
    Require (F(Bool^)(equals)(T^) On a)
    Require (F(String^)(toString)(*) On a)
]

[ Class <SortedList> <T Constrains Sortable> Final Extends None
    [ Public <>
        Property <items> Types Collections::List<T>^;

        Method <insert> Returns None Parameters (Parameter <value> Types T^) Do
        {
            // Can safely use lessThan, greaterThan, equals, toString
            // because Sortable constraint guarantees they exist
        }
    ]
]
```

### Example 4: Runnable Constraint (from Test Files)

```xxml
[ Constraint <Runnable> <T Constrains None> (T a)
    Require (F(Integer^)(run)(*) On a)
]

[ Class <Runner> Final Extends None
    [ Public <>
        Constructor = default;

        Method <run> Returns Integer^ Parameters () Do
        {
            Return Integer::Constructor(42);
        }
    ]
]

[ Class <Executor> <T Constrains Runnable> Final Extends None
    [ Private <>
        Property <task> Types T^;
    ]

    [ Public <>
        Constructor = default;

        Method <execute> Returns Integer^ Parameters () Do
        {
            Return task.run();
        }
    ]
]

[ Entrypoint
    {
        Instantiate Executor<Runner>^ As <exec> = Executor@Runner::Constructor();
    }
]
```

## Best Practices

### 1. Use Minimal Requirements

Only require what you actually need:

```xxml
// Good: Only requires what's used
[ Constraint <Printable> <T Constrains None> (T a)
    Require (F(String^)(toString)(*) On a)
]

// Avoid: Requires more than necessary
[ Constraint <OverlyStrict> <T Constrains None> (T a)
    Require (F(String^)(toString)(*) On a)
    Require (F(Bool^)(equals)(T^) On a)
    Require (F(T^)(clone)(*) On a)
    Require (F(None)(serialize)(*) On a)
]
```

### 2. Document Why Each Requirement Exists

```xxml
// Document the reason for each requirement
[ Constraint <Set> <T Constrains None> (T a)
    // Need toString() to display values
    Require (F(String^)(toString)(*) On a)
    // Need equals() to check for duplicates
    Require (F(Bool^)(equals)(T^) On a)
]
```

### 3. Group Related Requirements

```xxml
// All methods needed for comparison
[ Constraint <FullyComparable> <T Constrains None> (T a)
    Require (F(Bool^)(lessThan)(T^) On a)
    Require (F(Bool^)(equals)(T^) On a)
    Require (F(Bool^)(greaterThan)(T^) On a)
]
```

### 4. Provide Clear Constraint Names

```xxml
// Good: Clear what the constraint represents
[ Constraint <Sortable> <T Constrains None> (T a)
    Require (F(Bool^)(lessThan)(T^) On a)
]

// Avoid: Unclear single letter
[ Constraint <S> <T Constrains None> (T a)
    Require (F(Bool^)(lessThan)(T^) On a)
]
```

### 5. Test Constraints with Multiple Types

```xxml
// Test your constraint with various types to ensure it works
Instantiate MyClass<Integer>^ As <c1> = MyClass@Integer::Constructor();
Instantiate MyClass<String>^ As <c2> = MyClass@String::Constructor();
Instantiate MyClass<CustomType>^ As <c3> = MyClass@CustomType::Constructor();
```

## Common Constraint Patterns

### Pattern 1: Printable

```xxml
[ Constraint <Printable> <T Constrains None> (T a)
    Require (F(String^)(toString)(*) On a)
]
```
Used for any type that needs string representation.

### Pattern 2: Comparable

```xxml
[ Constraint <Comparable> <T Constrains None> (T a)
    Require (F(Bool^)(lessThan)(T^) On a)
    Require (F(Bool^)(equals)(T^) On a)
]
```
Used for sorting and ordering operations.

### Pattern 3: Clonable

```xxml
[ Constraint <Clonable> <T Constrains None> (T a)
    Require (F(T^)(clone)(*) On a)
]
```
Used when deep copies are needed.

### Pattern 4: Hashable

```xxml
[ Constraint <Hashable> <T Constrains None> (T a)
    Require (F(Integer^)(hashCode)(*) On a)
    Require (F(Bool^)(equals)(T^) On a)
]
```
Used for hash-based collections like HashMap.

### Pattern 5: Numeric

```xxml
[ Constraint <Numeric> <T Constrains None> (T a)
    Require (F(T^)(add)(T^) On a)
    Require (F(T^)(subtract)(T^) On a)
    Require (F(T^)(multiply)(T^) On a)
    Require (F(T^)(divide)(T^) On a)
]
```
Used for mathematical operations.

## Multiple Template Parameters

Constraints can work with multiple template parameters:

```xxml
[ Constraint <Convertible> <T Constrains None, U Constrains None> (T a, U b)
    Require (F(U^)(convertTo)(*) On a)
    Require (F(T^)(convertFrom)(U^) On b)
]

[ Class <Converter> <T Constrains Convertible, U Constrains None> Final Extends None
    [ Public <>
        Method <convert> Returns U^ Parameters (Parameter <value> Types T^) Do
        {
            Return value.convertTo();
        }
    ]
]
```

## Troubleshooting

### Error: Method Not Found

```
Error: Type 'MyClass' does not satisfy constraint 'Printable'
Required method: toString
```

**Solution**: Add the required method to your class:

```xxml
[ Class <MyClass> Final Extends None
    [ Public <>
        Method <toString> Returns String^ Parameters () Do
        {
            Return String::Constructor("MyClass instance");
        }
    ]
]
```

### Error: Wrong Method Signature

Constraints check method names and basic signatures. Ensure your method:
- Has the correct name (case-sensitive)
- Returns the expected type
- Takes the expected parameter types
- Is accessible (Public)

### Multiple Requirements Not Satisfied

Each `Require` statement must be satisfied independently. Check each required method exists with the correct signature.

## Syntax Reference

### Requirement Syntax Patterns

```xxml
// Method with any parameters
Require (F(ReturnType)(methodName)(*) On paramName)

// Method with specific parameters
Require (F(ReturnType)(methodName)(ParamType1, ParamType2) On paramName)

// Default constructor
Require (C(None) On paramName)

// Constructor with parameters
Require (C(ParamType1, ParamType2) On paramName)

// Truth condition
Require (Truth(booleanExpression))
```

### Parameter Binding Syntax

```xxml
// Single binding
(T a)

// Multiple bindings
(T a, U b)
```

### Complete Constraint Example

```xxml
[ Constraint <FullExample> <T Constrains None, U Constrains None> (T item, U container)
    // Method requirements
    Require (F(String^)(toString)(*) On item)
    Require (F(Bool^)(equals)(T^) On item)

    // Constructor requirements
    Require (C(None) On item)

    // Requirements on second parameter
    Require (F(None)(add)(T^) On container)
    Require (F(T^)(get)(Integer^) On container)
]
```

## See Also

- [TEMPLATES.md](TEMPLATES.md) - Template classes and parameters
- [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) - Complete language specification
- [ADVANCED_FEATURES.md](ADVANCED_FEATURES.md) - Advanced language features
