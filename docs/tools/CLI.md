# XXML Compiler CLI

Command-line interface reference for the XXML compiler.

---

## Synopsis

```bash
xxml [options] <input.XXML> -o <output>
```

---

## Basic Usage

### Compile to Executable

```bash
xxml Hello.XXML -o hello.exe
```

### Generate LLVM IR Only

```bash
xxml Hello.XXML -o hello.ll --ir
```

### Legacy Mode (LLVM IR Only)

```bash
xxml Hello.XXML -o hello.ll 2
```

---

## Options

| Option | Description |
|--------|-------------|
| `-o <file>` | Output file (.ll for IR, .exe/.dll for binary) |
| `--ir` | Generate LLVM IR only (same as mode 2) |
| `--processor` | Compile annotation processor to DLL |
| `--use-processor=<dll>` | Load annotation processor DLL (can be repeated) |
| `--stl-warnings` | Show warnings for standard library files (off by default) |
| `2` | Legacy mode: LLVM IR only |

---

## Output Formats

### Executable (.exe)

Default compilation target. Produces a native Windows executable.

```bash
xxml MyApp.XXML -o myapp.exe
```

### LLVM IR (.ll)

Human-readable LLVM intermediate representation. Useful for debugging and optimization analysis.

```bash
xxml MyApp.XXML -o myapp.ll --ir
```

### Dynamic Library (.dll)

For annotation processors or shared libraries.

```bash
xxml --processor MyProcessor.XXML -o MyProcessor.dll
```

---

## Annotation Processors

### Compiling a Processor

```bash
xxml --processor MyAnnotation.XXML -o MyAnnotation.dll
```

### Using a Processor

```bash
xxml --use-processor=MyAnnotation.dll App.XXML -o app.exe
```

### Multiple Processors

```bash
xxml --use-processor=Proc1.dll --use-processor=Proc2.dll App.XXML -o app.exe
```

---

## Warning Control

### Standard Library Warnings

By default, warnings from standard library files are suppressed. Enable them with:

```bash
xxml MyApp.XXML -o myapp.exe --stl-warnings
```

---

## Examples

### Basic Compilation

```bash
# Compile hello world
xxml Hello.XXML -o hello.exe

# Run the executable
./hello.exe
```

### Debug with LLVM IR

```bash
# Generate IR for inspection
xxml MyApp.XXML -o myapp.ll --ir

# View the generated IR
cat myapp.ll
```

### Annotation Processor Workflow

```bash
# 1. Create annotation processor source (MyLogger.XXML)
# 2. Compile to DLL
xxml --processor MyLogger.XXML -o MyLogger.dll

# 3. Use processor when compiling application
xxml --use-processor=MyLogger.dll App.XXML -o app.exe
```

---

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Successful compilation |
| 1 | Error (missing arguments, file not found, compilation error) |

---

## File Extensions

| Extension | Description |
|-----------|-------------|
| `.XXML` | XXML source file |
| `.exe` | Native executable |
| `.dll` | Dynamic library |
| `.ll` | LLVM IR text format |

---

## Environment

The compiler expects:
- Standard library files in `Language/` subdirectory relative to compiler location
- LLVM tools (llc, lld-link) available in PATH for executable generation
- Visual Studio Build Tools for Windows linking

---

## Compilation Pipeline

```
Source (.XXML)
    ↓
Lexical Analysis
    ↓
Syntax Analysis (AST)
    ↓
Semantic Analysis
    ↓
LLVM IR Generation (.ll)
    ↓
LLVM Compilation (.obj)
    ↓
Linking (.exe / .dll)
```

---

## Troubleshooting

### "Could not open file"

Verify the input file path exists and is accessible.

### "No output file specified"

Use `-o` flag to specify output: `xxml input.XXML -o output.exe`

### Linker Errors

Ensure LLVM tools and Visual Studio Build Tools are installed and in PATH.

### Standard Library Not Found

Verify `Language/` directory exists relative to compiler location with core STL files.

---

## See Also

- [Imports](IMPORTS.md) - Import system and module resolution
- [Architecture](ARCHITECTURE.md) - Compiler internals
- [Annotations](../advanced/ANNOTATIONS.md) - Annotation system

