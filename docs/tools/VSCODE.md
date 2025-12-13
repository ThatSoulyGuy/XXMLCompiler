# VS Code Extension for XXML

The XXML VS Code extension provides full IDE support including syntax highlighting, diagnostics, code navigation, and ownership visualization.

**Location:** `tools/vscode-xxml/`

---

## Table of Contents

1. [Features](#features)
2. [Installation](#installation)
3. [Configuration](#configuration)
4. [Ownership Visualization](#ownership-visualization)
5. [Language Server](#language-server)
6. [Building from Source](#building-from-source)

---

## Features

### Syntax Highlighting

Full syntax highlighting for XXML source files (`.XXML`):

- Keywords (`Class`, `Method`, `Property`, `Instantiate`, etc.)
- Types and type parameters
- Ownership modifiers (`^`, `&`, `%`)
- Strings and comments
- Annotations (`@Derive`, `@Test`, etc.)

### Language Server Integration

Real-time IDE features powered by the XXML Language Server:

| Feature | Description |
|---------|-------------|
| **Diagnostics** | Syntax and semantic errors as you type |
| **Go to Definition** | Jump to class, method, or property definitions |
| **Hover Information** | Type info and documentation on hover |
| **Code Completion** | IntelliSense for keywords and symbols |
| **Find References** | Find all usages of a symbol |

### Ownership Visualization

Visual highlighting of ownership modifiers with distinct colors:

| Modifier | Color | Meaning |
|----------|-------|---------|
| `^` | Green | **Owned** - Unique ownership, responsible for lifetime |
| `&` | Blue | **Reference** - Borrowed, cannot outlive owner |
| `%` | Orange | **Copy** - Independent bitwise copy |

---

## Installation

### From VSIX Package

1. Build the extension package:
   ```bash
   cd tools/vscode-xxml
   npm install
   npm run package
   ```

2. Install in VS Code:
   ```bash
   code --install-extension xxml-language-0.1.0.vsix
   ```

### For Development

1. Install dependencies:
   ```bash
   cd tools/vscode-xxml
   npm install
   ```

2. Compile TypeScript:
   ```bash
   npm run compile
   ```

3. Press **F5** in VS Code to launch the Extension Development Host.

---

## Configuration

Access settings via **File > Preferences > Settings** and search for "XXML".

### Available Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `xxml.languageServer.path` | `""` | Path to `xxml-lsp` executable. Leave empty to search in PATH. |
| `xxml.languageServer.enabled` | `true` | Enable/disable the language server |
| `xxml.ownershipVisualization.enabled` | `true` | Show ownership modifier highlighting |

### Example settings.json

```json
{
    "xxml.languageServer.path": "C:/path/to/xxml-lsp.exe",
    "xxml.languageServer.enabled": true,
    "xxml.ownershipVisualization.enabled": true
}
```

---

## Ownership Visualization

The extension highlights ownership modifiers inline to help visualize memory management:

```xxml
Property <data> Types Integer^;    // ^ highlighted in green
Method <process> Returns None Parameters (
    Parameter <input> Types String&    // & highlighted in blue
) Do { ... }
```

### Enabling/Disabling

Toggle via settings:
```json
{
    "xxml.ownershipVisualization.enabled": false
}
```

Or use the command palette:
1. Press `Ctrl+Shift+P` (or `Cmd+Shift+P` on Mac)
2. Search for "XXML: Toggle Ownership Visualization"

---

## Language Server

The XXML Language Server (`xxml-lsp`) provides IDE intelligence.

### Requirements

The language server must be installed for full functionality. Without it, only syntax highlighting is available.

### LSP Capabilities

| Capability | Status |
|------------|--------|
| `textDocument/didOpen` | Supported |
| `textDocument/didChange` | Supported |
| `textDocument/didClose` | Supported |
| `textDocument/completion` | Supported |
| `textDocument/hover` | Supported |
| `textDocument/definition` | Supported |
| `textDocument/references` | Supported |
| `textDocument/publishDiagnostics` | Supported |

### Troubleshooting

**"Language server not found"**
- Verify `xxml-lsp.exe` is in PATH or configure `xxml.languageServer.path`
- Check the Output panel (View > Output > XXML Language Server)

**"No diagnostics showing"**
- Ensure `xxml.languageServer.enabled` is `true`
- Check that the language server started successfully
- Look for errors in the Output panel

---

## Building from Source

### Build the Language Server

```bash
cd /path/to/XXMLCompiler
cmake --preset release
cmake --build --preset release --target xxml-lsp
```

The server is built at `build/release/bin/xxml-lsp.exe`.

### Build the VS Code Extension

```bash
cd tools/vscode-xxml

# Install dependencies
npm install

# Compile TypeScript
npm run compile

# Package as VSIX
npm run package
```

### Project Structure

```
tools/vscode-xxml/
├── src/
│   └── extension.ts          # Extension entry point
├── syntaxes/
│   └── xxml.tmLanguage.json  # TextMate grammar for highlighting
├── language-configuration.json # Bracket matching, comments
├── package.json              # Extension manifest
└── tsconfig.json             # TypeScript configuration
```

---

## File Associations

The extension automatically associates with `.XXML` files. To add additional patterns:

```json
{
    "files.associations": {
        "*.xxml": "xxml",
        "*.XXML": "xxml"
    }
}
```

---

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| `F12` | Go to Definition |
| `Shift+F12` | Find All References |
| `Ctrl+Space` | Trigger Completion |
| `Ctrl+Shift+Space` | Parameter Hints |
| `F2` | Rename Symbol |

---

## Known Limitations

- **Rename not fully implemented** - Rename across files may not work correctly
- **No debugging support** - The extension does not include a debug adapter
- **Limited refactoring** - Extract method/variable not supported

---

## See Also

- [CLI Reference](CLI.md) - Compiler command-line options
- [Architecture](ARCHITECTURE.md) - Compiler and LSP internals
- [Language Syntax](../language/SYNTAX.md) - XXML language reference
