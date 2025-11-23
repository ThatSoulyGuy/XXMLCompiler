# XXML Advanced Features

Version 2.0

## Table of Contents

1. [Destructors](#destructors)
2. [Ownership and Memory Management](#ownership-and-memory-management)
3. [Native Types](#native-types)
4. [Syscall Interface](#syscall-interface)
5. [Method Chaining](#method-chaining)
6. [Namespaces](#namespaces)
7. [Access Modifiers](#access-modifiers)
8. [Final Classes](#final-classes)

## Constructors

XXML supports multiple syntax styles for constructors, allowing both default and parameterized constructors.

### Constructor Syntax Options

**1. Default Constructor:**
```xxml
Constructor = default;
```

**2. New Syntax (Recommended):**
```xxml
Constructor Parameters() ->
{
    // Initialize members
}

Constructor Parameters (Parameter <value> Types Integer^) ->
{
    // Initialize with parameter
}
```

**3. Old Syntax (Still Supported):**
```xxml
Constructor (Parameter <value> Types Integer^) ->
{
    // Initialize with parameter
}
```

**4. Named Constructors (Static Factory Methods):**
```xxml
Method <Constructor> Returns MyClass^ Parameters (Parameter <param> Types Type^) ->
{
    // Custom initialization
    Return this;
}
```

### Complete Constructor Examples

```xxml
[ Class <Resource> Final Extends None
    [ Private <>
        Property <handle> Types NativeType<"void*">^;
        Property <size> Types Integer^;
    ]

    [ Public <>
        // Default constructor
        Constructor = default;

        // Parameterized constructor (new syntax)
        Constructor Parameters (Parameter <bufferSize> Types Integer^) ->
        {
            Set size = bufferSize;
            Instantiate NativeType<"int64">^ As <bytes> = bufferSize.toInt64();
            Set handle = Syscall::malloc(bytes);
        }

        // Named constructor for specific use case
        Method <Constructor> Returns Resource^ Parameters (
            Parameter <filePath> Types String^
        ) ->
        {
            // Custom initialization from file
            Return this;
        }
    ]
]
```

### Constructor Best Practices

```xxml
// Good: Initialize all members
Constructor Parameters() ->
{
    Set handle = Syscall::malloc(1024);
    Set size = Integer::Constructor(1024);
    Set isOpen = Bool::Constructor(true);
}

// Good: Use Parameters keyword for clarity
Constructor Parameters (Parameter <n> Types Integer^) ->
{
    Set count = n;
}

// Acceptable: Old syntax still works
Constructor (Parameter <n> Types Integer^) ->
{
    Set count = n;
}
```

## Destructors

XXML supports automatic resource cleanup through destructors with RAII (Resource Acquisition Is Initialization) semantics.

### Declaring Destructors (NEW SYNTAX)

**Current Syntax (as of Version 2.0):**
```xxml
Destructor Parameters() ->
{
    // Clean up resources
}
```

**Note**: The `Parameters()` must be empty - destructors cannot have parameters.

### Complete Destructor Example

```xxml
[ Class <Resource> Final Extends None
    [ Private <>
        Property <handle> Types NativeType<"void*">^;
        Property <isValid> Types Bool^;
    ]

    [ Public <>
        Constructor Parameters (Parameter <size> Types Integer^) ->
        {
            Instantiate NativeType<"int64">^ As <bytes> = size.toInt64();
            Set handle = Syscall::malloc(bytes);
            Set isValid = Bool::Constructor(true);
        }

        Destructor Parameters() ->
        {
            // Clean up resource
            If (isValid.getValue()) ->
            {
                Run Syscall::free(handle);
            }
        }

        Method <doSomething> Returns None Parameters () Do
        {
            // Use the resource
        }
    ]
]
```

### Automatic Destruction (RAII)

Destructors are called automatically when owned (`^`) variables go out of scope:

```xxml
[ Entrypoint
    {
        Instantiate Resource^ As <r> = Resource::Constructor(Integer::Constructor(1024));
        Run r.doSomething();
        // Destructor called automatically here when r goes out of scope
    }
]
```

### When Destructors Are Called

1. **Scope Exit**: When an owned variable goes out of scope
2. **Return Statements**: Before returning from a function
3. **Exception/Exit**: During program termination (for static/global objects)

**Important**: Only owned (`^`) objects have their destructors called automatically. References (`&`) and copy semantics (`%`) do not trigger destruction.

### Destructor Rules

1. **Syntax**: Must use `Destructor Parameters() -> { }`
2. **No Parameters**: The `Parameters()` must be empty
3. **Return Type**: Implicitly returns `void` (no return statement needed)
4. **Ownership**: Only called for owned (`^`) objects
5. **Never Call Manually**: Destructors are called automatically by the runtime

### Destructor Best Practices

```xxml
// Good: Release all resources
Destructor Parameters() ->
{
    Run closeFile();
    Run Syscall::free(buffer);
    Run releaseConnections();
}

// Good: Check validity before cleanup
Destructor Parameters() ->
{
    If (isOpen.getValue()) ->
    {
        Run close();
    }
}

// Good: Keep destructors simple and fast
Destructor Parameters() ->
{
    Run Syscall::free(data);
}

// Avoid: Complex logic or throwing errors
Destructor Parameters() ->
{
    // Keep destructors simple - avoid complex operations
    Run cleanup();
}
```

### Destructor Example with Owned Members

```xxml
[ Class <FileManager> Final Extends None
    [ Private <>
        Property <file> Types File^;
        Property <buffer> Types NativeType<"void*">^;
    ]

    [ Public <>
        Constructor Parameters (Parameter <path> Types String^) ->
        {
            Set file = File::open(path);
            Set buffer = Syscall::malloc(4096);
        }

        Destructor Parameters() ->
        {
            // Cleanup happens in reverse order of initialization
            Run Syscall::free(buffer);
            Run file.close();
            // Note: If file is owned (^), its destructor will also be called
        }
    ]
]
```

## Ownership and Memory Management

XXML uses explicit ownership semantics to prevent memory leaks and use-after-free bugs.

### Ownership Types

#### Owned (`^`)

Unique ownership - exactly one owner is responsible for cleanup:

```xxml
Instantiate String^ As <text> = String::Constructor("hello");
// text owns the String, will be destroyed when text goes out of scope
```

#### Reference (`&`)

Borrowed reference - does not own, cannot outlive the owner:

```xxml
Method <printString> Returns None Parameters (Parameter <str> Types String&) ->
{
    // str is borrowed, not owned
    // Cannot store str for later use
    Run Console::printLine(str);
}
```

#### Value Reference (`%`)

Pass-by-reference for reading - similar to `const&` in C++:

```xxml
Method <compare> Returns Bool^ Parameters (
    Parameter <a> Types Integer%,
    Parameter <b> Types Integer%
) ->
{
    Return a.equals(b);
}
```

### Move Semantics

Ownership can be transferred (moved):

```xxml
Instantiate String^ As <s1> = String::Constructor("hello");
Set s2 = s1;  // s1 is moved to s2, s1 is now invalid

// Error: s1 has been moved
Run Console::printLine(s1);  // Compile error
```

### Ownership Transfer in Functions

```xxml
Method <takeOwnership> Returns None Parameters (Parameter <str> Types String^) ->
{
    // This method takes ownership
    // str will be destroyed when method ends
}

Instantiate String^ As <text> = String::Constructor("hello");
Run takeOwnership(text);
// text is now invalid (moved)
```

### Copying vs Moving

```xxml
// Move (transfer ownership)
Instantiate String^ As <s1> = String::Constructor("hello");
Set s2 = s1;  // Move

// Copy (if type supports cloning)
Instantiate String^ As <s3> = String::Constructor("hello");
Instantiate String^ As <s4> = s3.clone();  // Both s3 and s4 are valid
```

## Native Types

XXML provides access to low-level types through `NativeType`:

### Syntax

```xxml
NativeType<"typename">
```

### Common Native Types

```xxml
// Pointer
Property <ptr> Types NativeType<"void*">^;

// 64-bit integer
Property <count> Types NativeType<"int64">^;

// 32-bit integer
Property <flags> Types NativeType<"int32">^;

// 8-bit byte
Property <byte> Types NativeType<"uint8">^;

// Boolean
Property <flag> Types NativeType<"bool">^;
```

### Using Native Types

```xxml
Class <Buffer>
[ Public <>
    Property <data> Types NativeType<"void*">^;
    Property <size> Types NativeType<"int64">^;

    Method <Constructor> Returns None Parameters (
        Parameter <bufferSize> Types NativeType<"int64">%
    ) ->
    {
        Set data = Syscall::malloc(bufferSize);
        Run Syscall::memcpy(&size, &bufferSize, 8);
    }

    Method <Destructor> Returns None Parameters () ->
    {
        Run Syscall::free(data);
    }
]
```

### Pointer Arithmetic

```xxml
// Calculate offset
Instantiate NativeType<"void*">^ As <base> = Syscall::malloc(100);
Instantiate NativeType<"int64">^ As <offset> = 10;
Instantiate NativeType<"void*">^ As <ptr> = base + offset;

// Read/write at offset
Run Syscall::ptr_write(ptr, someValue);
Instantiate SomeType^ As <value> = Syscall::ptr_read(ptr);
```

## Syscall Interface

The `Syscall` namespace provides low-level operations:

### Memory Management

```xxml
// Allocate memory
Instantiate NativeType<"void*">^ As <ptr> = Syscall::malloc(1024);

// Free memory
Run Syscall::free(ptr);

// Copy memory
Run Syscall::memcpy(dest, src, numBytes);

// Set memory
Run Syscall::memset(ptr, value, numBytes);
```

### Pointer Operations

```xxml
// Read from pointer
Instantiate MyType^ As <value> = Syscall::ptr_read(pointer);

// Write to pointer
Run Syscall::ptr_write(pointer, value);

// Read byte
Instantiate NativeType<"uint8">^ As <byte> = Syscall::read_byte(pointer);

// Write byte
Run Syscall::write_byte(pointer, byteValue);
```

### Example: Custom Allocator

```xxml
Class <MemoryPool>
[ Public <>
    Property <buffer> Types NativeType<"void*">^;
    Property <capacity> Types NativeType<"int64">^;
    Property <used> Types NativeType<"int64">^;

    Method <Constructor> Returns None Parameters (
        Parameter <size> Types NativeType<"int64">%
    ) ->
    {
        Set buffer = Syscall::malloc(size);
        Run Syscall::memcpy(&capacity, &size, 8);
        Instantiate NativeType<"int64">^ As <zero> = 0;
        Run Syscall::memcpy(&used, &zero, 8);
    }

    Method <allocate> Returns NativeType<"void*">^ Parameters (
        Parameter <size> Types NativeType<"int64">%
    ) ->
    {
        // Calculate next position
        Instantiate NativeType<"void*">^ As <ptr> = buffer + used;

        // Update used count
        Instantiate NativeType<"int64">^ As <newUsed> = used + size;
        Run Syscall::memcpy(&used, &newUsed, 8);

        Return ptr;
    }

    Method <Destructor> Returns None Parameters () ->
    {
        Run Syscall::free(buffer);
    }
]
```

## Method Chaining

XXML supports fluent interfaces through method chaining:

### Implementing Chainable Methods

Methods that return `this` enable chaining:

```xxml
Class <StringBuilder>
[ Public <>
    Property <text> Types String^;

    Method <Constructor> Returns None Parameters () ->
    {
        Set text = String::Constructor("");
    }

    Method <append> Returns StringBuilder^ Parameters (
        Parameter <str> Types String^
    ) ->
    {
        Set text = text.concat(str);
        Return this;  // Enable chaining
    }

    Method <appendLine> Returns StringBuilder^ Parameters (
        Parameter <str> Types String^
    ) ->
    {
        Set text = text.concat(str).concat(String::Constructor("\n"));
        Return this;  // Enable chaining
    }

    Method <toString> Returns String^ Parameters () ->
    {
        Return text;
    }
]
```

### Using Method Chains

```xxml
Instantiate StringBuilder^ As <builder> = StringBuilder::Constructor();

Instantiate String^ As <result> = builder
    .append(String::Constructor("Hello"))
    .append(String::Constructor(" "))
    .append(String::Constructor("World"))
    .toString();

Run Console::printLine(result);  // Hello World
```

## Namespaces

Organize code into logical groups:

### Declaring Namespaces

```xxml
[ Namespace <MyApp::Utils>
    Class <Helper>
    [ Public <>
        Method <doSomething> Returns None Parameters () -> { }
    ]
]
```

### Nested Namespaces

```xxml
[ Namespace <Company::Project::Module>
    Class <Component>
]
```

### Using Namespaced Types

```xxml
#import MyApp::Utils;

// Fully qualified
Instantiate MyApp::Utils::Helper^ As <h1> = MyApp::Utils::Helper::Constructor();

// Or use the imported name directly if no conflicts
Instantiate Helper^ As <h2> = Helper::Constructor();
```

## Access Modifiers

Control visibility of class members:

### Public

Accessible from anywhere:

```xxml
Class <MyClass>
[ Public <>
    Property <publicProp> Types Integer^;

    Method <publicMethod> Returns None Parameters () -> { }
]
```

### Private

Only accessible within the class:

```xxml
Class <MyClass>
[ Private <>
    Property <privateProp> Types Integer^;

    Method <privateHelper> Returns None Parameters () -> { }
]

[ Public <>
    Method <publicMethod> Returns None Parameters () ->
    {
        // Can access private members here
        Run privateHelper();
    }
]
```

### Protected

Accessible in the class and derived classes:

```xxml
Class <Base>
[ Protected <>
    Property <protectedProp> Types Integer^;

    Method <protectedMethod> Returns None Parameters () -> { }
]
```

### Example with All Modifiers

```xxml
Class <BankAccount>
[ Private <>
    Property <balance> Types Integer^;

    Method <validateAmount> Returns Bool^ Parameters (
        Parameter <amount> Types Integer^
    ) ->
    {
        Return amount.greaterThan(Integer::Constructor(0));
    }
]

[ Protected <>
    Property <accountId> Types String^;

    Method <logTransaction> Returns None Parameters (
        Parameter <desc> Types String^
    ) -> { }
]

[ Public <>
    Method <Constructor> Returns None Parameters () ->
    {
        Set balance = Integer::Constructor(0);
    }

    Method <deposit> Returns None Parameters (
        Parameter <amount> Types Integer^
    ) ->
    {
        If (validateAmount(amount)) ->
        {
            Set balance = balance.add(amount);
            Run logTransaction(String::Constructor("Deposit"));
        }
    }

    Method <getBalance> Returns Integer^ Parameters () ->
    {
        Return balance;
    }
]
```

## Final Classes

Prevent inheritance with the `Final` keyword:

### Syntax

```xxml
Class <UtilityClass> Final Extends None
[ Public <>
    Method <helperMethod> Returns None Parameters () -> { }
]
```

### Why Use Final?

1. **Performance**: Compiler can optimize final classes
2. **Design**: Prevent unintended inheritance
3. **Security**: Ensure behavior cannot be overridden

### Example

```xxml
// String is final - cannot be extended
Class <String> Final Extends None
[ Public <>
    Method <length> Returns Integer^ Parameters () -> { }
    Method <concat> Returns String^ Parameters (Parameter <other> Types String^) -> { }
]

// Error: Cannot extend final class
Class <MyString> Extends String  // Compile error
```

## Best Practices Summary

### Memory Management
1. Use owned (`^`) for ownership
2. Use reference (`&`) for borrowing
3. Implement destructors for resource cleanup
4. Avoid memory leaks by following ownership rules

### Native Types
1. Use only when necessary (low-level operations)
2. Prefer XXML types for safety
3. Document pointer arithmetic clearly
4. Always free allocated memory

### Design
1. Use access modifiers appropriately
2. Make classes final when inheritance is not intended
3. Design for method chaining when it improves usability
4. Organize code with namespaces

### Safety
1. Never dereference null pointers
2. Check bounds before array access
3. Validate parameters in public methods
4. Use constraints to enforce template requirements

## See Also

- [TEMPLATES.md](TEMPLATES.md) - Template programming
- [CONSTRAINTS.md](CONSTRAINTS.md) - Template constraints
- [LOOPS.md](LOOPS.md) - Loop constructs
- [LANGUAGE_SPEC.md](LANGUAGE_SPEC.md) - Complete language specification
