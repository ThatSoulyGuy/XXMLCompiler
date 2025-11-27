#pragma once

#include "Backends/IR/Types.h"
#include <string>
#include <vector>
#include <list>
#include <memory>
#include <cstdint>
#include <cassert>

namespace XXML {
namespace Backends {
namespace IR {

// Forward declarations
class Value;
class User;
class Use;
class Constant;
class ConstantInt;
class ConstantFP;
class ConstantNull;
class ConstantUndef;
class ConstantZeroInit;
class ConstantString;
class ConstantArray;
class GlobalValue;
class GlobalVariable;
class Function;
class Argument;
class BasicBlock;
class Instruction;

// ============================================================================
// Use - Represents a use of a Value by a User
// ============================================================================

class Use {
public:
    Use() : value_(nullptr), user_(nullptr), operandNo_(0) {}
    Use(Value* value, User* user, unsigned operandNo)
        : value_(value), user_(user), operandNo_(operandNo) {}

    Value* getValue() const { return value_; }
    User* getUser() const { return user_; }
    unsigned getOperandNo() const { return operandNo_; }

    void setValue(Value* v);
    void setUser(User* u) { user_ = u; }

    // For use lists
    Use* getNext() const { return next_; }
    Use* getPrev() const { return prev_; }

private:
    friend class Value;
    friend class User;

    Value* value_;
    User* user_;
    unsigned operandNo_;

    // Linked list for value's use chain
    Use* next_ = nullptr;
    Use* prev_ = nullptr;
};

// ============================================================================
// Value - Base class for all values in the IR
// ============================================================================

class Value {
public:
    enum class Kind {
        // Constants
        ConstantInt,
        ConstantFP,
        ConstantNull,
        ConstantUndef,
        ConstantZeroInit,
        ConstantString,
        ConstantArray,

        // Global values
        GlobalVariable,
        Function,

        // Local values
        Argument,
        BasicBlock,

        // Instructions (many kinds, handled via Instruction::getOpcode())
        Instruction
    };

    virtual ~Value() = default;

    Kind getValueKind() const { return kind_; }
    Type* getType() const { return type_; }
    void setType(Type* type) { type_ = type; }

    const std::string& getName() const { return name_; }
    void setName(const std::string& name) { name_ = name; }
    bool hasName() const { return !name_.empty(); }

    // Use-def chain
    bool hasUses() const { return useListHead_ != nullptr; }
    size_t getNumUses() const;

    // Iterate over uses
    class use_iterator {
    public:
        use_iterator(Use* use) : current_(use) {}
        Use& operator*() const { return *current_; }
        Use* operator->() const { return current_; }
        use_iterator& operator++() { current_ = current_->next_; return *this; }
        bool operator!=(const use_iterator& other) const { return current_ != other.current_; }
    private:
        Use* current_;
    };

    use_iterator use_begin() { return use_iterator(useListHead_); }
    use_iterator use_end() { return use_iterator(nullptr); }

    // Replace all uses of this value with another value
    void replaceAllUsesWith(Value* newValue);

    // Check if this is a constant
    bool isConstant() const {
        return kind_ >= Kind::ConstantInt && kind_ <= Kind::ConstantArray;
    }

    // Check if this is a global value (function or global variable)
    bool isGlobal() const {
        return kind_ == Kind::GlobalVariable || kind_ == Kind::Function;
    }

protected:
    Value(Kind kind, Type* type, const std::string& name = "")
        : kind_(kind), type_(type), name_(name) {}

    void addUse(Use* use);
    void removeUse(Use* use);

private:
    friend class Use;
    friend class User;

    Kind kind_;
    Type* type_;
    std::string name_;

    // Head of use list (singly-linked list of uses)
    Use* useListHead_ = nullptr;
};

// ============================================================================
// User - A Value that uses other Values (has operands)
// ============================================================================

class User : public Value {
public:
    size_t getNumOperands() const { return operands_.size(); }

    Value* getOperand(size_t i) const {
        assert(i < operands_.size() && "Operand index out of bounds");
        return operands_[i].getValue();
    }

    void setOperand(size_t i, Value* v) {
        assert(i < operands_.size() && "Operand index out of bounds");
        operands_[i].setValue(v);
    }

    // Iterate over operands
    class operand_iterator {
    public:
        operand_iterator(Use* use) : current_(use) {}
        Value* operator*() const { return current_->getValue(); }
        operand_iterator& operator++() { ++current_; return *this; }
        bool operator!=(const operand_iterator& other) const { return current_ != other.current_; }
    private:
        Use* current_;
    };

protected:
    User(Kind kind, Type* type, unsigned numOperands, const std::string& name = "")
        : Value(kind, type, name), operands_(numOperands) {
        for (unsigned i = 0; i < numOperands; ++i) {
            operands_[i].setUser(this);
        }
    }

    // For variable-operand users (PHI, call)
    void addOperand(Value* v) {
        unsigned idx = operands_.size();
        operands_.emplace_back(v, this, idx);
        if (v) {
            v->addUse(&operands_.back());
        }
    }

    void setNumOperands(unsigned n) {
        operands_.resize(n);
        for (unsigned i = 0; i < n; ++i) {
            operands_[i].setUser(this);
        }
    }

    std::vector<Use> operands_;
};

// ============================================================================
// Constant - Base class for all constants
// ============================================================================

class Constant : public User {
public:
    static bool classof(const Value* v) {
        return v->isConstant();
    }

protected:
    Constant(Kind kind, Type* type) : User(kind, type, 0) {}
};

// ============================================================================
// ConstantInt - Integer constant
// ============================================================================

class ConstantInt : public Constant {
public:
    int64_t getSExtValue() const { return static_cast<int64_t>(value_); }
    uint64_t getZExtValue() const { return value_; }
    uint64_t getValue() const { return value_; }  // Alias for getZExtValue
    unsigned getBitWidth() const {
        return static_cast<IntegerType*>(getType())->getBitWidth();
    }

    bool isZero() const { return value_ == 0; }
    bool isOne() const { return value_ == 1; }
    bool isMinusOne() const { return static_cast<int64_t>(value_) == -1; }

    // Factory methods
    static ConstantInt* get(TypeContext& ctx, IntegerType* type, int64_t value);
    static ConstantInt* get(TypeContext& ctx, IntegerType* type, uint64_t value);
    static ConstantInt* getTrue(TypeContext& ctx);
    static ConstantInt* getFalse(TypeContext& ctx);

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::ConstantInt;
    }

private:
    friend class TypeContext;
    ConstantInt(IntegerType* type, uint64_t value)
        : Constant(Kind::ConstantInt, type), value_(value) {}

    uint64_t value_;
};

// ============================================================================
// ConstantFP - Floating point constant
// ============================================================================

class ConstantFP : public Constant {
public:
    double getValue() const { return value_; }

    bool isZero() const { return value_ == 0.0; }
    bool isNaN() const;
    bool isInfinity() const;

    // Factory methods
    static ConstantFP* get(TypeContext& ctx, FloatType* type, double value);
    static ConstantFP* getZero(TypeContext& ctx, FloatType* type);

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::ConstantFP;
    }

private:
    friend class TypeContext;
    ConstantFP(FloatType* type, double value)
        : Constant(Kind::ConstantFP, type), value_(value) {}

    double value_;
};

// ============================================================================
// ConstantNull - Null pointer constant
// ============================================================================

class ConstantNull : public Constant {
public:
    static ConstantNull* get(TypeContext& ctx, PointerType* type = nullptr);

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::ConstantNull;
    }

private:
    friend class TypeContext;
    explicit ConstantNull(PointerType* type) : Constant(Kind::ConstantNull, type) {}
};

// ============================================================================
// ConstantUndef - Undefined value
// ============================================================================

class ConstantUndef : public Constant {
public:
    static ConstantUndef* get(TypeContext& ctx, Type* type);

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::ConstantUndef;
    }

private:
    friend class TypeContext;
    explicit ConstantUndef(Type* type) : Constant(Kind::ConstantUndef, type) {}
};

// ============================================================================
// ConstantZeroInit - Zero initializer for aggregates
// ============================================================================

class ConstantZeroInit : public Constant {
public:
    static ConstantZeroInit* get(TypeContext& ctx, Type* type);

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::ConstantZeroInit;
    }

private:
    friend class TypeContext;
    explicit ConstantZeroInit(Type* type) : Constant(Kind::ConstantZeroInit, type) {}
};

// ============================================================================
// ConstantString - String literal (array of i8 + null terminator)
// ============================================================================

class ConstantString : public Constant {
public:
    const std::string& getValue() const { return value_; }
    bool isNullTerminated() const { return nullTerminated_; }

    // Get the array type (e.g., [13 x i8] for "Hello World!")
    ArrayType* getArrayType() const {
        return static_cast<ArrayType*>(getType());
    }

    // Factory method
    static ConstantString* get(TypeContext& ctx, const std::string& value, bool nullTerminated = true);

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::ConstantString;
    }

private:
    friend class TypeContext;
    ConstantString(ArrayType* type, const std::string& value, bool nullTerminated)
        : Constant(Kind::ConstantString, type), value_(value), nullTerminated_(nullTerminated) {}

    std::string value_;
    bool nullTerminated_;
};

// ============================================================================
// ConstantArray - Array constant
// ============================================================================

class ConstantArray : public Constant {
public:
    ArrayType* getArrayType() const {
        return static_cast<ArrayType*>(getType());
    }

    const std::vector<Constant*>& getElements() const { return elements_; }
    size_t getNumElements() const { return elements_.size(); }
    Constant* getElement(size_t i) const {
        return i < elements_.size() ? elements_[i] : nullptr;
    }

    // Factory method
    static ConstantArray* get(TypeContext& ctx, ArrayType* type, std::vector<Constant*> elements);

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::ConstantArray;
    }

private:
    friend class TypeContext;
    ConstantArray(ArrayType* type, std::vector<Constant*> elements)
        : Constant(Kind::ConstantArray, type), elements_(std::move(elements)) {}

    std::vector<Constant*> elements_;
};

// ============================================================================
// GlobalValue - Base class for global variables and functions
// ============================================================================

class GlobalValue : public Constant {
public:
    enum class Linkage {
        External,       // Externally visible
        Internal,       // Not visible outside this module
        Private,        // Like internal, but can be discarded
        LinkOnce,       // Merged with other definitions
        Weak,           // Weak linkage
        Common,         // Common linkage (for C tentative definitions)
        Appending,      // Appending linkage (for arrays)
        ExternWeak,     // External weak linkage
        AvailableExternally  // Available externally
    };

    enum class Visibility {
        Default,
        Hidden,
        Protected
    };

    Linkage getLinkage() const { return linkage_; }
    void setLinkage(Linkage l) { linkage_ = l; }

    Visibility getVisibility() const { return visibility_; }
    void setVisibility(Visibility v) { visibility_ = v; }

    virtual bool isDeclaration() const { return isDeclaration_; }
    bool isDefinition() const { return !isDeclaration_; }

    unsigned getAlignment() const { return alignment_; }
    void setAlignment(unsigned a) { alignment_ = a; }

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::GlobalVariable ||
               v->getValueKind() == Kind::Function;
    }

protected:
    GlobalValue(Kind kind, Type* type, const std::string& name, Linkage linkage)
        : Constant(kind, type), linkage_(linkage), visibility_(Visibility::Default),
          alignment_(0), isDeclaration_(true) {
        setName(name);
    }

    Linkage linkage_;
    Visibility visibility_;
    unsigned alignment_;
    bool isDeclaration_;
};

// ============================================================================
// GlobalVariable - Global variable
// ============================================================================

class GlobalVariable : public GlobalValue {
public:
    GlobalVariable(PointerType* ptrType, Type* valueType, const std::string& name,
                   Linkage linkage = Linkage::External, Constant* initializer = nullptr);

    Type* getValueType() const { return valueType_; }

    bool hasInitializer() const { return initializer_ != nullptr; }
    Constant* getInitializer() const { return initializer_; }
    void setInitializer(Constant* init) {
        initializer_ = init;
        isDeclaration_ = (init == nullptr);
    }

    bool isConstant() const { return isConstant_; }
    void setConstant(bool c) { isConstant_ = c; }

    bool isThreadLocal() const { return isThreadLocal_; }
    void setThreadLocal(bool tl) { isThreadLocal_ = tl; }

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::GlobalVariable;
    }

private:
    Type* valueType_;         // The type of the value (not the pointer)
    Constant* initializer_;
    bool isConstant_;
    bool isThreadLocal_;
};

// ============================================================================
// Argument - Function argument
// ============================================================================

class Argument : public Value {
public:
    Argument(Type* type, const std::string& name, Function* parent, unsigned argNo);

    Function* getParent() const { return parent_; }
    unsigned getArgNo() const { return argNo_; }

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::Argument;
    }

private:
    Function* parent_;
    unsigned argNo_;
};

// ============================================================================
// Utility functions for getting default values
// ============================================================================

// Get the appropriate zero/null value for a type
Constant* getZeroValue(TypeContext& ctx, Type* type);

// Get undef value for a type
Constant* getUndefValue(TypeContext& ctx, Type* type);

} // namespace IR
} // namespace Backends
} // namespace XXML
