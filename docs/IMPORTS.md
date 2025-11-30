# Import System

XXML uses a file-based module system for code organization and reuse. This document describes how the import system works based on the `ImportResolver` implementation.

## Syntax

```xxml
#import Language::Core;
```

The `#import` directive takes a qualified namespace path and imports all XXML files found in the corresponding directory.

## How Import Resolution Works

When the compiler encounters `#import Language::Core;`, it performs the following steps:

### 1. Path Conversion

The namespace path is converted to a directory path by replacing `::` with `/`:

```
Language::Core  →  Language/Core
```

### 2. Search Path Lookup

The compiler searches for the directory in multiple locations, in order:

1. **Executable directory** - The folder containing `xxml.exe`
2. **Language folder** - `<executable_dir>/Language` (if it exists)
3. **Fallback Language folder** - `./Language` relative to current directory
4. **Current directory** - `.`
5. **Source file directory** - The parent directory of the file being compiled

### 3. File Discovery

Once a matching directory is found, the compiler loads **all** `.XXML` files in that directory. For example, if `Language/Core/` contains:

```
Language/Core/
├── String.XXML
├── Integer.XXML
├── Bool.XXML
└── Console.XXML
```

Then `#import Language::Core;` loads all four files.

### 4. Module Naming

Each file becomes a module with a name derived from its path:

| File Path | Module Name |
|-----------|-------------|
| `Language/Core/String.XXML` | `Language::Core::String` |
| `Language/Core/Integer.XXML` | `Language::Core::Integer` |
| `MyLib/Utils.XXML` | `MyLib::Utils` |

## Search Path Details

The `ImportResolver` initializes search paths in its constructor:

```cpp
// From ImportResolver.cpp
ImportResolver::ImportResolver() {
    std::string exeDir = getExecutableDirectory();
    std::string languagePath = exeDir + "/Language";

    // Add the executable directory itself
    addSearchPath(exeDir);

    // Check if Language folder exists next to executable
    if (fs::exists(languagePath) && fs::is_directory(languagePath)) {
        addSearchPath(languagePath);
    } else {
        // Fallback: try relative to current directory
        if (fs::exists("Language") && fs::is_directory("Language")) {
            addSearchPath("Language");
        }
    }

    // Current directory for user code
    addSearchPath(".");
}
```

When compiling a source file, its parent directory is also added:

```cpp
void ImportResolver::addSourceFileDirectory(const std::string& sourceFilePath) {
    fs::path filePath(sourceFilePath);
    if (filePath.has_parent_path()) {
        std::string dirPath = filePath.parent_path().string();
        addSearchPath(dirPath);
    }
}
```

## Module Caching

Modules are cached after first load to avoid redundant parsing. The cache is keyed by module name:

```cpp
// From ImportResolver.h
std::map<std::string, std::unique_ptr<Module>> moduleCache;
```

If a module is requested that has already been loaded, the cached version is returned.

## Compiler Output

When the compiler finds modules, it prints diagnostic messages:

```
✓ Found standard library at: C:/path/to/xxml/Language
✓ Added source directory to search paths: D:/MyProject
Auto-discovering XXML files in search paths...
  Scanning: C:/path/to/xxml
  Scanning: C:/path/to/xxml/Language
    Found: Language/Core/String.XXML -> Language::Core::String
```

## Example Directory Structure

A typical project layout:

```
xxml/
├── xxml.exe              # Compiler executable
└── Language/             # Standard library
    ├── Core/
    │   ├── String.XXML
    │   ├── Integer.XXML
    │   ├── Bool.XXML
    │   └── Console.XXML
    └── GLFW/
        └── GLFW.XXML     # Third-party library

MyProject/
├── Main.XXML             # Your code
└── Utils/
    └── Helpers.XXML      # Your modules
```

In `Main.XXML`:

```xxml
#import Language::Core;    // Loads all files from xxml/Language/Core/
#import Language::GLFW;    // Loads xxml/Language/GLFW/GLFW.XXML
#import Utils;             // Loads MyProject/Utils/*.XXML

[ Entrypoint
{
    // Use imported types
    Run Console::printLine(String::Constructor("Hello!"));
}]
```

## Importing Specific Files

Currently, XXML imports all files from a directory. To import from a single-file module, create a directory containing just that file:

```
Language/
└── GLFW/
    └── GLFW.XXML    # Single file in directory
```

Then `#import Language::GLFW;` loads just `GLFW.XXML`.

## Auto-Discovery

The `ImportResolver` can discover all XXML files in search paths using `discoverAllModules()`. This scans directories non-recursively and skips `build/` and `x64/` directories to avoid compiled artifacts.

## Module Structure

Each module tracks:

| Field | Description |
|-------|-------------|
| `moduleName` | Qualified name (e.g., `Language::Core::String`) |
| `filePath` | Path to the source file |
| `fileContent` | Raw source code |
| `ast` | Parsed abstract syntax tree |
| `exportedSymbols` | Symbol table for exported declarations |
| `imports` | List of modules this module depends on |
| `isParsed` | Whether parsing is complete |
| `isAnalyzed` | Whether semantic analysis is complete |
| `isCompiled` | Whether code generation is complete |

## Error Handling

If the `Language` folder is not found, the compiler prints a warning:

```
Warning: Language folder not found (searched: C:/path/to/xxml/Language and ./Language)
```

If a module file fails to load:

```
Warning: Failed to load module file: path/to/File.XXML
```

## Implementation Files

The import system is implemented in:

- `include/Import/ImportResolver.h` - Resolver class declaration
- `include/Import/Module.h` - Module class declaration
- `src/Import/ImportResolver.cpp` - Resolution logic
- `src/Import/Module.cpp` - Module loading
- `src/Parser/Parser.cpp` - `#import` parsing (`parseImport()`)
