# Console

Console I/O operations for terminal interaction.

```xxml
#import Language::Core;
#import Language::System;
```

---

## Output Methods

### print

Write text without newline.

```xxml
Run Console::print(String::Constructor("Hello "));
Run Console::print(String::Constructor("World"));
// Output: Hello World (on same line)
```

| Method | Returns | Description |
|--------|---------|-------------|
| `print(message: String^)` | `None` | Write to stdout |

### printLine

Write text with trailing newline.

```xxml
Run Console::printLine(String::Constructor("Hello World"));
Run Console::printLine(String::Constructor("Next line"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `printLine(message: String^)` | `None` | Write line to stdout |

### printError

Write to standard error.

```xxml
Run Console::printError(String::Constructor("Error: file not found"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `printError(message: String^)` | `None` | Write to stderr |

### printFormatted

Formatted output with placeholder substitution.

```xxml
Run Console::printFormatted(String::Constructor("Name: %s"), String::Constructor("Alice"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `printFormatted(format: String^, value: String^)` | `None` | Printf-style output |

### clear

Clear the console screen.

```xxml
Run Console::clear();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `clear()` | `None` | Clear terminal |

---

## Input Methods

### readLine

Read a line of text from stdin.

```xxml
Run Console::print(String::Constructor("Enter name: "));
Instantiate String^ As <name> = Console::readLine();
Run Console::printLine(String::Constructor("Hello, ").append(name));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readLine()` | `String^` | Read line from stdin |

### readChar

Read a single character.

```xxml
Instantiate String^ As <ch> = Console::readChar();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readChar()` | `String^` | Read single character |

### readInt

Read and parse an integer.

```xxml
Run Console::print(String::Constructor("Enter age: "));
Instantiate Integer^ As <age> = Console::readInt();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readInt()` | `Integer^` | Read and parse integer |

### readFloat

Read and parse a float.

```xxml
Instantiate Float^ As <value> = Console::readFloat();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readFloat()` | `Float^` | Read and parse float |

### readDouble

Read and parse a double.

```xxml
Instantiate Double^ As <value> = Console::readDouble();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readDouble()` | `Double^` | Read and parse double |

### readBool

Read and parse a boolean.

```xxml
Instantiate Bool^ As <confirm> = Console::readBool();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `readBool()` | `Bool^` | Read and parse boolean |

---

## System Utilities

### getTime

Get current time in seconds since epoch.

```xxml
Instantiate Integer^ As <now> = Console::getTime();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `getTime()` | `Integer^` | Seconds since epoch |

### getTimeMillis

Get current time in milliseconds.

```xxml
Instantiate Integer^ As <nowMs> = Console::getTimeMillis();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `getTimeMillis()` | `Integer^` | Milliseconds since epoch |

### sleep

Pause execution.

```xxml
Run Console::sleep(Integer::Constructor(1000));  // Sleep 1 second
```

| Method | Returns | Description |
|--------|---------|-------------|
| `sleep(milliseconds: Integer^)` | `None` | Pause for duration |

### exit

Terminate the program.

```xxml
Run Console::exit(Integer::Constructor(0));  // Exit with code 0
```

| Method | Returns | Description |
|--------|---------|-------------|
| `exit(exitCode: Integer^)` | `None` | Terminate process |

---

## Environment Variables

### getEnv

Get environment variable value.

```xxml
Instantiate String^ As <path> = Console::getEnv(String::Constructor("PATH"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `getEnv(varName: String^)` | `String^` | Get env variable |

### setEnv

Set environment variable.

```xxml
Instantiate Bool^ As <success> = Console::setEnv(
    String::Constructor("MY_VAR"),
    String::Constructor("my_value")
);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `setEnv(varName: String^, value: String^)` | `Bool^` | Set env variable |

---

## Complete Example

```xxml
#import Language::Core;
#import Language::System;

[ Entrypoint
    {
        // Greet user
        Run Console::print(String::Constructor("What is your name? "));
        Instantiate String^ As <name> = Console::readLine();

        Run Console::print(String::Constructor("How old are you? "));
        Instantiate Integer^ As <age> = Console::readInt();

        // Display info
        Run Console::printLine(String::Constructor(""));
        Run Console::printLine(String::Constructor("Hello, ").append(name).append(String::Constructor("!")));
        Run Console::printLine(String::Constructor("You are ").append(age.toString()).append(String::Constructor(" years old.")));

        // Timing example
        Instantiate Integer^ As <start> = Console::getTimeMillis();
        Run Console::sleep(Integer::Constructor(500));
        Instantiate Integer^ As <elapsed> = Console::getTimeMillis().subtract(start);
        Run Console::printLine(String::Constructor("Waited ").append(elapsed.toString()).append(String::Constructor("ms")));

        Exit(0);
    }
]
```

---

## See Also

- [Core Types](CORE.md) - String, Integer
- [File I/O](FILE_IO.md) - File operations
