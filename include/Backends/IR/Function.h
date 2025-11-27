#pragma once

#include "Backends/IR/BasicBlock.h"
#include <list>
#include <vector>
#include <memory>
#include <unordered_map>
#include <set>

namespace XXML {
namespace Backends {
namespace IR {

// Forward declarations
class Module;

// ============================================================================
// DominanceInfo - Dominance analysis for SSA verification
// ============================================================================

class DominanceInfo {
public:
    DominanceInfo() = default;

    void compute(Function* func);

    // Does block A dominate block B?
    bool dominates(BasicBlock* a, BasicBlock* b) const;

    // Does instruction A dominate instruction B?
    bool dominates(Instruction* a, Instruction* b) const;

    // Get immediate dominator of a block
    BasicBlock* getImmediateDominator(BasicBlock* bb) const;

    // Get all blocks dominated by this block
    const std::vector<BasicBlock*>& getDominatedBlocks(BasicBlock* bb) const;

    bool isValid() const { return valid_; }
    void invalidate() { valid_ = false; }

private:
    std::unordered_map<BasicBlock*, BasicBlock*> idom_;  // Immediate dominator
    std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> dominated_;
    bool valid_ = false;
};

// ============================================================================
// Function - A function definition or declaration
// ============================================================================

class Function : public GlobalValue {
public:
    using BBListType = std::list<std::unique_ptr<BasicBlock>>;
    using iterator = BBListType::iterator;
    using const_iterator = BBListType::const_iterator;
    using reverse_iterator = BBListType::reverse_iterator;
    using const_reverse_iterator = BBListType::const_reverse_iterator;

    using arg_iterator = std::vector<std::unique_ptr<Argument>>::iterator;
    using const_arg_iterator = std::vector<std::unique_ptr<Argument>>::const_iterator;

    // Function attributes
    enum class Attribute {
        NoInline,
        AlwaysInline,
        OptimizeNone,
        NoUnwind,
        NoReturn,
        UWTable,
        ReadNone,
        ReadOnly,
        WriteOnly,
        NoRecurse,
        MustProgress
    };

    // Create a function
    Function(FunctionType* funcType, const std::string& name, Module* parent = nullptr,
             Linkage linkage = Linkage::External);
    ~Function();

    // Parent module
    Module* getParent() const { return parent_; }
    void setParent(Module* m) { parent_ = m; }

    // ========== Type ==========

    FunctionType* getFunctionType() const { return funcType_; }
    Type* getReturnType() const { return funcType_->getReturnType(); }

    // ========== Arguments ==========

    size_t arg_size() const { return args_.size(); }
    bool arg_empty() const { return args_.empty(); }

    arg_iterator arg_begin() { return args_.begin(); }
    arg_iterator arg_end() { return args_.end(); }
    const_arg_iterator arg_begin() const { return args_.begin(); }
    const_arg_iterator arg_end() const { return args_.end(); }

    Argument* getArg(unsigned i) {
        return i < args_.size() ? args_[i].get() : nullptr;
    }
    const Argument* getArg(unsigned i) const {
        return i < args_.size() ? args_[i].get() : nullptr;
    }

    size_t getNumArgs() const { return args_.size(); }

    // Range-based for loop support for arguments
    const std::vector<std::unique_ptr<Argument>>& args() const { return args_; }

    // ========== Basic Blocks ==========

    iterator begin() { return blocks_.begin(); }
    iterator end() { return blocks_.end(); }
    const_iterator begin() const { return blocks_.begin(); }
    const_iterator end() const { return blocks_.end(); }

    reverse_iterator rbegin() { return blocks_.rbegin(); }
    reverse_iterator rend() { return blocks_.rend(); }

    bool empty() const { return blocks_.empty(); }
    size_t size() const { return blocks_.size(); }

    BasicBlock& front() { return *blocks_.front(); }
    BasicBlock& back() { return *blocks_.back(); }
    const BasicBlock& front() const { return *blocks_.front(); }
    const BasicBlock& back() const { return *blocks_.back(); }

    // Get the entry block (first block)
    BasicBlock* getEntryBlock() {
        return blocks_.empty() ? nullptr : blocks_.front().get();
    }
    const BasicBlock* getEntryBlock() const {
        return blocks_.empty() ? nullptr : blocks_.front().get();
    }

    // Add a basic block at the end
    void push_back(std::unique_ptr<BasicBlock> bb);

    // Insert a basic block before a position
    iterator insert(iterator pos, std::unique_ptr<BasicBlock> bb);

    // Remove a basic block
    iterator erase(iterator pos);
    iterator erase(BasicBlock* bb);

    // Create a new basic block in this function
    BasicBlock* createBasicBlock(const std::string& name = "");

    // ========== Declaration vs Definition ==========

    // A declaration has no basic blocks
    bool isDeclaration() const override { return blocks_.empty(); }
    bool isDefinition() const { return !blocks_.empty(); }

    // ========== Attributes ==========

    void addAttribute(Attribute attr) { attributes_.insert(attr); }
    void removeAttribute(Attribute attr) { attributes_.erase(attr); }
    bool hasAttribute(Attribute attr) const {
        return attributes_.find(attr) != attributes_.end();
    }

    // Common attribute queries
    bool doesNotThrow() const { return hasAttribute(Attribute::NoUnwind); }
    bool doesNotReturn() const { return hasAttribute(Attribute::NoReturn); }

    // ========== Dominance ==========

    // Compute dominance information (invalidated by CFG changes)
    void computeDominanceInfo();

    // Check dominance (computes info if needed)
    bool dominates(BasicBlock* a, BasicBlock* b);
    bool dominates(Instruction* a, Instruction* b);

    // Get immediate dominator
    BasicBlock* getImmediateDominator(BasicBlock* bb);

    // Invalidate dominance info (call after CFG changes)
    void invalidateDominanceInfo() { domInfo_.invalidate(); }

    // ========== Calling Convention ==========

    enum class CallingConv {
        C,          // Default C calling convention
        Fast,       // Fast calling convention
        Cold,       // Cold calling convention
        X86_StdCall,
        X86_FastCall,
        Win64
    };

    CallingConv getCallingConv() const { return callingConv_; }
    void setCallingConv(CallingConv cc) { callingConv_ = cc; }

    // ========== Utility ==========

    // View CFG (for debugging)
    void viewCFG() const;

    static bool classof(const Value* v) {
        return v->getValueKind() == Kind::Function;
    }

private:
    Module* parent_;
    FunctionType* funcType_;
    std::vector<std::unique_ptr<Argument>> args_;
    BBListType blocks_;
    std::set<Attribute> attributes_;
    CallingConv callingConv_ = CallingConv::C;
    DominanceInfo domInfo_;

    // Create arguments from function type
    void createArguments();
};

} // namespace IR
} // namespace Backends
} // namespace XXML
