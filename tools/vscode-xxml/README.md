# XXML Language Support for VS Code

This extension provides language support for the XXML programming language.

## Features

- **Syntax Highlighting** - Full syntax highlighting for XXML files
- **Language Server** - Real-time diagnostics, go-to-definition, hover info
- **Ownership Visualization** - Colored annotations for ownership modifiers (^, &, %)
- **Code Completion** - IntelliSense for keywords and symbols

## Installation

### From VSIX

1. Build the extension: `npm run package`
2. Install: `code --install-extension xxml-language-0.1.0.vsix`

### For Development

1. Install dependencies: `npm install`
2. Compile: `npm run compile`
3. Press F5 in VS Code to launch Extension Development Host

## Requirements

- **xxml-lsp** - The XXML Language Server must be installed and in PATH, or configured via settings

## Extension Settings

| Setting | Default | Description |
|---------|---------|-------------|
| `xxml.languageServer.path` | `""` | Path to xxml-lsp executable |
| `xxml.languageServer.enabled` | `true` | Enable/disable language server |
| `xxml.ownershipVisualization.enabled` | `true` | Show ownership annotations |

## Ownership Visualization

The extension highlights ownership modifiers with distinct colors:

- **Green** (`^`) - Owned: unique ownership, responsible for lifetime
- **Blue** (`&`) - Reference: borrowed, cannot outlive owner
- **Orange** (`%`) - Copy: independent bitwise copy

## XXML Language Quick Reference

```xxml
#import Language::Core;

[ Class <Example> Final Extends None
    [ Private <>
        Property <value> Types Integer^;
    ]
    [ Public <>
        Constructor Parameters (Parameter <v> Types Integer^) -> {
            Set value = v;
        }
        Method <getValue> Returns Integer^ Parameters () Do {
            Return value;
        }
    ]
]
```

## Building the Language Server

```bash
cd /path/to/XXMLCompiler
cmake --preset release
cmake --build --preset release --target xxml-lsp
```

The server will be built at `build/release/bin/xxml-lsp.exe`.

## License

MIT
