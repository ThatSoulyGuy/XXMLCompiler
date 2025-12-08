# File I/O

File operations for reading and writing files.

```xxml
#import Language::Core;
#import Language::IO;
```

---

## IO::File Class

The `File` class provides both static utility methods and instance-based file operations.

---

## Static Methods

### exists

Check if a file exists.

```xxml
Instantiate Bool^ As <found> = IO::File::exists(String::Constructor("config.txt"));
If (found) -> {
    Run Console::printLine(String::Constructor("File exists"));
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `exists(path: String^)` | `Bool^` | True if file exists |

### delete

Delete a file.

```xxml
Instantiate Bool^ As <deleted> = IO::File::delete(String::Constructor("temp.txt"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `delete(path: String^)` | `Bool^` | True if deleted |

### copy

Copy a file.

```xxml
Instantiate Bool^ As <copied> = IO::File::copy(
    String::Constructor("source.txt"),
    String::Constructor("dest.txt")
);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `copy(srcPath: String^, dstPath: String^)` | `Bool^` | True if copied |

### rename

Rename or move a file.

```xxml
Instantiate Bool^ As <renamed> = IO::File::rename(
    String::Constructor("old.txt"),
    String::Constructor("new.txt")
);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `rename(oldPath: String^, newPath: String^)` | `Bool^` | True if renamed |

### sizeOf

Get file size in bytes.

```xxml
Instantiate Integer^ As <size> = IO::File::sizeOf(String::Constructor("data.bin"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `sizeOf(path: String^)` | `Integer^` | Size in bytes |

### readAll

Read entire file content as string.

```xxml
Instantiate String^ As <content> = IO::File::readAll(String::Constructor("readme.txt"));
Run Console::printLine(content);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readAll(path: String^)` | `String^` | File content |

---

## Instance Methods

### Constructor

Open a file with a mode.

```xxml
// Read mode
Instantiate IO::File^ As <inFile> = IO::File::Constructor(
    String::Constructor("input.txt"),
    String::Constructor("r")
);

// Write mode (creates/truncates)
Instantiate IO::File^ As <outFile> = IO::File::Constructor(
    String::Constructor("output.txt"),
    String::Constructor("w")
);

// Append mode
Instantiate IO::File^ As <logFile> = IO::File::Constructor(
    String::Constructor("log.txt"),
    String::Constructor("a")
);
```

**File Modes:**
| Mode | Description |
|------|-------------|
| `"r"` | Read (file must exist) |
| `"w"` | Write (creates or truncates) |
| `"a"` | Append (creates if needed) |

### close

Close the file handle.

```xxml
Run file.close();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `close()` | `None^` | Close file |

### isOpen

Check if file is open.

```xxml
If (file.isOpen()) -> {
    // File operations
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `isOpen()` | `Bool^` | True if open |

### eof

Check if at end of file.

```xxml
While (file.eof().not()) -> {
    Instantiate String^ As <line> = file.readLine();
    Run Console::printLine(line);
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `eof()` | `Bool^` | True if at end |

---

## Reading

### readLine

Read one line from file.

```xxml
Instantiate String^ As <line> = file.readLine();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readLine()` | `String^` | Next line |

---

## Writing

### writeString

Write text to file.

```xxml
Instantiate Integer^ As <written> = file.writeString(String::Constructor("Hello"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `writeString(text: String^)` | `Integer^` | Bytes written |

### writeLine

Write text with newline.

```xxml
Run file.writeLine(String::Constructor("Line 1"));
Run file.writeLine(String::Constructor("Line 2"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `writeLine(text: String^)` | `Integer^` | Bytes written |

### flush

Flush buffered data to disk.

```xxml
Run file.flush();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `flush()` | `Integer^` | Status |

---

## File Info

### size

Get size of open file.

```xxml
Instantiate Integer^ As <bytes> = file.size();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `Integer^` | Size in bytes |

---

## Complete Example

```xxml
#import Language::Core;
#import Language::IO;
#import Language::System;

[ Entrypoint
    {
        // Write a file
        Instantiate IO::File^ As <out> = IO::File::Constructor(
            String::Constructor("example.txt"),
            String::Constructor("w")
        );
        Run out.writeLine(String::Constructor("Hello, World!"));
        Run out.writeLine(String::Constructor("This is XXML file I/O."));
        Run out.close();

        // Check file exists and size
        If (IO::File::exists(String::Constructor("example.txt"))) -> {
            Instantiate Integer^ As <size> = IO::File::sizeOf(String::Constructor("example.txt"));
            Run Console::printLine(String::Constructor("File size: ").append(size.toString()).append(String::Constructor(" bytes")));
        }

        // Read file line by line
        Instantiate IO::File^ As <in> = IO::File::Constructor(
            String::Constructor("example.txt"),
            String::Constructor("r")
        );
        Run Console::printLine(String::Constructor("Contents:"));
        While (in.eof().not()) -> {
            Instantiate String^ As <line> = in.readLine();
            Run Console::printLine(line);
        }
        Run in.close();

        // Read entire file at once
        Instantiate String^ As <all> = IO::File::readAll(String::Constructor("example.txt"));
        Run Console::printLine(String::Constructor("Full content:"));
        Run Console::printLine(all);

        // Cleanup
        Run IO::File::delete(String::Constructor("example.txt"));

        Exit(0);
    }
]
```

---

## See Also

- [Console](CONSOLE.md) - Console I/O
- [Core Types](CORE.md) - String, Bool, Integer
