#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <cstdint>

namespace XXML {

namespace Parser {
    struct LambdaExpr;
}

namespace Semantic {

// Forward declarations
class CompiletimeValue;

class CompiletimeValue {
public:
    enum class Kind {
        Integer,
        Float,
        Double,
        String,
        Bool,
        Null,
        Object,
        Lambda,
        TypeInfo  // For compile-time reflection
    };

    Kind kind;
    
    explicit CompiletimeValue(Kind k) : kind(k) {}
    virtual ~CompiletimeValue() = default;
    virtual std::unique_ptr<CompiletimeValue> clone() const = 0;

    // Raw LLVM value support for constant folding
    // Returns true if this value can be used as a raw LLVM primitive (i64, float, etc.)
    virtual bool canUseRawValue() const { return false; }
    // Returns the LLVM type string for the raw value (e.g., "i64", "float", "double", "i1")
    virtual std::string getRawLLVMType() const { return "ptr"; }

    // Type checking helpers
    bool isInteger() const { return kind == Kind::Integer; }
    bool isFloat() const { return kind == Kind::Float; }
    bool isDouble() const { return kind == Kind::Double; }
    bool isString() const { return kind == Kind::String; }
    bool isBool() const { return kind == Kind::Bool; }
    bool isNull() const { return kind == Kind::Null; }
    bool isObject() const { return kind == Kind::Object; }
    bool isLambda() const { return kind == Kind::Lambda; }
    bool isTypeInfo() const { return kind == Kind::TypeInfo; }
};

class CompiletimeInteger : public CompiletimeValue {
public:
    int64_t value;

    explicit CompiletimeInteger(int64_t v)
        : CompiletimeValue(Kind::Integer), value(v) {}

    std::unique_ptr<CompiletimeValue> clone() const override {
        return std::make_unique<CompiletimeInteger>(value);
    }

    bool canUseRawValue() const override { return true; }
    std::string getRawLLVMType() const override { return "i64"; }
};

class CompiletimeFloat : public CompiletimeValue {
public:
    float value;

    explicit CompiletimeFloat(float v)
        : CompiletimeValue(Kind::Float), value(v) {}

    std::unique_ptr<CompiletimeValue> clone() const override {
        return std::make_unique<CompiletimeFloat>(value);
    }

    bool canUseRawValue() const override { return true; }
    std::string getRawLLVMType() const override { return "float"; }
};

class CompiletimeDouble : public CompiletimeValue {
public:
    double value;

    explicit CompiletimeDouble(double v)
        : CompiletimeValue(Kind::Double), value(v) {}

    std::unique_ptr<CompiletimeValue> clone() const override {
        return std::make_unique<CompiletimeDouble>(value);
    }

    bool canUseRawValue() const override { return true; }
    std::string getRawLLVMType() const override { return "double"; }
};

class CompiletimeString : public CompiletimeValue {
public:
    std::string value;
    
    explicit CompiletimeString(const std::string& v) 
        : CompiletimeValue(Kind::String), value(v) {}
    
    std::unique_ptr<CompiletimeValue> clone() const override {
        return std::make_unique<CompiletimeString>(value);
    }
};

class CompiletimeBool : public CompiletimeValue {
public:
    bool value;

    explicit CompiletimeBool(bool v)
        : CompiletimeValue(Kind::Bool), value(v) {}

    std::unique_ptr<CompiletimeValue> clone() const override {
        return std::make_unique<CompiletimeBool>(value);
    }

    bool canUseRawValue() const override { return true; }
    std::string getRawLLVMType() const override { return "i1"; }
};

class CompiletimeNull : public CompiletimeValue {
public:
    CompiletimeNull() : CompiletimeValue(Kind::Null) {}
    
    std::unique_ptr<CompiletimeValue> clone() const override {
        return std::make_unique<CompiletimeNull>();
    }
};

class CompiletimeObject : public CompiletimeValue {
public:
    std::string className;
    std::unordered_map<std::string, std::unique_ptr<CompiletimeValue>> properties;
    
    explicit CompiletimeObject(const std::string& cn)
        : CompiletimeValue(Kind::Object), className(cn) {}
    
    std::unique_ptr<CompiletimeValue> clone() const override {
        auto cloned = std::make_unique<CompiletimeObject>(className);
        for (const auto& [name, value] : properties) {
            cloned->properties[name] = value->clone();
        }
        return cloned;
    }
    
    CompiletimeValue* getProperty(const std::string& name) {
        auto it = properties.find(name);
        return (it != properties.end()) ? it->second.get() : nullptr;
    }
    
    void setProperty(const std::string& name, std::unique_ptr<CompiletimeValue> value) {
        properties[name] = std::move(value);
    }
};

class CompiletimeLambda : public CompiletimeValue {
public:
    Parser::LambdaExpr* lambdaExpr;  // AST reference for execution
    std::unordered_map<std::string, std::unique_ptr<CompiletimeValue>> captures;

    explicit CompiletimeLambda(Parser::LambdaExpr* expr)
        : CompiletimeValue(Kind::Lambda), lambdaExpr(expr) {}

    std::unique_ptr<CompiletimeValue> clone() const override {
        auto cloned = std::make_unique<CompiletimeLambda>(lambdaExpr);
        for (const auto& [name, value] : captures) {
            cloned->captures[name] = value->clone();
        }
        return cloned;
    }
};

/**
 * @brief Compile-time type information for reflection
 *
 * Represents metadata about a type that can be queried at compile time.
 * This allows reflection queries like GetType<T>::get().getPropertyCount()
 * to be evaluated at compile time and folded to constants.
 */
class CompiletimeTypeInfo : public CompiletimeValue {
public:
    std::string typeName;           // Full qualified type name
    std::string simpleName;         // Simple name without namespace
    std::string namespaceName;      // Namespace portion

    // Type metadata
    std::vector<std::pair<std::string, std::string>> properties;  // name, type
    std::vector<std::string> propertyOwnerships;                   // ^, &, %
    std::vector<std::pair<std::string, std::string>> methods;     // name, returnType
    std::vector<std::string> methodReturnOwnerships;

    bool isValueType = false;       // True for Structure, false for Class
    bool isTemplate = false;
    std::vector<std::string> templateParams;
    size_t instanceSize = 0;

    explicit CompiletimeTypeInfo(const std::string& name)
        : CompiletimeValue(Kind::TypeInfo), typeName(name) {
        // Extract simple name and namespace
        size_t lastSep = name.rfind("::");
        if (lastSep != std::string::npos) {
            namespaceName = name.substr(0, lastSep);
            simpleName = name.substr(lastSep + 2);
        } else {
            simpleName = name;
        }
    }

    std::unique_ptr<CompiletimeValue> clone() const override {
        auto cloned = std::make_unique<CompiletimeTypeInfo>(typeName);
        cloned->simpleName = simpleName;
        cloned->namespaceName = namespaceName;
        cloned->properties = properties;
        cloned->propertyOwnerships = propertyOwnerships;
        cloned->methods = methods;
        cloned->methodReturnOwnerships = methodReturnOwnerships;
        cloned->isValueType = isValueType;
        cloned->isTemplate = isTemplate;
        cloned->templateParams = templateParams;
        cloned->instanceSize = instanceSize;
        return cloned;
    }

    // Compile-time queryable methods
    int64_t getPropertyCount() const { return static_cast<int64_t>(properties.size()); }
    int64_t getMethodCount() const { return static_cast<int64_t>(methods.size()); }

    std::string getPropertyNameAt(int64_t idx) const {
        if (idx >= 0 && static_cast<size_t>(idx) < properties.size()) {
            return properties[idx].first;
        }
        return "";
    }

    std::string getPropertyTypeAt(int64_t idx) const {
        if (idx >= 0 && static_cast<size_t>(idx) < properties.size()) {
            return properties[idx].second;
        }
        return "";
    }

    std::string getMethodNameAt(int64_t idx) const {
        if (idx >= 0 && static_cast<size_t>(idx) < methods.size()) {
            return methods[idx].first;
        }
        return "";
    }

    std::string getMethodReturnTypeAt(int64_t idx) const {
        if (idx >= 0 && static_cast<size_t>(idx) < methods.size()) {
            return methods[idx].second;
        }
        return "";
    }

    bool hasProperty(const std::string& name) const {
        for (const auto& [propName, _] : properties) {
            if (propName == name) return true;
        }
        return false;
    }

    bool hasMethod(const std::string& name) const {
        for (const auto& [methodName, _] : methods) {
            if (methodName == name) return true;
        }
        return false;
    }
};

} // namespace Semantic
} // namespace XXML
