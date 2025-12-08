# Time

Date, time, and duration handling.

```xxml
#import Language::Core;
#import Language::Time;
```

---

## DateTime

Represents a point in time.

### Constructor

Create DateTime for current time.

```xxml
Instantiate Time::DateTime^ As <now> = Time::DateTime::Constructor();
```

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Current date/time |

### Time Representation

| Method | Returns | Description |
|--------|---------|-------------|
| `toMilliseconds()` | `Integer^` | Milliseconds since epoch |
| `toSeconds()` | `Integer^` | Seconds since epoch |
| `toString()` | `String^` | "YYYY-MM-DD HH:MM:SS" format |

### Arithmetic

| Method | Returns | Description |
|--------|---------|-------------|
| `addMilliseconds(ms: Integer^)` | `DateTime^` | Add milliseconds |
| `addSeconds(s: Integer^)` | `DateTime^` | Add seconds |
| `addMinutes(m: Integer^)` | `DateTime^` | Add minutes |
| `addHours(h: Integer^)` | `DateTime^` | Add hours |
| `addDays(d: Integer^)` | `DateTime^` | Add days |
| `difference(other: DateTime^)` | `Integer^` | Difference in milliseconds |

### Comparison

| Method | Returns | Description |
|--------|---------|-------------|
| `isBefore(other: DateTime^)` | `Bool^` | True if before other |
| `isAfter(other: DateTime^)` | `Bool^` | True if after other |

### Example

```xxml
Instantiate Time::DateTime^ As <now> = Time::DateTime::Constructor();
Run Console::printLine(now.toString());

// Add one week
Instantiate Time::DateTime^ As <nextWeek> = now.addDays(Integer::Constructor(7));
Run Console::printLine(String::Constructor("Next week: ").append(nextWeek.toString()));

// Compare dates
If (nextWeek.isAfter(now)) -> {
    Run Console::printLine(String::Constructor("Next week is in the future"));
}
```

---

## TimeSpan

Represents a duration.

### Constructor

```xxml
Instantiate Time::TimeSpan^ As <duration> = Time::TimeSpan::Constructor();
```

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Zero duration |

### Properties

The `duration` property stores milliseconds internally.

### Conversion

| Method | Returns | Description |
|--------|---------|-------------|
| `toMilliseconds()` | `Integer^` | Total milliseconds |
| `toSeconds()` | `Integer^` | Total seconds |
| `toMinutes()` | `Integer^` | Total minutes |
| `toHours()` | `Integer^` | Total hours |
| `toDays()` | `Integer^` | Total days |
| `toString()` | `String^` | String representation |

### Arithmetic

| Method | Returns | Description |
|--------|---------|-------------|
| `add(other: TimeSpan^)` | `TimeSpan^` | Add durations |
| `subtract(other: TimeSpan^)` | `TimeSpan^` | Subtract durations |

---

## Timer

Stopwatch for measuring elapsed time.

### Constructor

```xxml
Instantiate Time::Timer^ As <timer> = Time::Timer::Constructor();
```

| Constructor | Description |
|-------------|-------------|
| `Constructor()` | Create stopped timer |

### Control

| Method | Returns | Description |
|--------|---------|-------------|
| `start()` | `None` | Start timing |
| `stop()` | `None` | Stop timing |
| `reset()` | `None` | Reset to zero |

### Measurement

| Method | Returns | Description |
|--------|---------|-------------|
| `elapsed()` | `Integer^` | Elapsed milliseconds |
| `isRunning()` | `Bool^` | True if currently timing |

### Example

```xxml
Instantiate Time::Timer^ As <timer> = Time::Timer::Constructor();

// Time an operation
Run timer.start();

// ... do some work ...
For (Integer <i> = 0 .. 1000000) -> {
    // Simulate work
}

Run timer.stop();

Instantiate Integer^ As <ms> = timer.elapsed();
Run Console::printLine(String::Constructor("Operation took ").append(ms.toString()).append(String::Constructor("ms")));

// Reset and reuse
Run timer.reset();
Run timer.start();
// ... more work ...
```

---

## Complete Example

```xxml
#import Language::Core;
#import Language::Time;
#import Language::System;

[ Entrypoint
    {
        // Current time
        Instantiate Time::DateTime^ As <now> = Time::DateTime::Constructor();
        Run Console::printLine(String::Constructor("Current time: ").append(now.toString()));

        // Calculate future date
        Instantiate Time::DateTime^ As <meetingTime> = now.addHours(Integer::Constructor(2));
        Run Console::printLine(String::Constructor("Meeting in 2 hours: ").append(meetingTime.toString()));

        // Timer example
        Run Console::printLine(String::Constructor("Starting benchmark..."));

        Instantiate Time::Timer^ As <timer> = Time::Timer::Constructor();
        Run timer.start();

        // Simulate work
        Instantiate Integer^ As <sum> = Integer::Constructor(0);
        For (Integer <i> = 0 .. 100000) -> {
            Set sum = sum.add(i);
        }

        Run timer.stop();

        Run Console::printLine(String::Constructor("Sum: ").append(sum.toString()));
        Run Console::printLine(String::Constructor("Time: ").append(timer.elapsed().toString()).append(String::Constructor("ms")));

        // Calculate time difference
        Instantiate Time::DateTime^ As <later> = Time::DateTime::Constructor();
        Instantiate Integer^ As <diff> = later.difference(now);
        Run Console::printLine(String::Constructor("Total elapsed: ").append(diff.toString()).append(String::Constructor("ms")));

        Exit(0);
    }
]
```

---

## See Also

- [Console](CONSOLE.md) - `getTime()`, `getTimeMillis()`, `sleep()`
- [Core Types](CORE.md) - Integer
