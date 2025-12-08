# Native Types

XXML provides access to low-level types and memory operations for systems programming.

---

## NativeType

Direct access to hardware-level types.

### Syntax

```xxml
NativeType<"typename">
```

### Common Native Types

| Type | Description | Size |
|------|-------------|------|
| `NativeType<"void*">` | Pointer type | 8 bytes (64-bit) |
| `NativeType<"int64">` | Signed 64-bit integer | 8 bytes |
| `NativeType<"int32">` | Signed 32-bit integer | 4 bytes |
| `NativeType<"int16">` | Signed 16-bit integer | 2 bytes |
| `NativeType<"int8">` | Signed 8-bit integer | 1 byte |
| `NativeType<"uint64">` | Unsigned 64-bit integer | 8 bytes |
| `NativeType<"uint32">` | Unsigned 32-bit integer | 4 bytes |
| `NativeType<"uint16">` | Unsigned 16-bit integer | 2 bytes |
| `NativeType<"uint8">` | Unsigned 8-bit (byte) | 1 byte |
| `NativeType<"bool">` | Boolean | 1 byte |
| `NativeType<"float">` | 32-bit floating point | 4 bytes |
| `NativeType<"double">` | 64-bit floating point | 8 bytes |

### Usage

```xxml
// Property declarations
Property <ptr> Types NativeType<"void*">^;
Property <count> Types NativeType<"int64">^;
Property <flags> Types NativeType<"uint32">^;
Property <byte> Types NativeType<"uint8">^;

// Local variables
Instantiate NativeType<"int64">% As <size> = 1024;
Instantiate NativeType<"void*">% As <buffer> = Syscall::malloc(size);
```

### Ownership with Native Types

Native types use the same ownership semantics as class types:

```xxml
// Owned - responsible for cleanup
Property <data> Types NativeType<"void*">^;

// Reference - borrowed
Parameter <ptr> Types NativeType<"void*">&

// Copy - value passed
Parameter <size> Types NativeType<"int64">%
```

---

## NativeStructure

Define C-compatible structures for FFI (Foreign Function Interface).

### Syntax

```xxml
[ NativeStructure <StructName>
    [ Public <>
        Property <field1> Types NativeType<"type1">%;
        Property <field2> Types NativeType<"type2">%;
    ]
]
```

### Example

```xxml
[ NativeStructure <Point>
    [ Public <>
        Property <x> Types NativeType<"int32">%;
        Property <y> Types NativeType<"int32">%;
    ]
]

[ NativeStructure <Rectangle>
    [ Public <>
        Property <left> Types NativeType<"int32">%;
        Property <top> Types NativeType<"int32">%;
        Property <right> Types NativeType<"int32">%;
        Property <bottom> Types NativeType<"int32">%;
    ]
]
```

### Usage

```xxml
// Create and use native structure
Instantiate Point% As <p>;
Set p.x = 10;
Set p.y = 20;

// Pass to native function
Run nativeDrawPoint(&p);
```

---

## CallbackType

Define function pointer types for callbacks to native code.

### Syntax

```xxml
[ CallbackType <CallbackName> Returns ReturnType Parameters (
    Parameter <param1> Types Type1,
    Parameter <param2> Types Type2
)]
```

### Example

```xxml
// Define a callback type
[ CallbackType <CompareFunc> Returns NativeType<"int32">% Parameters (
    Parameter <a> Types NativeType<"void*">%,
    Parameter <b> Types NativeType<"void*">%
)]

// Use as property type
Property <comparator> Types CompareFunc^;
```

### Passing Callbacks to Native Code

```xxml
// Register callback with native library
Run nativeSetCallback(myCallbackFunc);
```

---

## Syscall Interface

The `Syscall` namespace provides low-level operations.

### Memory Management

#### malloc

Allocate memory.

```xxml
Instantiate NativeType<"void*">% As <ptr> = Syscall::malloc(size);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `malloc(size: NativeType<"int64">%)` | `NativeType<"void*">%` | Allocate bytes |

#### free

Release allocated memory.

```xxml
Run Syscall::free(ptr);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `free(ptr: NativeType<"void*">%)` | `None` | Free memory |

#### memcpy

Copy memory bytes.

```xxml
Run Syscall::memcpy(dest, src, numBytes);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `memcpy(dest, src, bytes)` | `None` | Copy memory |

#### memset

Set memory to a value.

```xxml
Run Syscall::memset(ptr, value, numBytes);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `memset(ptr, value, bytes)` | `None` | Fill memory |

### Pointer Operations

#### ptr_read

Read value from pointer.

```xxml
Instantiate MyType^ As <value> = Syscall::ptr_read(pointer);
```

#### ptr_write

Write value to pointer.

```xxml
Run Syscall::ptr_write(pointer, value);
```

#### read_byte

Read single byte.

```xxml
Instantiate NativeType<"uint8">% As <byte> = Syscall::read_byte(pointer);
```

#### write_byte

Write single byte.

```xxml
Run Syscall::write_byte(pointer, byteValue);
```

---

## Pointer Arithmetic

Native pointers support arithmetic operations:

```xxml
// Calculate offset
Instantiate NativeType<"void*">% As <base> = Syscall::malloc(100);
Instantiate NativeType<"int64">% As <offset> = 10;
Instantiate NativeType<"void*">% As <ptr> = base + offset;

// Pointer subtraction
Instantiate NativeType<"int64">% As <diff> = ptr2 - ptr1;
```

---

## Complete Examples

### Buffer Class

```xxml
[ Class <Buffer> Final Extends None
    [ Private <>
        Property <data> Types NativeType<"void*">^;
        Property <size> Types NativeType<"int64">^;
    ]

    [ Public <>
        Constructor Parameters (
            Parameter <bufferSize> Types NativeType<"int64">%
        ) -> {
            Set data = Syscall::malloc(bufferSize);
            Set size = bufferSize;
            Run Syscall::memset(data, 0, bufferSize);
        }

        Destructor Parameters () -> {
            Run Syscall::free(data);
        }

        Method <write> Returns None Parameters (
            Parameter <offset> Types NativeType<"int64">%,
            Parameter <value> Types NativeType<"uint8">%
        ) Do {
            Instantiate NativeType<"void*">% As <ptr> = data + offset;
            Run Syscall::write_byte(ptr, value);
        }

        Method <read> Returns NativeType<"uint8">% Parameters (
            Parameter <offset> Types NativeType<"int64">%
        ) Do {
            Instantiate NativeType<"void*">% As <ptr> = data + offset;
            Return Syscall::read_byte(ptr);
        }

        Method <getSize> Returns NativeType<"int64">% Parameters () Do {
            Return size;
        }
    ]
]
```

### Memory Pool

```xxml
[ Class <MemoryPool> Final Extends None
    [ Private <>
        Property <buffer> Types NativeType<"void*">^;
        Property <capacity> Types NativeType<"int64">^;
        Property <used> Types NativeType<"int64">^;
    ]

    [ Public <>
        Constructor Parameters (
            Parameter <poolSize> Types NativeType<"int64">%
        ) -> {
            Set buffer = Syscall::malloc(poolSize);
            Set capacity = poolSize;
            Set used = 0;
        }

        Destructor Parameters () -> {
            Run Syscall::free(buffer);
        }

        Method <allocate> Returns NativeType<"void*">% Parameters (
            Parameter <size> Types NativeType<"int64">%
        ) Do {
            // Calculate next position
            Instantiate NativeType<"void*">% As <ptr> = buffer + used;

            // Update used count
            Set used = used + size;

            Return ptr;
        }

        Method <reset> Returns None Parameters () Do {
            Set used = 0;
        }
    ]
]
```

---

## Best Practices

### Memory Safety

1. **Always free allocated memory** - Match every `malloc` with `free`
2. **Check for null** - Verify allocation succeeded before use
3. **Avoid buffer overflows** - Validate indices before access
4. **Use RAII** - Wrap native resources in classes with destructors

### Performance

1. **Minimize allocations** - Reuse buffers when possible
2. **Align data** - Use natural alignment for better cache performance
3. **Batch operations** - Use `memcpy` for bulk transfers

### Interoperability

1. **Match C layouts** - Use `NativeStructure` for FFI
2. **Check calling conventions** - Ensure callback signatures match
3. **Handle endianness** - Be aware of byte order for cross-platform code

---

## See Also

- [FFI](FFI.md) - Foreign function interface
- [Destructors](DESTRUCTORS.md) - RAII and cleanup
- [Ownership](../language/OWNERSHIP.md) - Memory ownership semantics

