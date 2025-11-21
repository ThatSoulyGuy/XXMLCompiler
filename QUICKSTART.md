# XXML Compiler - Quick Start Guide

Get up and running with the XXML compiler in 5 minutes!

## Step 1: Build the Compiler

```bash
cmake -B build
cmake --build build --config Release
```

The compiler will be built at `build/bin/xxml` (Linux/macOS) or `build/bin/Release/xxml.exe` (Windows).

## Step 2: Write Your First Program

Create a file called `Hello.XXML`:

```xxml
#import System;

[ Entrypoint
    {
        Instantiate String As <message> = String::Constructor("Hello, XXML!");
        Run System::Print(message);
        Exit(0);
    }
]
```

## Step 3: Compile the Program

```bash
# Compile XXML to LLVM IR (mode 2)
./build/bin/xxml Hello.XXML output.ll 2

# On Windows:
# build\bin\Release\xxml.exe Hello.XXML output.ll 2
```

## Step 4: Run Your Program

The compiler generates LLVM IR which can be compiled to native code using LLVM tools or linked with the runtime library.

## Try the Example Files

The project includes examples in the `examples/` directory:

```bash
# Compile Hello.XXML
./build/bin/xxml examples/Hello.XXML output.ll 2
```

## Common Issues

### Issue: "xxml: command not found"

**Solution**: Use the full path to the compiler:
```bash
./build/bin/xxml input.XXML output.ll 2
```

### Issue: "CMake not found"

**Solution**: Install CMake:
```bash
# Ubuntu/Debian
sudo apt install cmake

# macOS
brew install cmake

# Windows
# Download from cmake.org
```

### Issue: "C++ compiler not found"

**Solution**: Install a C++ compiler:
```bash
# Ubuntu/Debian
sudo apt install build-essential

# macOS
xcode-select --install

# Windows
# Install Visual Studio or MinGW
```

## Language Basics

### Variables

```xxml
Instantiate Integer As <x> = 42i;
Instantiate String As <name> = String::Constructor("Alice");
```

### Classes

```xxml
[ Namespace <MyApp>
    [ Class <Person> Final Extends None
        [ Public <>
            Property <name> Types String^;
            Property <age> Types Integer^;
            Constructor = default;
        ]
    ]
]
```

### Methods

```xxml
Method <greet> Returns None Parameters () ->
{
    Run System::Print(name);
}
```

### Control Flow

```xxml
For (Integer <i> = 0 .. 10) ->
{
    Run System::Print(String::Convert(i));
}
```

### Ownership

- `^` = Owned (unique ownership)
- `&` = Reference (borrowed)
- `%` = Copy (value copy)

```xxml
Property <owned> Types String^;     // Owns the string
Property <reference> Types String&;  // Borrows the string
Parameter <copy> Types Integer%;     // Copies the integer
```

## Next Steps

1. **Read the Language Spec**: See `docs/LANGUAGE_SPEC.md`
2. **Explore Examples**: Check out `examples/` directory
3. **Read Architecture**: See `docs/ARCHITECTURE.md`
4. **Contribute**: See `CONTRIBUTING.md`

## Key Commands

```bash
# Build compiler
cmake -B build && cmake --build build --config Release

# Compile XXML file
./build/bin/xxml input.XXML output.ll 2
```

## Project Structure

```
XXMLCompiler/
├── examples/          # Example programs
├── tests/             # Test files
├── CMakeLists.txt     # Build configuration
├── Language/          # Standard library (in XXML)
├── runtime/           # C runtime for LLVM
└── docs/              # Documentation
```

---

For more details, see [README.md](README.md) and [docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)
