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

## User-Defined Compile-Time Classes

Classes can be marked as `Compiletime` to enable full compile-time constructor and method evaluation. When you instantiate a `Compiletime` variable of a user-defined `Compiletime` class, the compiler will:

1. Execute the constructor at compile-time
2. Execute any method calls at compile-time
3. Fold the results into constants in the generated code

### Example: Point Class

```xxml
[ Class <Point> Compiletime Final Extends None
    [ Private <>
        Property <x> Types Integer^;
        Property <y> Types Integer^;
    ]

    [ Public <>
        Constructor = default;

        Constructor Parameters (
            Parameter <px> Types Integer^,
            Parameter <py> Types Integer^
        ) -> {
            Set x = px;
            Set y = py;
        }

        Method <getX> Returns Integer^ Parameters () -> {
            Return x;
        }

        Method <getY> Returns Integer^ Parameters () -> {
            Return y;
        }
    ]
]

[ Entrypoint
    {
        // Create Point at compile-time
        Instantiate Compiletime Point^ As <p> = Point::Constructor(
            Integer::Constructor(10),
            Integer::Constructor(20)
        );

        // These method calls are evaluated at compile-time!
        // p.getX().toString() becomes "10" at compile-time
        Run Console::printLine(String::Constructor("x = ").append(p.getX().toString()));
        Run Console::printLine(String::Constructor("y = ").append(p.getY().toString()));
    }
]
```

### Generated LLVM IR

The above code generates optimized IR where method calls are completely eliminated:

```llvm
; String constants directly embedded - no runtime method calls!
@.str.1 = private constant [5 x i8] c"x = \00"
@.str.2 = private constant [3 x i8] c"10\00"   ; p.getX().toString() folded to "10"
@.str.3 = private constant [5 x i8] c"y = \00"
@.str.4 = private constant [3 x i8] c"20\00"   ; p.getY().toString() folded to "20"

define i32 @main() {
  ; No Point_getX, Point_getY, or Integer_toString calls!
  %ct.str = call ptr @String_Constructor(ptr @.str.2)
  ; ...
}
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

### Constant Constructor Arguments

When compile-time values are constructed with literal arguments, the constants are emitted directly:

```xxml
Instantiate Compiletime Integer^ As <x> = Integer::Constructor(10);
Instantiate Compiletime Integer^ As <y> = Integer::Constructor(8);
```

Generated LLVM IR:
```llvm
%0 = call ptr @Integer_Constructor(i64 10)
%1 = call ptr @Integer_Constructor(i64 8)
```

The literal values `10` and `8` are passed directly as i64 constants.

### Method Call Folding

Method calls on compile-time values are now fully evaluated at compile-time:

```xxml
Instantiate Compiletime Integer^ As <x> = Integer::Constructor(10);
Instantiate Compiletime Integer^ As <y> = Integer::Constructor(8);
Instantiate Compiletime Integer^ As <sum> = x.add(y);  // Folded to 18!

Run Console::printLine(String::Constructor("sum = ").append(sum.toString()));
```

Generated LLVM IR:
```llvm
; The value 18 is computed at compile-time and embedded directly
@.str.sum = private constant [3 x i8] c"18\00"

define i32 @main() {
  ; No Integer_add call - result is already computed!
  %ct.str = call ptr @String_Constructor(ptr @.str.sum)
  ; ...
}
```

The entire chain `x.add(y).toString()` is evaluated at compile-time, resulting in the string constant `"18"` being embedded directly in the binary.

### Constant Propagation
Compile-time values can be used in expressions with runtime values:
```xxml
Instantiate Compiletime Integer^ As <factor> = Integer::Constructor(10);
Instantiate Integer^ As <runtime> = Integer::Constructor(5);
Instantiate Integer^ As <result> = runtime.multiply(factor);
```

## Limitations

- Compile-time values must be initialized with compile-time evaluable expressions
- I/O operations cannot be performed at compile-time
- Compile-time class methods must have deterministic behavior
- Recursive compile-time methods have depth limits to prevent infinite loops
- External/native functions cannot be called at compile-time

## Supported Statement Types in Compile-Time Methods

The following statement types are supported in compile-time method/constructor bodies:

| Statement | Description |
|-----------|-------------|
| `Set x = expr` | Property or variable assignment |
| `Instantiate Type As <var> = expr` | Local variable declaration |
| `Return expr` | Return statement |
| `Run expr` | Expression statement (evaluated for side effects) |

Control flow statements (`If`, `While`, `For`) in compile-time methods are not yet supported.
