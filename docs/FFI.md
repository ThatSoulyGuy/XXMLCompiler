# Foreign Function Interface (FFI)

XXML provides a Foreign Function Interface (FFI) system that allows calling native functions from dynamic libraries (DLLs on Windows). This enables interoperability with system APIs, C libraries, and other native code.

## Overview

The FFI system consists of:
- `@NativeFunction` annotation for declaring native function bindings
- `NativeType<T>` for specifying native data types
- `NativeStructure` for defining C-compatible struct layouts
- Automatic string marshalling for C string parameters

## Declaring Native Functions

Use the `@NativeFunction` annotation to declare a binding to a native function:

```xxml
@NativeFunction(path = "library.dll", name = "FunctionName", convention = "stdcall")
Method <XXMLMethodName> Returns ReturnType^ Parameters(
    Parameter <param1> Types ParamType1^,
    Parameter <param2> Types ParamType2^
);
```

### Annotation Parameters

| Parameter | Description | Values |
|-----------|-------------|--------|
| `path` | Path to the DLL containing the function | String (e.g., `"kernel32.dll"`, `"user32.dll"`) |
| `name` | The exported symbol name in the DLL | String (e.g., `"MessageBoxA"`, `"Beep"`) |
| `convention` | Calling convention | `"stdcall"`, `"cdecl"`, `"fastcall"` |

### Calling Conventions

- **stdcall**: Standard Windows API calling convention. Callee cleans the stack. Used by most Windows APIs.
- **cdecl**: C calling convention. Caller cleans the stack. Used by C runtime functions.
- **fastcall**: First two arguments passed in registers. Less common.

## Native Types

Use `NativeType<"typename">` to specify native C types:

| NativeType | C Equivalent | Size |
|------------|--------------|------|
| `NativeType<"int8">` | `int8_t` / `char` | 1 byte |
| `NativeType<"uint8">` | `uint8_t` / `unsigned char` | 1 byte |
| `NativeType<"int16">` | `int16_t` / `short` | 2 bytes |
| `NativeType<"uint16">` | `uint16_t` / `unsigned short` | 2 bytes |
| `NativeType<"int32">` | `int32_t` / `int` | 4 bytes |
| `NativeType<"uint32">` | `uint32_t` / `unsigned int` | 4 bytes |
| `NativeType<"int64">` | `int64_t` / `long long` | 8 bytes |
| `NativeType<"uint64">` | `uint64_t` / `unsigned long long` | 8 bytes |
| `NativeType<"float">` | `float` | 4 bytes |
| `NativeType<"double">` | `double` | 8 bytes |
| `NativeType<"ptr">` | `void*` / pointer | 8 bytes (64-bit) |
| `NativeType<"void">` | `void` | N/A |

## String Marshalling

XXML automatically marshals string literals to C-style null-terminated strings when passed to native functions expecting `NativeType<"ptr">` parameters.

### Example

```xxml
// Declare puts from C runtime
@NativeFunction(path = "ucrtbase.dll", name = "puts", convention = "cdecl")
Method <FFI_puts> Returns NativeType<"int32">^ Parameters(
    Parameter <str> Types NativeType<"ptr">^
);

[ Entrypoint
{
    // String literal is automatically converted to C string pointer
    Run FFI_puts("Hello from XXML FFI!");
}
]
```

### How It Works

When calling a native function:
1. The compiler detects that the target function is an FFI method
2. For parameters typed as `NativeType<"ptr">`, it checks if the argument is a string literal
3. If so, the string literal is stored as a global constant and a direct pointer is passed
4. No XXML String object is created - the raw C string pointer is passed directly

This enables seamless interop with C APIs that expect `const char*` parameters.

## Native Structures

Define C-compatible structures using `NativeStructure`:

```xxml
[ NativeStructure <POINT> Aligns(4)
[ Public<>
    Property <x> Types NativeType<"int32">^;
    Property <y> Types NativeType<"int32">^;
]
]
```

### Alignment

The `Aligns(n)` specifier sets the structure alignment in bytes. Common values:
- `Aligns(1)` - Packed, no padding
- `Aligns(4)` - 4-byte alignment (common for 32-bit structs)
- `Aligns(8)` - 8-byte alignment (common for 64-bit structs)

### Opaque Handle Types

For type-safe handles (like C's opaque pointers), define empty `NativeStructure` declarations:

```xxml
// Opaque handle types - like C's typedef struct GLFWwindow GLFWwindow;
[ NativeStructure <GLFWwindow> Aligns(8)
[ Public<>
]
]

[ NativeStructure <GLFWmonitor> Aligns(8)
[ Public<>
]
]

[ NativeStructure <GLFWcursor> Aligns(8)
[ Public<>
]
]
```

This provides type safety - you cannot accidentally pass a `GLFWmonitor^` where a `GLFWwindow^` is expected:

```xxml
// Type-safe function signatures
@NativeFunction(name = "glfwCreateWindow", path = "glfw3.dll", convention = "cdecl")
Method <glfwCreateWindow> Returns GLFWwindow^ Parameters (
    Parameter <width> Types NativeType<"int32">^,
    Parameter <height> Types NativeType<"int32">^,
    Parameter <title> Types NativeType<"ptr">^,
    Parameter <monitor> Types GLFWmonitor^,  // Type-safe!
    Parameter <share> Types GLFWwindow^      // Type-safe!
);

@NativeFunction(name = "glfwDestroyWindow", path = "glfw3.dll", convention = "cdecl")
Method <glfwDestroyWindow> Returns None Parameters (
    Parameter <window> Types GLFWwindow^
);

// Usage:
Instantiate GLFWwindow^ As <window> = glfwCreateWindow(800, 600, title, 0, 0);
Run glfwDestroyWindow(window);  // Type-safe: only accepts GLFWwindow^
```

## Complete Examples

### Windows Beep Function

```xxml
#import Language::Core;

@NativeFunction(path = "kernel32.dll", name = "Beep", convention = "stdcall")
Method <Beep> Returns NativeType<"int32">^ Parameters(
    Parameter <dwFreq> Types NativeType<"uint32">^,
    Parameter <dwDuration> Types NativeType<"uint32">^
);

[ Entrypoint
{
    Run Console::printLine(String::Constructor("Playing beep..."));
    Run Beep(440, 500);  // A4 note for 500ms
}
]
```

### Windows MessageBox

```xxml
@NativeFunction(path = "user32.dll", name = "MessageBoxA", convention = "stdcall")
Method <MessageBoxA> Returns NativeType<"int32">^ Parameters(
    Parameter <hWnd> Types NativeType<"ptr">^,
    Parameter <lpText> Types NativeType<"ptr">^,
    Parameter <lpCaption> Types NativeType<"ptr">^,
    Parameter <uType> Types NativeType<"uint32">^
);

[ Entrypoint
{
    // String literals auto-marshalled to C strings
    Run MessageBoxA(0, "Hello from XXML!", "XXML FFI", 0);
}
]
```

### C Runtime printf

```xxml
#import Language::Core;

@NativeFunction(path = "ucrtbase.dll", name = "puts", convention = "cdecl")
Method <puts> Returns NativeType<"int32">^ Parameters(
    Parameter <str> Types NativeType<"ptr">^
);

@NativeFunction(path = "ucrtbase.dll", name = "printf", convention = "cdecl")
Method <printf> Returns NativeType<"int32">^ Parameters(
    Parameter <format> Types NativeType<"ptr">^
);

[ Entrypoint
{
    Run puts("Hello via puts!");
    Run printf("Hello via printf!\n");
}
]
```

### Sleep and GetTickCount

```xxml
#import Language::Core;

@NativeFunction(path = "kernel32.dll", name = "Sleep", convention = "stdcall")
Method <Sleep> Returns None Parameters(
    Parameter <dwMilliseconds> Types NativeType<"uint32">^
);

@NativeFunction(path = "kernel32.dll", name = "GetTickCount", convention = "stdcall")
Method <GetTickCount> Returns NativeType<"uint32">^ Parameters();

[ Entrypoint
{
    Run Console::printLine(String::Constructor("Sleeping for 1 second..."));
    Run Sleep(1000);
    Run Console::printLine(String::Constructor("Done!"));
}
]
```

## Implementation Notes

### Internal Linkage

FFI thunk functions are generated with LLVM `internal` linkage to prevent symbol conflicts with system libraries. This allows using native function names directly (e.g., `Sleep`, `Beep`) without prefixing.

### Dynamic Loading

Native functions are loaded dynamically at runtime using:
- `LoadLibraryA` to load the DLL
- `GetProcAddress` to resolve the function symbol

This means:
- DLLs are loaded on first call
- Missing DLLs or symbols result in null returns (not crashes)
- System DLLs like `kernel32.dll` are always available

### Error Handling

If a DLL fails to load or a symbol is not found:
- Functions returning `void` simply return
- Functions returning integers return `0`
- Functions returning pointers return `null`

For robust code, check return values when calling functions that may fail.

## Best Practices

1. **Use appropriate calling conventions**: Windows APIs use `stdcall`, C runtime uses `cdecl`
2. **Match types exactly**: Ensure XXML NativeTypes match the C function signature
3. **Handle null returns**: Check for failure when calling functions that may not be available
4. **String encoding**: Use `*A` variants for ANSI strings (e.g., `MessageBoxA`), `*W` for Unicode
5. **Memory management**: Be careful with functions that allocate memory - ensure proper cleanup

## Limitations

- Varargs functions (like `printf` with format args) have limited support
- Callback functions (function pointers as parameters) are not yet supported
- Wide string (Unicode) marshalling requires manual conversion
- Struct return values are not yet supported
