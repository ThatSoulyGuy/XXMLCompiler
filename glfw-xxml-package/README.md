# GLFW Bindings for XXML

A modern, type-safe wrapper for GLFW 3.4 in XXML.

## Contents

```
glfw-xxml/
  GLFW.XXML           - Main API file (copy to your Language folder)
  lib/
    glfw3.dll         - GLFW native library (must be in PATH or exe directory)
  examples/
    SafeAPIBasicTest.XXML  - Minimal example
    SafeAPITest.XXML       - Full featured example
  README.md           - This file
  GLFW_LICENSE.md     - GLFW library license (zlib/libpng)
```

## Installation

1. Copy `GLFW.XXML` to your compiler's `Language/` folder
2. Copy `lib/glfw3.dll` to either:
   - Your executable's directory, OR
   - A folder in your system PATH

## Quick Start

```xxml
#import Language::Core;
#import Language::GLFW;

[ Entrypoint
{
    // Create GLFW instance
    Instantiate GLFW^ As <glfw> = GLFW::Constructor();

    // Initialize
    Instantiate Bool^ As <ok> = glfw.init();
    If (ok == Bool::Constructor(false)) -> {
        Run Console::printLine(String::Constructor("Failed to initialize GLFW"));
        Exit(1);
    }

    // Create window
    Instantiate Window^ As <window> = Window::Constructor();
    Run window.create(
        Integer::Constructor(800),
        Integer::Constructor(600),
        String::Constructor("My GLFW Window")
    );

    // Game loop
    For (Integer <i> = 0 .. 1000) -> {
        Run glfw.pollEvents();

        Instantiate Bool^ As <closing> = window.shouldClose();
        If (closing == Bool::Constructor(true)) -> {
            Break;
        }

        // Check for escape key
        Instantiate Bool^ As <esc> = window.isKeyPressed(Key::Escape);
        If (esc == Bool::Constructor(true)) -> {
            Break;
        }

        Run window.swapBuffers();
    }

    // Cleanup
    Run window.close();
    Run glfw.terminate();
    Exit(0);
}]
```

## API Reference

### GLFW Class (Global Functions)

```xxml
Instantiate GLFW^ As <glfw> = GLFW::Constructor();
```

| Method | Description |
|--------|-------------|
| `init()` | Initialize GLFW. Returns `Bool^` |
| `terminate()` | Shutdown GLFW |
| `pollEvents()` | Process pending events |
| `waitEvents()` | Wait for events (blocks) |
| `getTime()` | Get time since init (returns `NativeType<"double">^`) |
| `setSwapInterval(interval)` | Set VSync (0=off, 1=on) |
| `rawMouseMotionSupported()` | Check raw mouse motion support |
| `isJoystickConnected(id)` | Check if joystick connected |
| `isGamepad(id)` | Check if joystick is gamepad |

### Window Class

```xxml
Instantiate Window^ As <window> = Window::Constructor();
Run window.create(width, height, title);
```

| Method | Description |
|--------|-------------|
| `create(width, height, title)` | Create standard window |
| `fullscreen(title)` | Create fullscreen window |
| `borderless(w, h, title)` | Create borderless window |
| `fixed(w, h, title)` | Create non-resizable window |
| `close()` | Destroy window |
| `shouldClose()` | Check if close requested |
| `requestClose()` | Request window close |
| `swapBuffers()` | Swap front/back buffers |
| `isKeyPressed(key)` | Check key state |
| `isMouseButtonPressed(btn)` | Check mouse button |
| `setCursor(cursor)` | Set custom cursor |
| `setClipboard(text)` | Set clipboard text |
| `getClipboard()` | Get clipboard text |

### Cursor Class

```xxml
Instantiate Cursor^ As <cursor> = Cursor::Constructor();
Run cursor.hand();  // Create hand cursor
```

| Factory Method | Description |
|----------------|-------------|
| `arrow()` | Standard arrow cursor |
| `ibeam()` | Text input cursor |
| `crosshair()` | Crosshair cursor |
| `hand()` | Hand/pointer cursor |
| `resizeH()` | Horizontal resize |
| `resizeV()` | Vertical resize |

### Key Constants

```xxml
Key::Escape, Key::Space, Key::W, Key::A, Key::S, Key::D
Key::Up, Key::Down, Key::Left, Key::Right
Key::Enter, Key::Tab, Key::Backspace
Key::LeftShift, Key::LeftControl, Key::LeftAlt
// ... and more (see GLFW.XXML for full list)
```

### Mouse Button Constants

```xxml
MouseButton::Left, MouseButton::Right, MouseButton::Middle
```

## License

- **XXML Bindings**: MIT License
- **GLFW Library**: zlib/libpng license (see GLFW_LICENSE.md)

## Requirements

- XXML Compiler v2.0+
- Windows x64 (glfw3.dll is Windows 64-bit)
