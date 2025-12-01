#pragma once

#include "Backends/LLVMIR/TypedValue.h"
#include "Backends/LLVMIR/TypedInstructions.h"
#include <vector>
#include <map>
#include <memory>
#include <string>
#include <string_view>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// Array Type (needed for strings and arrays) - must be before Constants
// ============================================================================

class ArrayType final : public Type {
public:
    ArrayType(Type* elementType, size_t numElements)
        : Type(TypeKind::Array), elementType_(elementType), numElements_(numElements) {}

    Type* getElementType() const { return elementType_; }
    size_t getNumElements() const { return numElements_; }

    size_t getSizeInBits() const override {
        return elementType_->getSizeInBits() * numElements_;
    }
    size_t getAlignmentInBytes() const override {
        return elementType_->getAlignmentInBytes();
    }
    std::string toLLVM() const override {
        return "[" + std::to_string(numElements_) + " x " + elementType_->toLLVM() + "]";
    }

    static bool classof(const Type* t) { return t->getKind() == TypeKind::Array; }

private:
    Type* elementType_;
    size_t numElements_;
};

// ============================================================================
// Constants
// ============================================================================

class Constant : public Value {
public:
    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Constant;
    }

protected:
    Constant(Type* type) : Value(ValueKind::Constant, type) {}
};

class ConstantInt final : public Constant {
public:
    ConstantInt(IntegerType* type, int64_t value)
        : Constant(type), value_(value) {}

    int64_t getValue() const { return value_; }
    IntegerType* getIntType() const { return static_cast<IntegerType*>(getType()); }

    IntValue toTypedValue() const {
        return IntValue(const_cast<ConstantInt*>(this));
    }

private:
    int64_t value_;
};

class ConstantFP final : public Constant {
public:
    ConstantFP(FloatType* type, double value)
        : Constant(type), value_(value) {}

    double getValue() const { return value_; }
    FloatType* getFloatType() const { return static_cast<FloatType*>(getType()); }

    FloatValue toTypedValue() const {
        return FloatValue(const_cast<ConstantFP*>(this));
    }

private:
    double value_;
};

class ConstantNull final : public Constant {
public:
    explicit ConstantNull(PointerType* type) : Constant(type) {}

    PtrValue toTypedValue() const {
        return PtrValue(const_cast<ConstantNull*>(this));
    }
};

class ConstantString final : public Constant {
public:
    ConstantString(ArrayType* type, std::string value)
        : Constant(type), value_(std::move(value)) {}

    const std::string& getValue() const { return value_; }

private:
    std::string value_;
};

// ============================================================================
// Struct Type
// ============================================================================

class StructType final : public Type {
public:
    struct Field {
        std::string name;
        Type* type;
        size_t offset;
    };

    explicit StructType(std::string_view name = "")
        : Type(TypeKind::Struct), name_(name) {}

    const std::string& getName() const { return name_; }
    bool hasName() const { return !name_.empty(); }
    bool isPacked() const { return isPacked_; }
    bool isOpaque() const { return !hasBody_; }

    const std::vector<Field>& getFields() const { return fields_; }
    size_t getNumFields() const { return fields_.size(); }
    Type* getFieldType(size_t index) const {
        return index < fields_.size() ? fields_[index].type : nullptr;
    }

    void setBody(std::vector<Type*> types, bool isPacked = false);

    size_t getSizeInBits() const override { return totalSize_ * 8; }
    size_t getAlignmentInBytes() const override {
        if (isPacked_ || fields_.empty()) return 1;
        size_t maxAlign = 1;
        for (const auto& field : fields_) {
            maxAlign = std::max(maxAlign, field.type->getAlignmentInBytes());
        }
        return maxAlign;
    }
    std::string toLLVM() const override {
        if (hasName()) return "%" + name_;
        // Literal struct
        std::string result = isPacked_ ? "<{" : "{";
        for (size_t i = 0; i < fields_.size(); ++i) {
            if (i > 0) result += ", ";
            result += fields_[i].type->toLLVM();
        }
        result += isPacked_ ? "}>" : "}";
        return result;
    }

    static bool classof(const Type* t) { return t->getKind() == TypeKind::Struct; }

private:
    std::string name_;
    std::vector<Field> fields_;
    bool isPacked_ = false;
    bool hasBody_ = false;
    size_t totalSize_ = 0;
};

// ============================================================================
// Function Type
// ============================================================================

class FunctionType final : public Type {
public:
    FunctionType(Type* returnType, std::vector<Type*> paramTypes, bool isVarArg = false)
        : Type(TypeKind::Function), returnType_(returnType),
          paramTypes_(std::move(paramTypes)), isVarArg_(isVarArg) {}

    Type* getReturnType() const { return returnType_; }
    const std::vector<Type*>& getParamTypes() const { return paramTypes_; }
    size_t getNumParams() const { return paramTypes_.size(); }
    Type* getParamType(size_t index) const {
        return index < paramTypes_.size() ? paramTypes_[index] : nullptr;
    }
    bool isVarArg() const { return isVarArg_; }

    size_t getSizeInBits() const override { return 0; }
    size_t getAlignmentInBytes() const override { return 0; }
    std::string toLLVM() const override {
        std::string result = returnType_->toLLVM() + " (";
        for (size_t i = 0; i < paramTypes_.size(); ++i) {
            if (i > 0) result += ", ";
            result += paramTypes_[i]->toLLVM();
        }
        if (isVarArg_) {
            if (!paramTypes_.empty()) result += ", ";
            result += "...";
        }
        result += ")";
        return result;
    }

    static bool classof(const Type* t) { return t->getKind() == TypeKind::Function; }

private:
    Type* returnType_;
    std::vector<Type*> paramTypes_;
    bool isVarArg_;
};

// ============================================================================
// Function Argument
// ============================================================================

class Argument final : public Value {
public:
    Argument(Type* type, std::string_view name, unsigned index)
        : Value(ValueKind::Argument, type, name), index_(index) {}

    unsigned getIndex() const { return index_; }

    // Get typed value based on actual type
    AnyValue toTypedValue() const { return AnyValue(const_cast<Argument*>(this)); }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Argument;
    }

private:
    unsigned index_;
};

// ============================================================================
// Basic Block
// ============================================================================

class BasicBlock final : public Value {
public:
    explicit BasicBlock(std::string_view name = "")
        : Value(ValueKind::BasicBlock, nullptr, name) {}

    // Instruction list management
    void appendInstruction(Instruction* inst);
    void insertBefore(Instruction* inst, Instruction* before);

    Instruction* getFirstInstruction() const { return first_; }
    Instruction* getLastInstruction() const { return last_; }
    TerminatorInst* getTerminator() const;

    bool empty() const { return first_ == nullptr; }
    size_t size() const;

    // Parent function
    Function* getParent() const { return parent_; }
    void setParent(Function* func) { parent_ = func; }

    // Iteration support
    class iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Instruction*;
        using difference_type = std::ptrdiff_t;
        using pointer = Instruction**;
        using reference = Instruction*&;

        iterator(Instruction* inst) : current_(inst) {}

        Instruction* operator*() const { return current_; }
        iterator& operator++() {
            if (current_) current_ = current_->getNext();
            return *this;
        }
        bool operator==(const iterator& other) const { return current_ == other.current_; }
        bool operator!=(const iterator& other) const { return current_ != other.current_; }

    private:
        Instruction* current_;
    };

    iterator begin() const { return iterator(first_); }
    iterator end() const { return iterator(nullptr); }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::BasicBlock;
    }

private:
    Instruction* first_ = nullptr;
    Instruction* last_ = nullptr;
    Function* parent_ = nullptr;
};

// ============================================================================
// Global Variable
// ============================================================================

class GlobalVariable final : public Value {
public:
    enum class Linkage {
        External,
        Internal,
        Private,
        Common,
        Weak
    };

    GlobalVariable(Type* valueType, PointerType* ptrType, std::string_view name,
                   Linkage linkage = Linkage::External, Constant* initializer = nullptr)
        : Value(ValueKind::GlobalVariable, ptrType, name),
          valueType_(valueType), linkage_(linkage), initializer_(initializer) {}

    Type* getValueType() const { return valueType_; }
    Linkage getLinkage() const { return linkage_; }
    bool hasInitializer() const { return initializer_ != nullptr; }
    Constant* getInitializer() const { return initializer_; }
    void setInitializer(Constant* init) { initializer_ = init; }

    bool isConstant() const { return isConstant_; }
    void setConstant(bool c) { isConstant_ = c; }

    PtrValue toTypedValue() const {
        return PtrValue(const_cast<GlobalVariable*>(this));
    }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::GlobalVariable;
    }

private:
    Type* valueType_;
    Linkage linkage_;
    Constant* initializer_;
    bool isConstant_ = false;
};

// ============================================================================
// Function
// ============================================================================

class Function final : public Value {
public:
    using Linkage = GlobalVariable::Linkage;

    enum class CallingConv {
        C,
        Fast,
        Cold,
        X86_StdCall,
        X86_FastCall,
        Win64
    };

    Function(FunctionType* funcType, PointerType* ptrType, std::string_view name,
             Linkage linkage = Linkage::External)
        : Value(ValueKind::Function, ptrType, name),
          funcType_(funcType), linkage_(linkage) {
        // Create arguments
        for (size_t i = 0; i < funcType->getNumParams(); ++i) {
            args_.push_back(std::make_unique<Argument>(
                funcType->getParamType(i),
                "arg" + std::to_string(i),
                static_cast<unsigned>(i)
            ));
        }
    }

    // Type information
    FunctionType* getFunctionType() const { return funcType_; }
    Type* getReturnType() const { return funcType_->getReturnType(); }
    size_t getNumParams() const { return funcType_->getNumParams(); }

    // Arguments
    const std::vector<std::unique_ptr<Argument>>& getArgs() const { return args_; }
    Argument* getArg(size_t index) const {
        return index < args_.size() ? args_[index].get() : nullptr;
    }

    // Basic blocks
    BasicBlock* createBasicBlock(std::string_view name = "");
    const std::vector<std::unique_ptr<BasicBlock>>& getBasicBlocks() const { return blocks_; }
    BasicBlock* getEntryBlock() const {
        return blocks_.empty() ? nullptr : blocks_[0].get();
    }
    size_t getNumBlocks() const { return blocks_.size(); }

    // Linkage and attributes
    Linkage getLinkage() const { return linkage_; }
    CallingConv getCallingConv() const { return callingConv_; }
    void setCallingConv(CallingConv cc) { callingConv_ = cc; }

    bool isDeclaration() const { return blocks_.empty(); }
    bool isDefinition() const { return !blocks_.empty(); }

    PtrValue toTypedValue() const {
        return PtrValue(const_cast<Function*>(this));
    }

    static bool classof(const Value* v) {
        return v->getValueKind() == ValueKind::Function;
    }

private:
    FunctionType* funcType_;
    Linkage linkage_;
    CallingConv callingConv_ = CallingConv::C;
    std::vector<std::unique_ptr<Argument>> args_;
    std::vector<std::unique_ptr<BasicBlock>> blocks_;
};

// ============================================================================
// Type Context (Type Factory with Interning)
// ============================================================================

class TypeContext {
public:
    TypeContext();

    // Primitive types
    VoidType* getVoidTy();
    IntegerType* getInt1Ty();
    IntegerType* getInt8Ty();
    IntegerType* getInt16Ty();
    IntegerType* getInt32Ty();
    IntegerType* getInt64Ty();
    IntegerType* getIntNTy(unsigned bitWidth);

    FloatType* getFloatTy();
    FloatType* getDoubleTy();

    PointerType* getPtrTy(unsigned addressSpace = 0);

    // Composite types
    ArrayType* getArrayTy(Type* elementType, size_t numElements);
    StructType* createStructTy(std::string_view name);
    StructType* getNamedStructTy(std::string_view name) const;
    FunctionType* getFunctionTy(Type* returnType, std::vector<Type*> paramTypes,
                                bool isVarArg = false);

private:
    std::unique_ptr<VoidType> voidTy_;
    std::map<unsigned, std::unique_ptr<IntegerType>> intTypes_;
    std::map<FloatType::Precision, std::unique_ptr<FloatType>> floatTypes_;
    std::map<unsigned, std::unique_ptr<PointerType>> ptrTypes_;
    std::map<std::string, std::unique_ptr<StructType>> namedStructs_;
    std::vector<std::unique_ptr<ArrayType>> arrayTypes_;
    std::vector<std::unique_ptr<FunctionType>> funcTypes_;
};

// ============================================================================
// Module
// ============================================================================

class Module {
public:
    explicit Module(std::string_view name);

    const std::string& getName() const { return name_; }

    // Type context
    TypeContext& getContext() { return context_; }
    const TypeContext& getContext() const { return context_; }

    // Target info
    const std::string& getTargetTriple() const { return targetTriple_; }
    void setTargetTriple(std::string_view triple) { targetTriple_ = std::string(triple); }

    const std::string& getDataLayout() const { return dataLayout_; }
    void setDataLayout(std::string_view layout) { dataLayout_ = std::string(layout); }

    // Struct types
    StructType* createStruct(std::string_view name);
    StructType* getStruct(std::string_view name) const;
    const std::map<std::string, StructType*>& getStructs() const { return structs_; }

    // Global variables
    GlobalVariable* createGlobal(Type* valueType, std::string_view name,
                                 GlobalVariable::Linkage linkage = GlobalVariable::Linkage::External,
                                 Constant* initializer = nullptr);
    GlobalVariable* getGlobal(std::string_view name) const;
    const std::map<std::string, std::unique_ptr<GlobalVariable>>& getGlobals() const {
        return globals_;
    }

    // Functions
    Function* createFunction(FunctionType* funcType, std::string_view name,
                             Function::Linkage linkage = Function::Linkage::External);
    Function* getFunction(std::string_view name) const;
    const std::map<std::string, std::unique_ptr<Function>>& getFunctions() const {
        return functions_;
    }

    // String literals
    GlobalVariable* getOrCreateStringLiteral(std::string_view value);

    // Constants factory
    ConstantInt* getConstantInt(IntegerType* type, int64_t value);
    ConstantFP* getConstantFP(FloatType* type, double value);
    ConstantNull* getConstantNull(PointerType* type);

private:
    std::string name_;
    std::string targetTriple_;
    std::string dataLayout_;

    TypeContext context_;

    std::map<std::string, StructType*> structs_;
    std::map<std::string, std::unique_ptr<GlobalVariable>> globals_;
    std::map<std::string, std::unique_ptr<Function>> functions_;
    std::map<std::string, GlobalVariable*> stringLiterals_;

    // Constants storage
    std::vector<std::unique_ptr<ConstantInt>> constantInts_;
    std::vector<std::unique_ptr<ConstantFP>> constantFPs_;
    std::vector<std::unique_ptr<ConstantNull>> constantNulls_;
    std::vector<std::unique_ptr<ConstantString>> constantStrings_;

    size_t stringCounter_ = 0;
};

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
