# XXML Template Implementation

## Status: PARTIAL - Code Generation Remaining

## Completed Features

### 1. Lexer ✅
- Added `Constrains` keyword (TokenType::Constrains)
- Added `Static` keyword (TokenType::Static)
- Both registered in keyword map and tokenTypeToString

### 2. AST ✅
- Added `TemplateParameter` struct to hold template parameter info:
  ```cpp
  struct TemplateParameter {
      std::string name;                      // e.g., "T"
      std::vector<std::string> constraints;  // e.g., ["Integer", "String"] or empty for None
      Common::SourceLocation location;
  };
  ```

- Modified `ClassDecl` to include template parameters:
  ```cpp
  std::vector<TemplateParameter> templateParams;
  ```

- Modified `TypeRef` to include template arguments:
  ```cpp
  std::vector<std::string> templateArgs;  // e.g., ["Integer"] for List<Integer>
  ```

### 3. Parser ✅
- Added `parseTemplateParameters()` method
  - Parses syntax: `<T Constrains None>` or `<T Constrains Integer | String>`
  - Supports multiple template parameters separated by commas
  - Constraints separated by `|` (pipe)

- Modified `parseClass()` to parse template parameters after class name
  - Syntax: `[ Class <List> <T Constrains None> Final Extends None`

- Modified `parseTypeRef()` to parse template instantiations
  - Parses: `List<Integer>^`, `HashMap<String, Integer>^`
  - Supports multiple template arguments separated by commas

### 4. Semantic Analyzer ✅
- Added template tracking data structures:
  ```cpp
  std::unordered_map<std::string, Parser::ClassDecl*> templateClasses;
  std::set<std::pair<std::string, std::vector<std::string>>> templateInstantiations;
  ```

- Modified `visit(ClassDecl&)` to register template classes
- Modified `visit(TypeRef&)` to record template instantiations
- Added `recordTemplateInstantiation()` to validate template argument count
- Added public getters for code generator to access template data

## Syntax Examples

### Template Class Definition
```xxml
[ Namespace <Collections>
	[ Class <List> <T Constrains None> Final Extends None
		[ Private <>
			Property <data> Types NativeType<"ptr">;
		]

		[ Public <>
			Method <add> Returns None Parameters (Parameter <value> Types T^) Do {
				// Use T as a type
			}
		]
	]
]
```

### Multiple Constraints
```xxml
[ Class <Container> <T Constrains Integer | String | Bool> Final Extends None
	// T can only be Integer, String, or Bool
]
```

### Multiple Template Parameters
```xxml
[ Class <Pair> <T Constrains None, U Constrains None> Final Extends None
	[ Private <>
		Property <first> Types T^;
		Property <second> Types U^;
	]
]
```

### Template Instantiation
```xxml
Instantiate Collections::List<Integer>^ As <intList> = Collections::List<Integer>::Constructor();
Instantiate Collections::List<String>^ As <strList> = Collections::List<String>::Constructor();
Instantiate Collections::Pair<Integer, String>^ As <pair> = Collections::Pair<Integer, String>::Constructor();
```

## Remaining Work: Code Generation ⏳

### Required Changes to CodeGenerator

1. **Access Semantic Analysis Results**
   - Pass SemanticAnalyzer reference to CodeGenerator or extract template data
   - Get `templateClasses` and `templateInstantiations`

2. **Template Instantiation Logic**
   - For each entry in `templateInstantiations`:
     - Clone the template class AST
     - Perform type substitution (replace T with actual type)
     - Generate code with mangled name

3. **Type Substitution**
   - Replace template parameter throughout class:
     - Property types: `Property <value> Types T^` → `Property <value> Types Integer^`
     - Method parameters: `Parameter <value> Types T^` → `Parameter <value> Types Integer^`
     - Return types if using template parameter
     - Local variable types
     - All type references

4. **Name Mangling**
   - `List<Integer>` → `List_Integer`
   - `List<String>` → `List_String`
   - `HashMap<String, Integer>` → `HashMap_String_Integer`
   - Handle qualified names: `Collections::List<Integer>` → `Collections::List_Integer`

5. **Code Generation Strategy**
   - Option A: Generate all instantiations before generating user code
   - Option B: Generate instantiations as encountered
   - Recommended: Generate at the end after processing all user code

6. **Handling in convertType()**
   - Update `convertType()` to recognize template instantiations
   - Map `List<Integer>` to `List_Integer` in generated C++

### Implementation Approach

```cpp
// In CodeGenerator
void generateTemplateInstantiations(
    const std::set<std::pair<std::string, std::vector<std::string>>>& instantiations,
    const std::unordered_map<std::string, Parser::ClassDecl*>& templates)
{
    for (const auto& [templateName, args] : instantiations) {
        auto* templateClass = templates.at(templateName);

        // Clone class AST
        auto instantiatedClass = cloneClass(templateClass);

        // Build type substitution map
        std::unordered_map<std::string, std::string> typeMap;
        for (size_t i = 0; i < templateClass->templateParams.size(); ++i) {
            typeMap[templateClass->templateParams[i].name] = args[i];
        }

        // Substitute types throughout class
        substituteTypes(instantiatedClass, typeMap);

        // Generate mangled name
        std::string mangledName = mangleName(templateName, args);
        instantiatedClass->name = mangledName;

        // Generate code for instantiated class
        visit(*instantiatedClass);
    }
}

std::string mangleName(const std::string& templateName, const std::vector<std::string>& args) {
    std::string result = templateName;
    for (const auto& arg : args) {
        result += "_" + arg;
    }
    return result;
}
```

### Test Case

File: `TemplateTest.XXML`

Expected behavior:
1. Parser successfully parses template syntax
2. Semantic analyzer records:
   - Template class: `List` with parameter `T`
   - Instantiations: `List<Integer>`, `List<String>`
3. Code generator produces:
   - `List_Integer` class with `Integer` substituted for `T`
   - `List_String` class with `String` substituted for `T`
4. Generated C++ compiles and runs successfully

## Architecture Notes

- Templates are fully resolved at compile time (no C++ templates in output)
- Each unique instantiation gets its own concrete class
- Type substitution is syntactic (simple text replacement with validation)
- Constraints are validated during semantic analysis
- Template definitions cannot be in header files (must be in .XXML source)

## Future Enhancements

1. **Template Methods** - Methods that are themselves generic
2. **Template Constraints** - Proper interface/trait constraints
3. **Partial Specialization** - Different implementations for specific types
4. **Template Type Deduction** - Infer template arguments from usage
5. **Variadic Templates** - Variable number of template parameters
6. **Template Metaprogramming** - Compile-time computations

## Files Modified

### Lexer
- `include/Lexer/TokenType.h` - Added Constrains and Static keywords
- `src/Lexer/TokenType.cpp` - Registered keywords

### Parser/AST
- `include/Parser/AST.h` - Added TemplateParameter, modified ClassDecl and TypeRef
- `include/Parser/Parser.h` - Added parseTemplateParameters declaration
- `src/Parser/Parser.cpp` - Implemented template parsing

### Semantic
- `include/Semantic/SemanticAnalyzer.h` - Added template tracking
- `src/Semantic/SemanticAnalyzer.cpp` - Implemented template recording

### Code Generator (TODO)
- `include/CodeGen/CodeGenerator.h` - Need to add template instantiation methods
- `src/CodeGen/CodeGenerator.cpp` - Need to implement template code generation

## Conclusion

The foundation for templates is complete:
- ✅ Syntax parsing
- ✅ AST representation
- ✅ Semantic analysis
- ⏳ Code generation (remaining)

Once code generation is implemented, XXML will have full compile-time template support without relying on C++ templates in the generated code. This enables truly generic data structures in pure XXML!
