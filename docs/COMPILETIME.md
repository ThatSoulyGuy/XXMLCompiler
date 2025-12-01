# Compile-Time Evaluation

XXML supports compile-time evaluation, allowing expressions to be computed during compilation rather than at runtime. This enables constant folding, type-safe compile-time constants, and optimization opportunities.

## Basic Syntax

Use the `Compiletime` keyword after `Instantiate` to declare a compile-time constant:

```xxml
Instantiate Compiletime Integer^ As <x> = Integer::Constructor(10);
Instantiate Compiletime Bool^ As <flag> = Bool::Constructor(true);
Instantiate Compiletime String^ As <name> = String::Constructor("Hello");
```

## Supported Types

The following built-in types are declared as `Compiletime` classes and support compile-time evaluation:

| Type | Description | Source |
|------|-------------|--------|
| `Integer` | 64-bit signed integers | `Language/Core/Integer.XXML` |
| `Float` | 32-bit floating point | `Language/Core/Float.XXML` |
| `Double` | 64-bit floating point | `Language/Core/Double.XXML` |
| `Bool` | Boolean values | `Language/Core/Bool.XXML` |
| `String` | String values | `Language/Core/String.XXML` |
| `Void` | No value (built-in) | N/A |

These types are marked with the `Compiletime` modifier in their class definitions, making all their methods eligible for compile-time evaluation.

## Compile-Time Operations

### Integer Methods

```xxml
Instantiate Compiletime Integer^ As <x> = Integer::Constructor(10);
Instantiate Compiletime Integer^ As <y> = Integer::Constructor(5);

// Arithmetic
Instantiate Compiletime Integer^ As <sum> = x.add(y);        // 15
Instantiate Compiletime Integer^ As <diff> = x.subtract(y);  // 5
Instantiate Compiletime Integer^ As <prod> = x.multiply(y);  // 50
Instantiate Compiletime Integer^ As <quot> = x.divide(y);    // 2
Instantiate Compiletime Integer^ As <rem> = x.modulo(y);     // 0

// Unary operations
Instantiate Compiletime Integer^ As <neg> = x.negate();      // -10
Instantiate Compiletime Integer^ As <abs> = neg.abs();       // 10

// Comparisons (return Bool)
Instantiate Compiletime Bool^ As <eq> = x.equals(y);         // false
Instantiate Compiletime Bool^ As <lt> = x.lessThan(y);       // false
Instantiate Compiletime Bool^ As <gt> = x.greaterThan(y);    // true

// Properties
Instantiate Compiletime Bool^ As <isZ> = x.isZero();         // false
Instantiate Compiletime Bool^ As <isP> = x.isPositive();     // true
Instantiate Compiletime Bool^ As <isE> = x.isEven();         // true

// Bitwise operations
Instantiate Compiletime Integer^ As <band> = x.bitwiseAnd(y);
Instantiate Compiletime Integer^ As <bor> = x.bitwiseOr(y);
Instantiate Compiletime Integer^ As <bxor> = x.bitwiseXor(y);
Instantiate Compiletime Integer^ As <lsh> = x.leftShift(Integer::Constructor(2));
Instantiate Compiletime Integer^ As <rsh> = x.rightShift(Integer::Constructor(1));

// Conversions
Instantiate Compiletime String^ As <str> = x.toString();     // "10"
Instantiate Compiletime Float^ As <f> = x.toFloat();
Instantiate Compiletime Double^ As <d> = x.toDouble();
```

### Float/Double Methods

```xxml
Instantiate Compiletime Float^ As <f> = Float::Constructor(3.14f);

// Arithmetic
Instantiate Compiletime Float^ As <sum> = f.add(Float::Constructor(1.0f));
Instantiate Compiletime Float^ As <neg> = f.negate();
Instantiate Compiletime Float^ As <abs> = neg.abs();

// Math functions
Instantiate Compiletime Float^ As <fl> = f.floor();
Instantiate Compiletime Float^ As <cl> = f.ceil();
Instantiate Compiletime Float^ As <rd> = f.round();
Instantiate Compiletime Float^ As <sq> = f.sqrt();
Instantiate Compiletime Float^ As <sn> = f.sin();
Instantiate Compiletime Float^ As <cs> = f.cos();
Instantiate Compiletime Float^ As <tn> = f.tan();
Instantiate Compiletime Float^ As <ex> = f.exp();
Instantiate Compiletime Float^ As <lg> = f.log();

// Double has additional methods
Instantiate Compiletime Double^ As <d> = Double::Constructor(3.14D);
Instantiate Compiletime Double^ As <asn> = d.asin();
Instantiate Compiletime Double^ As <acs> = d.acos();
Instantiate Compiletime Double^ As <atn> = d.atan();
Instantiate Compiletime Double^ As <l10> = d.log10();
```

### Bool Methods

```xxml
Instantiate Compiletime Bool^ As <a> = Bool::Constructor(true);
Instantiate Compiletime Bool^ As <b> = Bool::Constructor(false);

// Logical operations
Instantiate Compiletime Bool^ As <notA> = a.not();           // false
Instantiate Compiletime Bool^ As <andR> = a.and(b);          // false
Instantiate Compiletime Bool^ As <orR> = a.or(b);            // true
Instantiate Compiletime Bool^ As <xorR> = a.xor(b);          // true
Instantiate Compiletime Bool^ As <impl> = a.implies(b);      // false

// Comparisons
Instantiate Compiletime Bool^ As <eq> = a.equals(b);         // false

// Conversions
Instantiate Compiletime String^ As <str> = a.toString();     // "true"
Instantiate Compiletime Integer^ As <int> = a.toInteger();   // 1
```

### String Methods

```xxml
Instantiate Compiletime String^ As <s> = String::Constructor("Hello World");

// Properties
Instantiate Compiletime Integer^ As <len> = s.length();      // 11
Instantiate Compiletime Bool^ As <empty> = s.isEmpty();      // false

// Transformations
Instantiate Compiletime String^ As <up> = s.toUpperCase();   // "HELLO WORLD"
Instantiate Compiletime String^ As <lo> = s.toLowerCase();   // "hello world"
Instantiate Compiletime String^ As <tr> = s.trim();
Instantiate Compiletime String^ As <rv> = s.reverse();       // "dlroW olleH"

// String operations
Instantiate Compiletime String^ As <cat> = s.append(String::Constructor("!"));
Instantiate Compiletime Bool^ As <has> = s.contains(String::Constructor("World"));  // true
Instantiate Compiletime Bool^ As <sw> = s.startsWith(String::Constructor("Hello")); // true
Instantiate Compiletime Bool^ As <ew> = s.endsWith(String::Constructor("World"));   // true
Instantiate Compiletime Integer^ As <idx> = s.indexOf(String::Constructor("o"));    // 4

// Substring and character access
Instantiate Compiletime String^ As <ch> = s.charAt(Integer::Constructor(0));        // "H"
Instantiate Compiletime String^ As <sub> = s.substring(Integer::Constructor(0), Integer::Constructor(5)); // "Hello"

// Replace
Instantiate Compiletime String^ As <rep> = s.replace(String::Constructor("World"), String::Constructor("XXML"));

// Repeat
Instantiate Compiletime String^ As <r3> = String::Constructor("ab").repeat(Integer::Constructor(3)); // "ababab"
```

## Chained Operations

Compile-time operations can be chained:

```xxml
Instantiate Compiletime Integer^ As <a> = Integer::Constructor(2);
Instantiate Compiletime Integer^ As <b> = Integer::Constructor(3);
Instantiate Compiletime Integer^ As <c> = Integer::Constructor(4);

// (a + b) * c = (2 + 3) * 4 = 20
Instantiate Compiletime Integer^ As <result> = a.add(b).multiply(c);
```

## Mixed Compile-Time and Runtime

Compile-time values can be used with runtime values:

```xxml
Instantiate Compiletime Integer^ As <factor> = Integer::Constructor(10);
Instantiate Integer^ As <runtime> = Integer::Constructor(5);

// Compile-time value used in runtime expression
Instantiate Integer^ As <result> = runtime.multiply(factor);  // 50
```

## Use in Control Flow

Compile-time boolean values can be used in control flow:

```xxml
Instantiate Compiletime Bool^ As <debug> = Bool::Constructor(true);

If (debug) -> {
    Run Console::printLine(String::Constructor("Debug mode enabled"));
}
```

## Compile-Time Classes

Classes can be marked as compile-time to enable compile-time constructor and method evaluation:

```xxml
[ Class <Config> Compiletime Final Extends None
    [ Public <>
        Property Compiletime <maxRetries> Types Integer^;
        Property Compiletime <timeout> Types Integer^;

        Constructor Compiletime Parameters (
            Parameter <retries> Types Integer^,
            Parameter <time> Types Integer^
        ) -> {
            Set maxRetries = retries;
            Set timeout = time;
        }
    ]
]
```

## Compile-Time Methods

Individual methods can be marked as compile-time:

```xxml
[ Class <Math> Final Extends None
    [ Public <>
        Method <factorial> Compiletime Returns Integer^ Parameters (
            Parameter <n> Types Integer^
        ) Do {
            // Compile-time recursive factorial
            If (n.lessOrEqual(Integer::Constructor(1))) -> {
                Return Integer::Constructor(1);
            }
            Return n.multiply(factorial(n.subtract(Integer::Constructor(1))));
        }
    ]
]
```

## Benefits

1. **Performance**: Expressions computed at compile-time have zero runtime overhead
2. **Type Safety**: Compile-time evaluation catches errors during compilation
3. **Optimization**: The compiler can optimize based on known constant values
4. **Code Clarity**: Clearly marks values that are constant throughout execution

## Optimizations Applied

The compiler applies the following optimizations for compile-time values:

### Compile-Time Expression Evaluation
All operations on compile-time values are evaluated during compilation:
```xxml
Instantiate Compiletime Integer^ As <x> = Integer::Constructor(10);
Instantiate Compiletime Integer^ As <y> = Integer::Constructor(5);
Instantiate Compiletime Integer^ As <sum> = x.add(y);  // Evaluated to 15 at compile-time
```
The `x.add(y)` call is computed at compile-time; no runtime method call occurs.

### Value Caching
When a compile-time value is accessed at runtime (e.g., for printing), the wrapper object is created only once and cached:
```xxml
Run Console::printLine(sum.toString());  // Creates Integer(15) once
Run Console::printLine(sum.toString());  // Reuses the cached object
```
This avoids redundant object allocation for repeated accesses.

### Constant Propagation
Compile-time values can be used in expressions with runtime values:
```xxml
Instantiate Compiletime Integer^ As <factor> = Integer::Constructor(10);
Instantiate Integer^ As <runtime> = Integer::Constructor(5);
Instantiate Integer^ As <result> = runtime.multiply(factor);  // factor is constant 10
```

## Limitations

- Compile-time values must be initialized with compile-time evaluable expressions
- User-defined compile-time methods are still experimental
- Compile-time objects have limited support
- I/O operations cannot be performed at compile-time
