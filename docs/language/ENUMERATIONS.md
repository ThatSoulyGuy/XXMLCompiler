# XXML Enumerations

Enumerations define named integer constants for type-safe value sets.

---

## Declaration Syntax

```xxml
[ Enumeration <EnumName>
    Value <NAME1> = intValue;
    Value <NAME2> = intValue;
    Value <NAME3>;  // Auto-increments from previous value
]
```

---

## Explicit Values

Each enum value can have an explicit integer assignment:

```xxml
[ Enumeration <Color>
    Value <RED> = 1;
    Value <GREEN> = 2;
    Value <BLUE> = 3;
]
```

---

## Auto-Increment

When no value is specified, the compiler auto-increments from the previous value:

```xxml
[ Enumeration <Key>
    Value <UNKNOWN> = -1;
    Value <SPACE> = 32;
    Value <A> = 65;
    Value <B>;  // Auto: 66
    Value <C>;  // Auto: 67
    Value <D>;  // Auto: 68
]
```

If no previous value exists, auto-increment starts from 0.

---

## Accessing Enum Values

Enum values are accessed using the `EnumName::ValueName` syntax:

```xxml
// Use enum value in comparison
If (keyCode.equals(Integer::Constructor(Key::SPACE))) -> {
    Run Console::printLine(String::Constructor("Space pressed"));
}

// Use as constructor argument
Instantiate Integer^ As <color> = Integer::Constructor(Color::RED);
```

---

## Features

| Feature | Description |
|---------|-------------|
| Explicit values | Assign any integer value with `= intValue` |
| Auto-increment | Continues from previous value when omitted |
| Namespace-qualified | Access via `EnumName::Value` |
| Compile-time constants | Values resolved at compile time |

---

## Complete Example

```xxml
#import Language::Core;
#import Language::System;

[ Enumeration <Direction>
    Value <NORTH> = 0;
    Value <EAST> = 1;
    Value <SOUTH> = 2;
    Value <WEST> = 3;
]

[ Enumeration <HttpStatus>
    Value <OK> = 200;
    Value <CREATED> = 201;
    Value <BAD_REQUEST> = 400;
    Value <NOT_FOUND> = 404;
    Value <INTERNAL_ERROR> = 500;
]

[ Entrypoint
    {
        // Use enum values
        Instantiate Integer^ As <currentDirection> = Integer::Constructor(Direction::NORTH);

        Run Console::printLine(String::Constructor("Direction value: ").append(currentDirection.toString()));

        // Compare with enum
        If (currentDirection.equals(Integer::Constructor(Direction::NORTH))) -> {
            Run Console::printLine(String::Constructor("Facing North"));
        }

        // HTTP status example
        Instantiate Integer^ As <status> = Integer::Constructor(HttpStatus::OK);

        If (status.equals(Integer::Constructor(HttpStatus::OK))) -> {
            Run Console::printLine(String::Constructor("Request successful"));
        }

        Exit(0);
    }
]
```

---

## Use Cases

### State Machines

```xxml
[ Enumeration <GameState>
    Value <MENU> = 0;
    Value <PLAYING> = 1;
    Value <PAUSED> = 2;
    Value <GAME_OVER> = 3;
]

// Check current state
If (state.equals(Integer::Constructor(GameState::PLAYING))) -> {
    Run updateGame();
}
```

### Configuration Options

```xxml
[ Enumeration <LogLevel>
    Value <DEBUG> = 0;
    Value <INFO> = 1;
    Value <WARNING> = 2;
    Value <ERROR> = 3;
]
```

### Bit Flags

```xxml
[ Enumeration <Permission>
    Value <NONE> = 0;
    Value <READ> = 1;
    Value <WRITE> = 2;
    Value <EXECUTE> = 4;
    Value <ALL> = 7;  // READ | WRITE | EXECUTE
]
```

---

## Limitations

- Enum values are compile-time integer constants
- No methods or associated data on enum types
- Cannot iterate over enum values
- No type safety beyond integer comparison

---

## See Also

- [Syntax](SYNTAX.md) - Core syntax reference
- [Classes](CLASSES.md) - Class declarations

