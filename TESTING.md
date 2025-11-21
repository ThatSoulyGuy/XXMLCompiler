# Testing the XXML Compiler

This guide explains how to test the XXML compiler.

## Test Files

Test files are located in the `tests/` directory. Example programs are in `examples/`.

## Quick Test

### Step 1: Build the Compiler
```bash
cmake -B build
cmake --build build --config Release
```

### Step 2: Run a Test
```bash
# Compile an example file to LLVM IR
./build/bin/xxml examples/Hello.XXML output.ll 2

# On Windows:
# build\bin\Release\xxml.exe examples\Hello.XXML output.ll 2
```

### Step 3: Verify Output
Check that the output file was created:
```bash
ls -lh output.ll
```

## CMake Testing

The project includes CMake tests:

```bash
cd build
ctest

# Verbose output
ctest -V
```

## Running Multiple Tests

To run tests from the `tests/` directory:

```bash
# Run a specific test
./build/bin/xxml tests/SimpleTest.XXML output.ll 2

# On Windows
build\bin\Release\xxml.exe tests\SimpleTest.XXML output.ll 2
```

## Error Testing

### Test Syntax Errors
Create a file with syntax errors and verify the compiler reports them correctly:

```xxml
[ Namespace <Test>
    [ Class Missing closing bracket
]
```

### Test Semantic Errors
Test type checking with undefined types:

```xxml
[ Namespace <Test>
    [ Class <MyClass> Final Extends None
        [ Public <>
            Property <x> Types UndefinedType^;
        ]
    ]
]
```

## Test Coverage Checklist

Use this checklist to verify features:

- [ ] Imports
- [ ] Namespaces
- [ ] Classes and inheritance
- [ ] Access modifiers (Public/Private/Protected)
- [ ] Properties with ownership (^, &, %)
- [ ] Constructors
- [ ] Methods with parameters
- [ ] Templates with constraints
- [ ] For loops
- [ ] Instantiate statements
- [ ] Run statements
- [ ] Method calls
- [ ] Arithmetic operators
- [ ] String/Integer literals
- [ ] Comments
- [ ] Exit statement
- [ ] Entrypoint block

## Troubleshooting

### "File not found"
Check the file path is correct and the file exists.

### "Permission denied"
Make the binary executable: `chmod +x build/bin/xxml`

### "No output file created"
Check compiler return code: `echo $?` (should be 0 for success)

---

For more information, see:
- [QUICKSTART.md](QUICKSTART.md) - Getting started
- [README.md](README.md) - Full documentation
