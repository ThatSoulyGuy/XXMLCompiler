# Iterators

Iterator types for traversing collections.

```xxml
#import Language::Core;
#import Language::Collections;
```

---

## Iterator Constraints

The standard library defines iterator contracts:

### Forward Iterator

Minimum requirement for iteration:

```xxml
[ Constraint <Iterator> <T Constrains None, Element Constrains None> (T iter)
    Require (F(Bool^)(hasNext)(*) On iter)
    Require (F(Element^)(next)(*) On iter)
    Require (F(Element^)(current)(*) On iter)
]
```

### Bidirectional Iterator

Extends forward iteration with backward movement:

```xxml
[ Constraint <BidirectionalIterator> <T Constrains None, Element Constrains None> (T iter)
    Require (F(Bool^)(hasPrevious)(*) On iter)
    Require (F(Element^)(previous)(*) On iter)
]
```

### Random Access Iterator

Adds index-based access:

```xxml
[ Constraint <RandomAccessIterator> <T Constrains None, Element Constrains None> (T iter)
    Require (F(None)(advance)(Integer^) On iter)
    Require (F(Element^)(at)(Integer^) On iter)
    Require (F(Integer^)(index)(*) On iter)
]
```

---

## ListIterator\<T\>

Random access iterator for List.

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor(data, size, startPos)` | Create iterator (internal use) |

### Forward Iteration

| Method | Returns | Description |
|--------|---------|-------------|
| `hasNext()` | `Bool^` | More elements ahead |
| `next()` | `T^` | Advance and return element |
| `current()` | `T^` | Current element (no advance) |

### Backward Iteration

| Method | Returns | Description |
|--------|---------|-------------|
| `hasPrevious()` | `Bool^` | More elements behind |
| `previous()` | `T^` | Move back and return element |

### Random Access

| Method | Returns | Description |
|--------|---------|-------------|
| `advance(n: Integer^)` | `None` | Move n positions forward |
| `at(idx: Integer^)` | `T^` | Get element at absolute index |
| `index()` | `Integer^` | Current position |

### Utility

| Method | Returns | Description |
|--------|---------|-------------|
| `reset()` | `None` | Return to beginning |
| `toEnd()` | `None` | Move to end |
| `equals(other: ListIterator<T>^)` | `Bool^` | Compare positions |
| `distance(other: ListIterator<T>^)` | `Integer^` | Elements between iterators |

### Example

```xxml
Instantiate Collections::List<Integer>^ As <nums> = Collections::List@Integer::Constructor();
Run nums.add(Integer::Constructor(10));
Run nums.add(Integer::Constructor(20));
Run nums.add(Integer::Constructor(30));

Instantiate Collections::ListIterator<Integer>^ As <iter> = nums.begin();

// Forward iteration
While (iter.hasNext()) -> {
    Run Console::printLine(iter.next().toString());
}
// Output: 10, 20, 30

// Random access
Run iter.reset();
Instantiate Integer^ As <middle> = iter.at(Integer::Constructor(1));  // 20
```

---

## SetIterator\<T\>

Forward iterator for Set.

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor(buckets, counts, numBkts, total)` | Create iterator (internal use) |

### Iteration

| Method | Returns | Description |
|--------|---------|-------------|
| `hasNext()` | `Bool^` | More elements available |
| `next()` | `T^` | Advance and return element |
| `current()` | `T^` | Current element |
| `reset()` | `None` | Return to beginning |

### Example

```xxml
Instantiate Collections::Set<String>^ As <tags> = Collections::Set@String::Constructor();
Run tags.add(String::Constructor("a"));
Run tags.add(String::Constructor("b"));

Instantiate Collections::SetIterator<String>^ As <iter> = tags.begin();
While (iter.hasNext()) -> {
    Run Console::printLine(iter.next());
}
```

---

## HashMapIterator\<K, V\>

Forward iterator for HashMap, yields key-value pairs.

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor(keys, values, counts, numBkts, total)` | Create iterator (internal use) |

### Iteration

| Method | Returns | Description |
|--------|---------|-------------|
| `hasNext()` | `Bool^` | More entries available |
| `next()` | `KeyValuePair<K,V>^` | Advance and return entry |
| `current()` | `KeyValuePair<K,V>^` | Current entry |
| `currentKey()` | `K&` | Reference to current key |
| `currentValue()` | `V&` | Reference to current value |
| `reset()` | `None` | Return to beginning |

### Example

```xxml
Instantiate Collections::HashMap<String, Integer>^ As <scores> =
    Collections::HashMap@String, Integer::Constructor();
Run scores.put(String::Constructor("Alice"), Integer::Constructor(95));
Run scores.put(String::Constructor("Bob"), Integer::Constructor(87));

Instantiate Collections::HashMapIterator<String, Integer>^ As <iter> = scores.begin();
While (iter.hasNext()) -> {
    Instantiate Collections::KeyValuePair<String, Integer>^ As <entry> = iter.next();
    Instantiate String^ As <line> = entry.key().append(String::Constructor(": "));
    Set line = line.append(entry.value().toString());
    Run Console::printLine(line);
}
```

---

## KeyValuePair\<K, V\>

Non-owning view of a key-value pair from HashMap iteration.

### Constructor

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Empty pair |
| `Constructor(k: K^, v: V^)` | Pair with key and value |

### Access

| Method | Returns | Description |
|--------|---------|-------------|
| `key()` | `K&` | Reference to key |
| `value()` | `V&` | Reference to value |
| `toString()` | `String^` | "(key, value)" format |

**Note**: KeyValuePair is a non-owning view. It doesn't own the key/value data - they remain owned by the HashMap.

---

## Iterable Constraints

Containers that support iteration implement:

```xxml
[ Constraint <Iterable> <Container Constrains None, IteratorType Constrains None>
    Require (F(IteratorType^)(begin)(*) On container)
    Require (F(Integer^)(size)(*) On container)
]
```

---

## Common Patterns

### Foreach-style Loop

```xxml
Instantiate Collections::ListIterator<Integer>^ As <iter> = list.begin();
While (iter.hasNext()) -> {
    Instantiate Integer^ As <item> = iter.next();
    // Use item
}
```

### Index-based Access

```xxml
For (Integer <i> = 0 .. list.size().toInt64()) -> {
    Instantiate Integer^ As <item> = list.get(i);
    // Use item
}
```

### Early Exit

```xxml
Instantiate Collections::ListIterator<String>^ As <iter> = names.begin();
While (iter.hasNext()) -> {
    Instantiate String^ As <name> = iter.next();
    If (name.equals(String::Constructor("target"))) -> {
        Run Console::printLine(String::Constructor("Found!"));
        Break;
    }
}
```

---

## See Also

- [Collections](COLLECTIONS.md) - List, HashMap, Set
- [Templates](../language/TEMPLATES.md) - Generic types
- [Constraints](../language/CONSTRAINTS.md) - Type constraints
