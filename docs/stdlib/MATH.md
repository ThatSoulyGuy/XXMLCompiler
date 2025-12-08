# Math

Mathematical functions and utilities.

```xxml
#import Language::Core;
#import Language::Math;
```

---

## Constants

Constants returned as methods (scaled by 100000 for integer precision):

| Method | Value | Description |
|--------|-------|-------------|
| `pi()` | 314159 | Pi * 100000 |
| `e()` | 271828 | Euler's number * 100000 |

```xxml
// To get actual pi: divide by 100000
Instantiate Integer^ As <piScaled> = Math::pi();
// For floating-point precision, use Float/Double operations
```

---

## Basic Operations

### abs

Absolute value.

```xxml
Instantiate Integer^ As <x> = Integer::Constructor(-5);
Instantiate Integer^ As <absX> = Math::abs(x);  // 5
```

| Method | Returns | Description |
|--------|---------|-------------|
| `abs(value: Integer^)` | `Integer^` | Absolute value |

### min / max

Minimum and maximum of two values.

```xxml
Instantiate Integer^ As <smaller> = Math::min(Integer::Constructor(5), Integer::Constructor(3));  // 3
Instantiate Integer^ As <larger> = Math::max(Integer::Constructor(5), Integer::Constructor(3));  // 5
```

| Method | Returns | Description |
|--------|---------|-------------|
| `min(a: Integer^, b: Integer^)` | `Integer^` | Smaller value |
| `max(a: Integer^, b: Integer^)` | `Integer^` | Larger value |

### clamp

Constrain value to range.

```xxml
Instantiate Integer^ As <clamped> = Math::clamp(
    Integer::Constructor(15),   // value
    Integer::Constructor(0),    // min
    Integer::Constructor(10)    // max
);  // 10
```

| Method | Returns | Description |
|--------|---------|-------------|
| `clamp(value: Integer^, minVal: Integer^, maxVal: Integer^)` | `Integer^` | Value in range |

---

## Power and Roots

### pow

Exponentiation.

```xxml
Instantiate Integer^ As <result> = Math::pow(Integer::Constructor(2), Integer::Constructor(10));  // 1024
```

| Method | Returns | Description |
|--------|---------|-------------|
| `pow(base: Integer^, exponent: Integer^)` | `Integer^` | base^exponent |

### sqrt

Square root (integer).

```xxml
Instantiate Integer^ As <root> = Math::sqrt(Integer::Constructor(16));  // 4
```

| Method | Returns | Description |
|--------|---------|-------------|
| `sqrt(value: Integer^)` | `Integer^` | Square root |

### cbrt

Cube root.

```xxml
Instantiate Integer^ As <cubeRoot> = Math::cbrt(Integer::Constructor(27));  // 3
```

| Method | Returns | Description |
|--------|---------|-------------|
| `cbrt(value: Integer^)` | `Integer^` | Cube root |

---

## Rounding

### floor / ceil / round

```xxml
// Note: These operate on integer representations
// For actual rounding, use with scaled values
Instantiate Integer^ As <f> = Math::floor(Integer::Constructor(7));
Instantiate Integer^ As <c> = Math::ceil(Integer::Constructor(7));
Instantiate Integer^ As <r> = Math::round(Integer::Constructor(7));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `floor(value: Integer^)` | `Integer^` | Round down |
| `ceil(value: Integer^)` | `Integer^` | Round up |
| `round(value: Integer^)` | `Integer^` | Round to nearest |

---

## Trigonometry

Trigonometric functions using integer angles (typically in degrees or scaled radians).

```xxml
Instantiate Integer^ As <sinVal> = Math::sin(Integer::Constructor(45));
Instantiate Integer^ As <cosVal> = Math::cos(Integer::Constructor(45));
Instantiate Integer^ As <tanVal> = Math::tan(Integer::Constructor(45));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `sin(angle: Integer^)` | `Integer^` | Sine |
| `cos(angle: Integer^)` | `Integer^` | Cosine |
| `tan(angle: Integer^)` | `Integer^` | Tangent |

---

## Logarithms

### log / log10 / log2

```xxml
Instantiate Integer^ As <natural> = Math::log(Integer::Constructor(100));
Instantiate Integer^ As <base10> = Math::log10(Integer::Constructor(100));
Instantiate Integer^ As <base2> = Math::log2(Integer::Constructor(8));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `log(value: Integer^)` | `Integer^` | Natural logarithm |
| `log10(value: Integer^)` | `Integer^` | Base-10 logarithm |
| `log2(value: Integer^)` | `Integer^` | Base-2 logarithm |

---

## Random Numbers

### random

Get a random integer.

```xxml
Instantiate Integer^ As <r> = Math::random();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `random()` | `Integer^` | Random integer |

### randomRange

Get random integer in range [min, max].

```xxml
Instantiate Integer^ As <dice> = Math::randomRange(Integer::Constructor(1), Integer::Constructor(6));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `randomRange(minVal: Integer^, maxVal: Integer^)` | `Integer^` | Random in range |

---

## Number Theory

### gcd

Greatest common divisor.

```xxml
Instantiate Integer^ As <g> = Math::gcd(Integer::Constructor(12), Integer::Constructor(8));  // 4
```

| Method | Returns | Description |
|--------|---------|-------------|
| `gcd(a: Integer^, b: Integer^)` | `Integer^` | GCD |

### lcm

Least common multiple.

```xxml
Instantiate Integer^ As <l> = Math::lcm(Integer::Constructor(4), Integer::Constructor(6));  // 12
```

| Method | Returns | Description |
|--------|---------|-------------|
| `lcm(a: Integer^, b: Integer^)` | `Integer^` | LCM |

### isPrime

Check if number is prime.

```xxml
Instantiate Bool^ As <prime> = Math::isPrime(Integer::Constructor(17));  // true
```

| Method | Returns | Description |
|--------|---------|-------------|
| `isPrime(value: Integer^)` | `Bool^` | Primality test |

---

## Sequences

### factorial

Calculate n!

```xxml
Instantiate Integer^ As <fact> = Math::factorial(Integer::Constructor(5));  // 120
```

| Method | Returns | Description |
|--------|---------|-------------|
| `factorial(n: Integer^)` | `Integer^` | n! |

### fibonacci

Get nth Fibonacci number.

```xxml
Instantiate Integer^ As <fib> = Math::fibonacci(Integer::Constructor(10));  // 55
```

| Method | Returns | Description |
|--------|---------|-------------|
| `fibonacci(n: Integer^)` | `Integer^` | Fibonacci(n) |

---

## Complete Example

```xxml
#import Language::Core;
#import Language::Math;
#import Language::System;

[ Entrypoint
    {
        // Basic operations
        Instantiate Integer^ As <a> = Integer::Constructor(-10);
        Run Console::printLine(String::Constructor("abs(-10) = ").append(Math::abs(a).toString()));

        // Power
        Instantiate Integer^ As <power> = Math::pow(Integer::Constructor(2), Integer::Constructor(8));
        Run Console::printLine(String::Constructor("2^8 = ").append(power.toString()));

        // Random dice roll
        Instantiate Integer^ As <roll> = Math::randomRange(Integer::Constructor(1), Integer::Constructor(6));
        Run Console::printLine(String::Constructor("Dice roll: ").append(roll.toString()));

        // Check if prime
        Instantiate Integer^ As <num> = Integer::Constructor(97);
        If (Math::isPrime(num)) -> {
            Run Console::printLine(num.toString().append(String::Constructor(" is prime")));
        }

        // Fibonacci sequence
        Run Console::print(String::Constructor("First 10 Fibonacci: "));
        For (Integer <i> = 0 .. 10) -> {
            Run Console::print(Math::fibonacci(i).toString().append(String::Constructor(" ")));
        }
        Run Console::printLine(String::Constructor(""));

        Exit(0);
    }
]
```

---

## See Also

- [Core Types](CORE.md) - Integer, Float, Double
