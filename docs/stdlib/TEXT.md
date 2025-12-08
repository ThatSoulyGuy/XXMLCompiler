# Text Utilities

String manipulation and regular expression support.

```xxml
#import Language::Core;
#import Language::Text;
```

---

## StringUtils

Pure XXML string manipulation utilities.

### Search Methods

#### contains

Check if string contains substring.

```xxml
Instantiate Bool^ As <found> = Text::StringUtils::contains(
    String::Constructor("Hello World"),
    String::Constructor("World")
);  // true
```

| Method | Returns | Description |
|--------|---------|-------------|
| `contains(str: String^, substring: String^)` | `Bool^` | True if found |

#### indexOf

Find first occurrence of substring.

```xxml
Instantiate NativeType<"int64">% As <pos> = Text::StringUtils::indexOf(
    String::Constructor("Hello World"),
    String::Constructor("o")
);  // 4 (or -1 if not found)
```

| Method | Returns | Description |
|--------|---------|-------------|
| `indexOf(str: String^, substring: String^)` | `NativeType<"int64">%` | Index or -1 |

#### startsWith

Check if string starts with prefix.

```xxml
Instantiate Bool^ As <starts> = Text::StringUtils::startsWith(
    String::Constructor("Hello World"),
    String::Constructor("Hello")
);  // true
```

| Method | Returns | Description |
|--------|---------|-------------|
| `startsWith(str: String^, prefix: String^)` | `Bool^` | True if matches |

#### endsWith

Check if string ends with suffix.

```xxml
Instantiate Bool^ As <ends> = Text::StringUtils::endsWith(
    String::Constructor("file.txt"),
    String::Constructor(".txt")
);  // true
```

| Method | Returns | Description |
|--------|---------|-------------|
| `endsWith(str: String^, suffix: String^)` | `Bool^` | True if matches |

---

### Extraction Methods

#### substring

Extract portion of string.

```xxml
Instantiate String^ As <sub> = Text::StringUtils::substring(
    String::Constructor("Hello World"),
    Integer::Constructor(0),
    Integer::Constructor(5)
);  // "Hello"
```

| Method | Returns | Description |
|--------|---------|-------------|
| `substring(str: String^, start: Integer^, end: Integer^)` | `String^` | Extracted portion (end exclusive) |

---

### Transformation Methods

#### split

Split string by delimiter.

```xxml
Instantiate Collections::List<String>^ As <parts> = Text::StringUtils::split(
    String::Constructor("a,b,c"),
    String::Constructor(",")
);
// parts: ["a", "b", "c"]
```

| Method | Returns | Description |
|--------|---------|-------------|
| `split(str: String^, delimiter: String^)` | `Collections::List<String>^` | List of parts |

#### join

Join list of strings with delimiter.

```xxml
Instantiate Collections::List<String>^ As <words> = Collections::List@String::Constructor();
Run words.add(String::Constructor("Hello"));
Run words.add(String::Constructor("World"));

Instantiate String^ As <joined> = Text::StringUtils::join(words, String::Constructor(" "));
// "Hello World"
```

| Method | Returns | Description |
|--------|---------|-------------|
| `join(list: Collections::List<String>^, delimiter: String^)` | `String^` | Joined string |

#### trim

Remove leading and trailing whitespace.

```xxml
Instantiate String^ As <trimmed> = Text::StringUtils::trim(
    String::Constructor("  hello  ")
);  // "hello"
```

| Method | Returns | Description |
|--------|---------|-------------|
| `trim(str: String^)` | `String^` | Trimmed string |

#### repeat

Repeat string n times.

```xxml
Instantiate String^ As <dashes> = Text::StringUtils::repeat(
    String::Constructor("-"),
    Integer::Constructor(10)
);  // "----------"
```

| Method | Returns | Description |
|--------|---------|-------------|
| `repeat(str: String^, count: Integer^)` | `String^` | Repeated string |

---

### Padding Methods

#### padLeft

Pad string on left to reach width.

```xxml
Instantiate String^ As <padded> = Text::StringUtils::padLeft(
    String::Constructor("42"),
    Integer::Constructor(5),
    String::Constructor("0")
);  // "00042"
```

| Method | Returns | Description |
|--------|---------|-------------|
| `padLeft(str: String^, width: Integer^, padChar: String^)` | `String^` | Left-padded string |

#### padRight

Pad string on right to reach width.

```xxml
Instantiate String^ As <padded> = Text::StringUtils::padRight(
    String::Constructor("Name"),
    Integer::Constructor(10),
    String::Constructor(" ")
);  // "Name      "
```

| Method | Returns | Description |
|--------|---------|-------------|
| `padRight(str: String^, width: Integer^, padChar: String^)` | `String^` | Right-padded string |

---

## Pattern (Regex)

Regular expression pattern matching.

### Constructor

Create a pattern (implementation-dependent).

```xxml
Instantiate Text::Pattern^ As <pattern> = Text::Pattern::Constructor();
```

### isValid

Check if pattern compiled successfully.

```xxml
If (pattern.isValid()) -> {
    // Use pattern
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `isValid()` | `Bool^` | True if valid |

### matches

Check if entire text matches pattern.

```xxml
Instantiate Bool^ As <match> = pattern.matches(String::Constructor("test123"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `matches(text: String^)` | `Bool^` | True if full match |

### find

Check if pattern found anywhere in text.

```xxml
Instantiate Bool^ As <found> = pattern.find(String::Constructor("the test string"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `find(text: String^)` | `Bool^` | True if found |

### replace

Replace all occurrences.

```xxml
Instantiate String^ As <result> = pattern.replace(
    String::Constructor("hello world"),
    String::Constructor("X")
);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `replace(text: String^, replacement: String^)` | `String^` | Modified string |

### replaceFirst

Replace first occurrence only.

```xxml
Instantiate String^ As <result> = pattern.replaceFirst(
    String::Constructor("aaa"),
    String::Constructor("X")
);  // "Xaa"
```

| Method | Returns | Description |
|--------|---------|-------------|
| `replaceFirst(text: String^, replacement: String^)` | `String^` | Modified string |

### split

Split text by pattern.

```xxml
Instantiate Collections::List<String>^ As <parts> = pattern.split(
    String::Constructor("a1b2c3")
);
```

| Method | Returns | Description |
|--------|---------|-------------|
| `split(text: String^)` | `Collections::List<String>^` | Split parts |

---

## Complete Example

```xxml
#import Language::Core;
#import Language::Text;
#import Language::Collections;
#import Language::System;

[ Entrypoint
    {
        Instantiate String^ As <text> = String::Constructor("  Hello, World!  ");

        // Trim whitespace
        Instantiate String^ As <trimmed> = Text::StringUtils::trim(text);
        Run Console::printLine(trimmed);  // "Hello, World!"

        // Check prefix/suffix
        If (Text::StringUtils::startsWith(trimmed, String::Constructor("Hello"))) -> {
            Run Console::printLine(String::Constructor("Starts with Hello"));
        }

        // Split by delimiter
        Instantiate String^ As <csv> = String::Constructor("apple,banana,cherry");
        Instantiate Collections::List<String>^ As <fruits> = Text::StringUtils::split(
            csv,
            String::Constructor(",")
        );

        // Join with different delimiter
        Instantiate String^ As <joined> = Text::StringUtils::join(
            fruits,
            String::Constructor(" | ")
        );
        Run Console::printLine(joined);  // "apple | banana | cherry"

        // Padding for alignment
        Instantiate String^ As <num> = String::Constructor("42");
        Instantiate String^ As <padded> = Text::StringUtils::padLeft(
            num,
            Integer::Constructor(6),
            String::Constructor("0")
        );
        Run Console::printLine(padded);  // "000042"

        Exit(0);
    }
]
```

---

## See Also

- [Core Types](CORE.md) - String
- [Collections](COLLECTIONS.md) - List
