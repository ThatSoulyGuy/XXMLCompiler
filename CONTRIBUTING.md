# Contributing to XXML Compiler

Thank you for your interest in contributing to the XXML Compiler project! This document provides guidelines and information for contributors.

## Code of Conduct

- Be respectful and inclusive
- Provide constructive feedback
- Focus on what is best for the community
- Show empathy towards other community members

## Getting Started

### Prerequisites

- C++17 compatible compiler
- CMake 3.15+
- Git
- Basic understanding of compiler theory

### Setting Up Development Environment

1. **Clone the repository:**
   ```bash
   git clone <repository-url>
   cd XXMLCompiler
   ```

2. **Build the project:**
   ```bash
   mkdir build && cd build
   cmake ..
   cmake --build .
   ```

3. **Run tests:**
   ```bash
   ctest
   ```

## Project Structure

```
XXMLCompiler/
├── include/        # Public headers
├── src/            # Implementation files
├── runtime/        # XXML standard library
├── docs/           # Documentation
├── tests/          # Test files
└── CMakeLists.txt  # Build configuration
```

## Code Style Guidelines

### C++ Style

- **Indentation**: 4 spaces (no tabs)
- **Braces**: Opening brace on same line
- **Naming Conventions**:
  - Classes: `PascalCase` (e.g., `TokenType`, `Parser`)
  - Methods: `camelCase` (e.g., `parseExpression`)
  - Variables: `camelCase` (e.g., `currentToken`)
  - Constants: `UPPER_SNAKE_CASE` (e.g., `MAX_TOKENS`)
  - Namespaces: `PascalCase` (e.g., `Lexer`, `Parser`)

### Example:

```cpp
namespace XXML {
namespace Parser {

class Parser {
private:
    std::vector<Token> tokens;
    size_t currentPosition;

public:
    Parser(const std::vector<Token>& toks)
        : tokens(toks), currentPosition(0) {}

    std::unique_ptr<Expression> parseExpression() {
        // Implementation
    }
};

} // namespace Parser
} // namespace XXML
```

### XXML Style

- **Indentation**: 1 tab per nesting level
- **Brackets**: Each `[` on its own line
- **Comments**: Use `//` for comments

## Development Workflow

### 1. Create a Branch

```bash
git checkout -b feature/your-feature-name
```

Branch naming:
- `feature/` - New features
- `bugfix/` - Bug fixes
- `refactor/` - Code refactoring
- `docs/` - Documentation updates

### 2. Make Changes

- Write clean, documented code
- Add comments for complex logic
- Update documentation if needed
- Write tests for new features

### 3. Test Your Changes

```bash
# Build
cmake --build build

# Run tests
cd build
ctest

# Test manually
./bin/xxml ../Test.XXML output.cpp
g++ -std=c++17 output.cpp -o test
./test
```

### 4. Commit Changes

```bash
git add .
git commit -m "feat: add support for while loops"
```

Commit message format:
```
<type>: <subject>

<body>

<footer>
```

Types:
- `feat`: New feature
- `fix`: Bug fix
- `docs`: Documentation
- `refactor`: Code refactoring
- `test`: Add tests
- `chore`: Build/tooling changes

### 5. Push and Create Pull Request

```bash
git push origin feature/your-feature-name
```

Then create a pull request on GitHub with:
- Clear description of changes
- Reference to any related issues
- Screenshots if UI changes
- Test results

## Testing

### Unit Tests

Create tests in `tests/unit/`:

```cpp
#include "../../include/Lexer/Lexer.h"
#include <cassert>

void testIntegerLiteral() {
    XXML::Common::ErrorReporter reporter;
    XXML::Lexer::Lexer lexer("42i", "test.xxml", reporter);
    auto tokens = lexer.tokenize();

    assert(tokens[0].type == XXML::Lexer::TokenType::IntegerLiteral);
    assert(tokens[0].intValue == 42);
}
```

### Integration Tests

Create `.xxml` test files in `tests/`:

```xxml
// tests/test_basic.xxml
[ Entrypoint
    {
        Exit(0);
    }
]
```

## Areas for Contribution

### High Priority

- [ ] Improve error messages and suggestions
- [ ] Add more unit tests
- [ ] Optimize code generation
- [ ] Extend standard library

### Medium Priority

- [ ] Add IDE support (LSP)
- [ ] Implement generic types
- [ ] Add module system
- [ ] Create more examples

### Low Priority

- [ ] Alternative backends (LLVM)
- [ ] Package manager
- [ ] Debugger integration
- [ ] Performance profiling tools

## Documentation

### Code Documentation

Use clear comments:

```cpp
/**
 * Parses a method declaration.
 *
 * Syntax: Method <name> Returns Type Parameters (...) -> { body }
 *
 * @return Unique pointer to MethodDecl AST node
 */
std::unique_ptr<MethodDecl> parseMethod();
```

### User Documentation

Update relevant documentation:
- `README.md` - Overview and quick start
- `docs/LANGUAGE_SPEC.md` - Language features
- `docs/ARCHITECTURE.md` - Compiler internals

## Pull Request Guidelines

### Before Submitting

- [ ] Code builds without errors
- [ ] All tests pass
- [ ] New tests added for new features
- [ ] Documentation updated
- [ ] Code follows style guidelines
- [ ] Commit messages follow convention

### PR Description Template

```markdown
## Description
Brief description of changes

## Type of Change
- [ ] Bug fix
- [ ] New feature
- [ ] Breaking change
- [ ] Documentation update

## Testing
Describe tests performed

## Checklist
- [ ] Code compiles
- [ ] Tests pass
- [ ] Documentation updated
- [ ] Style guidelines followed
```

## Reporting Issues

### Bug Reports

Include:
- XXML Compiler version
- Operating system
- Minimal reproduction code
- Expected vs actual behavior
- Error messages

### Feature Requests

Include:
- Clear description of feature
- Use cases
- Example syntax (if language feature)
- Why this improves the language/compiler

## Questions?

- Open an issue with the `question` label
- Check existing documentation
- Review similar issues

## License

By contributing, you agree that your contributions will be licensed under the project's MIT License.

---

Thank you for contributing to XXML Compiler!
