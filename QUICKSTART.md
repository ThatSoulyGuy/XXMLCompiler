# XXML Compiler - Quick Start Guide

Get up and running with the XXML compiler in 5 minutes!

## Step 1: Build the Compiler

### Windows
```cmd
build.bat
```

### Linux/macOS
```bash
chmod +x build.sh
./build.sh
```

The compiler will be built at `build/bin/xxml` (or `build/bin/Release/xxml.exe` on Windows).

## Step 2: Write Your First Program

Create a file called `hello.xxml`:

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
# Compile XXML to C++
./build/bin/xxml hello.xxml hello.cpp

# On Windows:
# build\bin\Release\xxml.exe hello.xxml hello.cpp
```

## Step 4: Compile the Generated C++

```bash
# GCC/Clang
g++ -std=c++17 hello.cpp -o hello

# MSVC
cl /EHsc /std:c++17 hello.cpp
```

## Step 5: Run Your Program

```bash
./hello
```

## Try the Test File

The project includes `Test.XXML` which demonstrates all language features:

```bash
# Compile Test.XXML
./build/bin/xxml Test.XXML test_output.cpp

# Compile the C++
g++ -std=c++17 test_output.cpp -o test

# Run it
./test
```

## Common Issues

### Issue: "xxml: command not found"

**Solution**: Use the full path to the compiler:
```bash
./build/bin/xxml input.xxml output.cpp
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

- `^` = Owned (like `std::unique_ptr`)
- `&` = Reference (like `T&`)
- `%` = Copy (like `T`)

```xxml
Property <owned> Types String^;     // Owns the string
Property <reference> Types String&;  // Borrows the string
Parameter <copy> Types Integer%;     // Copies the integer
```

## Next Steps

1. **Read the Language Spec**: See `docs/LANGUAGE_SPEC.md`
2. **Explore Examples**: Check out `Test.XXML`
3. **Read Architecture**: See `docs/ARCHITECTURE.md`
4. **Contribute**: See `CONTRIBUTING.md`

## Help & Support

- **Documentation**: See `docs/` directory
- **Issues**: Open an issue on GitHub
- **Examples**: See `Test.XXML` and `runtime/` directory

## Key Commands

```bash
# Build compiler
mkdir build && cd build && cmake .. && cmake --build .

# Compile XXML file
./build/bin/xxml input.xxml output.cpp

# Compile generated C++
g++ -std=c++17 output.cpp -o program

# Run program
./program
```

## Project Structure

```
XXMLCompiler/
├── Test.XXML          # Example program
├── build.bat          # Windows build script
├── build.sh           # Unix build script
├── CMakeLists.txt     # Build configuration
├── runtime/           # Standard library
│   ├── Integer.XXML
│   ├── String.XXML
│   └── System.XXML
└── docs/              # Documentation
    ├── LANGUAGE_SPEC.md
    └── ARCHITECTURE.md
```

## Example Programs

### Hello World
```xxml
#import System;

[ Entrypoint
    {
        Run System::Print(String::Constructor("Hello!"));
        Exit(0);
    }
]
```

### Calculator
```xxml
[ Namespace <Math>
    [ Class <Calculator> Final Extends None
        [ Public <>
            Method <add> Returns Integer^ Parameters (
                Parameter <a> Types Integer%,
                Parameter <b> Types Integer%
            ) ->
            {
                Return a + b;
            }
        ]
    ]
]
```

### For Loop
```xxml
[ Entrypoint
    {
        For (Integer <i> = 0 .. 5) ->
        {
            Run System::Print(String::Convert(i));
        }
        Exit(0);
    }
]
```

---

**Ready to start coding in XXML!** 🚀

For more details, see [README.md](README.md) and [docs/LANGUAGE_SPEC.md](docs/LANGUAGE_SPEC.md)
