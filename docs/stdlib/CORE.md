# Core Types

The `Language::Core` module provides primitive wrapper types for XXML.

## Integer

64-bit signed integer wrapper.

```xxml
#import Language::Core;

Instantiate Integer^ As <x> = Integer::Constructor(42);
Instantiate Integer^ As <y> = Integer::Constructor(10);
Instantiate Integer^ As <sum> = x.add(y);  // 52
```

### Constructors

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates integer with value 0 |
| `Constructor(val: NativeType<"int64">%)` | Creates integer from native int64 |

### Arithmetic Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `add(other: Integer&)` | `Integer^` | Addition |
| `subtract(other: Integer&)` | `Integer^` | Subtraction |
| `multiply(other: Integer&)` | `Integer^` | Multiplication |
| `divide(other: Integer&)` | `Integer^` | Division |
| `modulo(other: Integer&)` | `Integer^` | Remainder |
| `negate()` | `Integer^` | Negation (-x) |
| `abs()` | `Integer^` | Absolute value |

### Compound Assignment

| Method | Returns | Description |
|--------|---------|-------------|
| `addAssign(other: Integer&)` | `Integer^` | x += y |
| `subtractAssign(other: Integer&)` | `Integer^` | x -= y |
| `multiplyAssign(other: Integer&)` | `Integer^` | x *= y |
| `divideAssign(other: Integer&)` | `Integer^` | x /= y |
| `moduloAssign(other: Integer&)` | `Integer^` | x %= y |

### Comparison Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `equals(other: Integer&)` | `Bool^` | Equality check |
| `lessThan(other: Integer&)` | `Bool^` | Less than |
| `greaterThan(other: Integer&)` | `Bool^` | Greater than |
| `lessOrEqual(other: Integer&)` | `Bool^` | Less or equal |
| `greaterOrEqual(other: Integer&)` | `Bool^` | Greater or equal |

### Conversion Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `toString()` | `String^` | Convert to string |
| `toInt64()` | `NativeType<"int64">%` | Get native value |
| `toInt32()` | `NativeType<"int32">^` | Convert to 32-bit |
| `hash()` | `NativeType<"int64">^` | Hash code (for collections) |

---

## String

UTF-8 text string backed by C++ std::string.

```xxml
Instantiate String^ As <greeting> = String::Constructor("Hello");
Instantiate String^ As <name> = String::Constructor("World");
Instantiate String^ As <message> = greeting.append(String::Constructor(" ")).append(name);
```

### Constructors

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates empty string |
| `Constructor(cstr: NativeType<"cstr">%)` | Creates from C string literal |

### Basic Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `length()` | `Integer^` | Number of characters |
| `isEmpty()` | `Bool^` | True if length is 0 |
| `copy()` | `String^` | Create independent copy |
| `equals(other: String&)` | `Bool^` | Content equality |
| `hash()` | `NativeType<"int64">^` | Hash code (for collections) |

### Manipulation Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `append(other: String&)` | `String^` | Concatenate strings |
| `charAt(index: Integer&)` | `String^` | Get character at index |
| `setCharAt(index: Integer&, ch: String&)` | `None` | Set character at index |

### Conversion

| Method | Returns | Description |
|--------|---------|-------------|
| `toCString()` | `NativeType<"cstr">%` | Get C string pointer |

### Static Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `FromCString(ptr: NativeType<"ptr">)` | `String^` | Create from raw pointer |

---

## Bool

Boolean wrapper type.

```xxml
Instantiate Bool^ As <flag> = Bool::Constructor(true);
If (flag.getValue()) -> {
    Run Console::printLine(String::Constructor("Flag is true"));
}
```

### Constructors

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates false |
| `Constructor(val: NativeType<"bool">%)` | Creates from native bool |

### Logical Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `and(other: Bool&)` | `Bool^` | Logical AND |
| `or(other: Bool&)` | `Bool^` | Logical OR |
| `not()` | `Bool^` | Logical NOT |
| `xor(other: Bool&)` | `Bool^` | Exclusive OR |
| `equals(other: Bool&)` | `Bool^` | Equality check |

### Conversion

| Method | Returns | Description |
|--------|---------|-------------|
| `getValue()` | `NativeType<"bool">%` | Get native value |
| `toBool()` | `NativeType<"bool">%` | Alias for getValue |
| `toInteger()` | `Integer^` | 1 for true, 0 for false |

---

## Float

32-bit floating point wrapper.

```xxml
Instantiate Float^ As <pi> = Float::Constructor(3.14f);
Instantiate Float^ As <radius> = Float::Constructor(5.0f);
Instantiate Float^ As <area> = pi.multiply(radius).multiply(radius);
```

### Constructors

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates 0.0 |
| `Constructor(val: NativeType<"float">%)` | Creates from native float |

### Arithmetic Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `add(other: Float&)` | `Float^` | Addition |
| `subtract(other: Float&)` | `Float^` | Subtraction |
| `multiply(other: Float&)` | `Float^` | Multiplication |
| `divide(other: Float&)` | `Float^` | Division |
| `negate()` | `Float^` | Negation |
| `abs()` | `Float^` | Absolute value |

### Comparison Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `equals(other: Float&)` | `Bool^` | Equality check |
| `lessThan(other: Float&)` | `Bool^` | Less than |
| `greaterThan(other: Float&)` | `Bool^` | Greater than |

### Conversion

| Method | Returns | Description |
|--------|---------|-------------|
| `toString()` | `String^` | Convert to string |
| `toFloat()` | `NativeType<"float">%` | Get native value |
| `toInteger()` | `Integer^` | Truncate to integer |
| `toDouble()` | `Double` | Convert to double |

---

## Double

64-bit floating point wrapper.

```xxml
Instantiate Double^ As <precise> = Double::Constructor(3.141592653589793D);
```

### Constructors

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates 0.0 |
| `Constructor(val: NativeType<"double">%)` | Creates from native double |

### Arithmetic Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `add(other: Double&)` | `Double^` | Addition |
| `subtract(other: Double&)` | `Double^` | Subtraction |
| `multiply(other: Double&)` | `Double^` | Multiplication |
| `divide(other: Double&)` | `Double^` | Division |
| `negate()` | `Double^` | Negation |
| `abs()` | `Double^` | Absolute value |

### Comparison Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `equals(other: Double&)` | `Bool^` | Equality check |
| `lessThan(other: Double&)` | `Bool^` | Less than |
| `greaterThan(other: Double&)` | `Bool^` | Greater than |
| `lessOrEqual(other: Double&)` | `Bool^` | Less or equal |
| `greaterOrEqual(other: Double&)` | `Bool^` | Greater or equal |

### Conversion

| Method | Returns | Description |
|--------|---------|-------------|
| `toString()` | `String^` | Convert to string |
| `toDouble()` | `NativeType<"double">%` | Get native value |
| `getValue()` | `NativeType<"double">%` | Alias for toDouble |
| `toInteger()` | `Integer^` | Truncate to integer |
| `toFloat()` | `Float` | Convert to float |

---

## None

Unit/void type for methods with no return value.

```xxml
Method <doSomething> Returns None Parameters () -> {
    Run Console::printLine(String::Constructor("Side effect"));
}
```

### Constructors

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Creates None instance |

---

## Compile-Time Support

All core types (`Integer`, `Float`, `Double`, `Bool`, `String`) are marked `Compiletime`, meaning they can be evaluated at compile-time:

```xxml
Instantiate Compiletime Integer^ As <x> = Integer::Constructor(10);
Instantiate Compiletime Integer^ As <y> = Integer::Constructor(5);
Instantiate Compiletime Integer^ As <sum> = x.add(y);  // Computed at compile-time
```

See [Compile-Time Evaluation](../language/COMPILETIME.md) for details.

---

## See Also

- [Collections](COLLECTIONS.md) - Generic data structures
- [Math](MATH.md) - Mathematical functions
- [Ownership System](../language/OWNERSHIP.md) - Understanding `^`, `&`, `%`
