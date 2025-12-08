# XXML Lambdas

XXML supports lambda expressions and function reference types for first-class functions.

---

## Lambda Expression Syntax

```xxml
[ Lambda [captures] Returns ReturnType Parameters (parameters) {
    // body
}]
```

### Components

| Component | Description |
|-----------|-------------|
| `Lambda` | Keyword starts the lambda |
| `[captures]` | Optional capture list (explicit captures only) |
| `Returns ReturnType` | Return type with ownership |
| `Parameters (...)` | Optional parameter list |
| `{ ... }` | Lambda body |

---

## Function Reference Types

Function reference types use the `F(...)` syntax:

```xxml
F(ReturnType)(ParamType1, ParamType2, ...)^
```

### Examples

```xxml
// Function taking Integer&, returning Integer^
F(Integer^)(Integer&)^

// Function taking no parameters, returning String^
F(String^)()^

// Function taking two parameters, returning Bool^
F(Bool^)(Integer&, Integer&)^

// Function returning None (void)
F(None)(String&)^
```

---

## Capture Semantics

Lambdas use explicit capture lists with **required ownership modifiers** that match XXML's ownership semantics.

| Modifier | Name | Closure Storage | Lambda Body Access |
|----------|------|-----------------|-------------------|
| `%var` | Copy | Stores value directly | Reads stored value |
| `^var` | Owned | Stores value directly | Reads stored value |
| `&var` | Reference | Stores address of variable | Dereferences to get current value |

---

### Copy Capture (`%`)

The closure stores a **copy of the value** at the time the lambda is created.

```xxml
Instantiate Integer^ As <x> = Integer::Constructor(10);

Instantiate F(Integer^)()^ As <getCopy> = [ Lambda [%x] Returns Integer^ Parameters () {
    // x is the value that was captured when the lambda was created
    Return x;
}];

// Even if x changes, the lambda sees the original value (10)
Instantiate Integer^ As <result> = getCopy.call();  // Returns 10
```

---

### Owned Capture (`^`)

The closure stores the value, and **ownership is conceptually transferred** to the lambda.

```xxml
Instantiate Integer^ As <data> = Integer::Constructor(42);

Instantiate F(Integer^)()^ As <consume> = [ Lambda [^data] Returns Integer^ Parameters () {
    // data's ownership moved into the lambda
    Return data;
}];

// data should not be used after this point (ownership transferred)
Instantiate Integer^ As <result> = consume.call();  // Returns 42
```

---

### Reference Capture (`&`)

The closure stores the **address of the variable**. Each access dereferences to get the current value.

```xxml
Instantiate Integer^ As <counter> = Integer::Constructor(0);

Instantiate F(Integer^)()^ As <readCounter> = [ Lambda [&counter] Returns Integer^ Parameters () {
    // Reads counter through the stored reference
    Return counter;
}];

// The lambda sees the current value of counter
Set counter = Integer::Constructor(100);
Instantiate Integer^ As <val> = readCounter.call();  // Returns 100 (current value)
```

---

### Multiple Captures

Mix capture modes in a single lambda:

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

---

### No Captures

Lambdas with empty capture lists access no external variables:

```xxml
Instantiate F(Integer^)(Integer&)^ As <double> = [ Lambda [] Returns Integer^ Parameters (
    Parameter <n> Types Integer&
) {
    Return n.multiply(Integer::Constructor(2));
}];
```

---

## Calling Lambdas

Use the `.call()` method to invoke a lambda:

```xxml
// No arguments
Instantiate Integer^ As <result> = noArgLambda.call();

// With arguments
Instantiate Integer^ As <sum> = addLambda.call(Integer::Constructor(5), Integer::Constructor(3));

// For None return type, use Run
Run printerLambda.call(String::Constructor("Hello"));
```

---

## Examples

### Basic Lambda

```xxml
#import Language::Core;
#import Language::System;

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

### Counter with Reference Capture

```xxml
Instantiate Integer^ As <count> = Integer::Constructor(0);

// Lambda captures count by reference
Instantiate F(Integer^)()^ As <getCount> = [ Lambda [&count] Returns Integer^ Parameters () {
    Return count;
}];

Run Console::printLine(getCount.call().toString());  // 0

Set count = Integer::Constructor(5);
Run Console::printLine(getCount.call().toString());  // 5

Set count = Integer::Constructor(10);
Run Console::printLine(getCount.call().toString());  // 10
```

---

## Implementation Details

### Closure Structure

Lambdas compile to closure structs with the following layout:

```
{ ptr (function_pointer), ptr (capture_0), ptr (capture_1), ... }
```

- **Slot 0**: Function pointer to the generated lambda function
- **Slots 1+**: Captured values or references

### Capture Storage (at lambda creation)

| Mode | What's Stored | Code Generated |
|------|---------------|----------------|
| `%var` (Copy) | Value loaded from variable | `load ptr, ptr %var` then store |
| `^var` (Owned) | Value loaded from variable | `load ptr, ptr %var` then store |
| `&var` (Reference) | Address of variable's alloca | Store `%var` (the alloca ptr) directly |

### Capture Access (in lambda body)

| Mode | How Value is Retrieved |
|------|----------------------|
| `%var` / `^var` | Single load: `load ptr, ptr %capture.var.ptr` |
| `&var` | Double load: First load gets alloca address, second load gets value |

### Generated Lambda Function

Each lambda generates a function with signature:

```llvm
define ptr @lambda.N(ptr %closure, <param_types>...) {
    ; Load captures from closure struct
    ; Execute lambda body
    ; Return result
}
```

### The `.call()` Method

Invoking a lambda via `.call()`:

1. Load the closure pointer
2. Extract the function pointer from slot 0
3. Perform indirect call: `call ptr %func_ptr(ptr %closure, <args>...)`

---

## Ownership Semantics Summary

- **Copy (`%`)** and **Owned (`^`)** currently generate identical code (both store the value)
- The distinction is semantic: `^` indicates the original should not be used after capture
- **Reference (`&`)** stores the address, enabling access to the variable's current value

---

## Limitations

- Lambdas cannot capture `this` (self-reference in methods)
- Recursive lambdas require manual setup
- Lambda types are not interchangeable even if signatures match (nominal typing)

---

## See Also

- [Ownership](OWNERSHIP.md) - Memory ownership semantics
- [Syntax](SYNTAX.md) - Core syntax reference

