# JSON

JSON parsing and generation for data interchange.

```xxml
#import Language::Core;
#import Language::Format;
```

---

## JSONObject

Key-value object for JSON data.

### Constructor

```xxml
Instantiate Format::JSONObject^ As <obj> = Format::JSONObject::Constructor();
```

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Create empty JSON object |

### Object Operations

#### set

Add or update a key-value pair.

```xxml
Run obj.set(String::Constructor("name"), String::Constructor("Alice"));
Run obj.set(String::Constructor("city"), String::Constructor("Seattle"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `set(key: String^, value: String^)` | `None` | Set key to value |

#### get

Retrieve a value by key.

```xxml
Instantiate String^ As <name> = obj.get(String::Constructor("name"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `get(key: String^)` | `String^` | Value or empty string |

#### has

Check if key exists.

```xxml
If (obj.has(String::Constructor("name"))) -> {
    Run Console::printLine(String::Constructor("Has name"));
}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `has(key: String^)` | `Bool^` | True if key exists |

#### remove

Remove a key-value pair.

```xxml
Run obj.remove(String::Constructor("city"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `remove(key: String^)` | `None` | Remove key |

#### size

Get number of key-value pairs.

```xxml
Instantiate Integer^ As <count> = obj.size();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `Integer^` | Number of pairs |

---

### Type-Specific Getters

#### getInteger

Get value as integer.

```xxml
Run obj.set(String::Constructor("age"), String::Constructor("25"));
Instantiate Integer^ As <age> = obj.getInteger(String::Constructor("age"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `getInteger(key: String^)` | `Integer^` | Parsed integer value |

#### getBool

Get value as boolean.

```xxml
Run obj.set(String::Constructor("active"), String::Constructor("true"));
Instantiate Bool^ As <active> = obj.getBool(String::Constructor("active"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `getBool(key: String^)` | `Bool^` | Parsed boolean value |

---

### Serialization

#### stringify

Convert object to JSON string.

```xxml
Instantiate String^ As <json> = obj.stringify();
Run Console::printLine(json);  // {"name":"Alice","age":"25"}
```

| Method | Returns | Description |
|--------|---------|-------------|
| `stringify()` | `String^` | JSON string |

---

## JSONArray

Ordered array of JSON values.

### Constructor

```xxml
Instantiate Format::JSONArray^ As <arr> = Format::JSONArray::Constructor();
```

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Create empty JSON array |

### Array Operations

#### add

Append value to array.

```xxml
Run arr.add(String::Constructor("first"));
Run arr.add(String::Constructor("second"));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `add(value: String^)` | `None` | Add to end |

#### get

Get value at index.

```xxml
Instantiate String^ As <first> = arr.get(Integer::Constructor(0));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `get(index: Integer^)` | `String^` | Value at index |

#### remove

Remove value at index.

```xxml
Run arr.remove(Integer::Constructor(0));
```

| Method | Returns | Description |
|--------|---------|-------------|
| `remove(index: Integer^)` | `None` | Remove at index |

#### size

Get array length.

```xxml
Instantiate Integer^ As <len> = arr.size();
```

| Method | Returns | Description |
|--------|---------|-------------|
| `size()` | `Integer^` | Number of elements |

---

### Serialization

#### stringify

Convert array to JSON string.

```xxml
Instantiate String^ As <json> = arr.stringify();
Run Console::printLine(json);  // ["first","second"]
```

| Method | Returns | Description |
|--------|---------|-------------|
| `stringify()` | `String^` | JSON string |

---

## Complete Example

```xxml
#import Language::Core;
#import Language::Format;
#import Language::System;

[ Entrypoint
    {
        // Build a JSON object
        Instantiate Format::JSONObject^ As <person> = Format::JSONObject::Constructor();
        Run person.set(String::Constructor("name"), String::Constructor("Alice"));
        Run person.set(String::Constructor("age"), String::Constructor("30"));
        Run person.set(String::Constructor("active"), String::Constructor("true"));

        // Output as JSON
        Run Console::printLine(String::Constructor("Person JSON:"));
        Run Console::printLine(person.stringify());

        // Read values back
        Instantiate String^ As <name> = person.get(String::Constructor("name"));
        Instantiate Integer^ As <age> = person.getInteger(String::Constructor("age"));
        Instantiate Bool^ As <active> = person.getBool(String::Constructor("active"));

        Run Console::printLine(String::Constructor("Name: ").append(name));
        Run Console::printLine(String::Constructor("Age: ").append(age.toString()));

        // Build a JSON array
        Instantiate Format::JSONArray^ As <items> = Format::JSONArray::Constructor();
        Run items.add(String::Constructor("apple"));
        Run items.add(String::Constructor("banana"));
        Run items.add(String::Constructor("cherry"));

        Run Console::printLine(String::Constructor("Items JSON:"));
        Run Console::printLine(items.stringify());

        Exit(0);
    }
]
```

---

## Implementation Notes

- JSON module is backed by C++ runtime
- All values are stored and returned as strings
- Use type-specific getters (`getInteger`, `getBool`) for automatic parsing
- Nested objects require manual string parsing

---

## See Also

- [Core Types](CORE.md) - String, Integer, Bool
- [HTTP](HTTP.md) - HTTP client (often used with JSON APIs)

