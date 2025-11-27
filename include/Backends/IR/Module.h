#pragma once

#include "Backends/IR/Function.h"
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace XXML {
namespace Backends {
namespace IR {

// ============================================================================
// Module - Top-level container for an IR program
// ============================================================================

class Module {
public:
    explicit Module(const std::string& name);
    ~Module();

    // Non-copyable
    Module(const Module&) = delete;
    Module& operator=(const Module&) = delete;

    // ========== Module Identity ==========

    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }

    // ========== Target Information ==========

    const std::string& getTargetTriple() const { return targetTriple_; }
    void setTargetTriple(const std::string& triple) { targetTriple_ = triple; }

    const std::string& getDataLayout() const { return dataLayout_; }
    void setDataLayout(const std::string& layout) { dataLayout_ = layout; }

    // ========== Type Context ==========

    TypeContext& getContext() { return context_; }
    const TypeContext& getContext() const { return context_; }

    // ========== Struct Types ==========

    // Create a named struct type
    StructType* createStructType(const std::string& name);

    // Get a named struct type (returns nullptr if not found)
    StructType* getStructType(const std::string& name) const;

    // Get all struct types
    const std::map<std::string, StructType*>& getStructTypes() const { return structTypes_; }

    // ========== Global Variables ==========

    // Add a global variable (takes ownership)
    GlobalVariable* addGlobalVariable(std::unique_ptr<GlobalVariable> gv);

    // Create and add a global variable
    GlobalVariable* createGlobalVariable(Type* valueType, const std::string& name,
                                         GlobalValue::Linkage linkage = GlobalValue::Linkage::External,
                                         Constant* initializer = nullptr);

    // Get a global variable by name
    GlobalVariable* getGlobalVariable(const std::string& name) const;

    // Get all global variables
    using GlobalListType = std::map<std::string, std::unique_ptr<GlobalVariable>>;
    using global_iterator = GlobalListType::iterator;
    using const_global_iterator = GlobalListType::const_iterator;

    global_iterator global_begin() { return globals_.begin(); }
    global_iterator global_end() { return globals_.end(); }
    const_global_iterator global_begin() const { return globals_.begin(); }
    const_global_iterator global_end() const { return globals_.end(); }

    size_t global_size() const { return globals_.size(); }
    bool global_empty() const { return globals_.empty(); }
    const GlobalListType& getGlobals() const { return globals_; }

    // ========== Functions ==========

    // Add a function (takes ownership)
    Function* addFunction(std::unique_ptr<Function> func);

    // Create and add a function
    Function* createFunction(FunctionType* funcType, const std::string& name,
                            GlobalValue::Linkage linkage = GlobalValue::Linkage::External);

    // Get a function by name
    Function* getFunction(const std::string& name) const;

    // Get or create a function (useful for declarations)
    Function* getOrInsertFunction(const std::string& name, FunctionType* funcType);

    // Get all functions
    using FunctionListType = std::map<std::string, std::unique_ptr<Function>>;
    using iterator = FunctionListType::iterator;
    using const_iterator = FunctionListType::const_iterator;

    iterator begin() { return functions_.begin(); }
    iterator end() { return functions_.end(); }
    const_iterator begin() const { return functions_.begin(); }
    const_iterator end() const { return functions_.end(); }

    size_t size() const { return functions_.size(); }
    bool empty() const { return functions_.empty(); }
    const FunctionListType& getFunctions() const { return functions_; }

    // ========== String Literals ==========

    // Get or create a global string literal constant
    // Returns a GlobalVariable with the string content
    GlobalVariable* getOrCreateStringLiteral(const std::string& value);

    // Get the string literal index (for naming: @.str.0, @.str.1, etc.)
    size_t getStringLiteralCount() const { return stringLiterals_.size(); }

    // ========== External Declarations ==========

    // Declare an external function (creates a declaration if not exists)
    Function* declareFunction(const std::string& name, FunctionType* funcType);

    // Declare an external global variable
    GlobalVariable* declareGlobalVariable(const std::string& name, Type* type);

    // ========== Verification ==========

    // Verify the entire module
    // Returns true if valid, false otherwise
    // If errorMessage is provided, fills it with error description
    bool verify(std::string* errorMessage = nullptr) const;

    // ========== Utility ==========

    // Print the module (for debugging)
    void print() const;

    // Dump to string
    std::string toString() const;

private:
    std::string name_;
    std::string targetTriple_;
    std::string dataLayout_;

    TypeContext context_;

    // Named struct types
    std::map<std::string, StructType*> structTypes_;

    // Global variables
    GlobalListType globals_;

    // Functions
    FunctionListType functions_;

    // String literals (value -> global variable name)
    std::map<std::string, GlobalVariable*> stringLiterals_;
    size_t stringLiteralCounter_ = 0;
};

} // namespace IR
} // namespace Backends
} // namespace XXML
