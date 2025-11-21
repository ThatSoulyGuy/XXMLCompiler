#include "Backends/TypeConverter.h"
#include <unordered_map>

namespace XXML {
namespace Backends {

static const std::unordered_map<std::string, LLVMType::Kind> typeMap = {
    {"None", LLVMType::Kind::Void},
    {"Bool", LLVMType::Kind::I1},
    {"Integer", LLVMType::Kind::I64},
    {"Float", LLVMType::Kind::Float},
    {"Double", LLVMType::Kind::Double},
    {"String", LLVMType::Kind::Ptr},
};

LLVMType TypeConverter::xxmlToLLVM(const std::string& xxmlType) {
    auto it = typeMap.find(xxmlType);
    if (it != typeMap.end()) {
        return LLVMType(it->second);
    }

    // All other types (classes, templates) are pointers
    return LLVMType::getPointerType();
}

std::string TypeConverter::llvmToXXML(const LLVMType& llvmType) {
    switch (llvmType.getKind()) {
        case LLVMType::Kind::Void:   return "None";
        case LLVMType::Kind::I1:     return "Bool";
        case LLVMType::Kind::I64:    return "Integer";
        case LLVMType::Kind::Float:  return "Float";
        case LLVMType::Kind::Double: return "Double";
        case LLVMType::Kind::Ptr:    return "ptr";
        case LLVMType::Kind::Struct: return llvmType.getStructName();
        default: return "ptr";
    }
}

bool TypeConverter::isPrimitive(const std::string& xxmlType) {
    return xxmlType == "None" || xxmlType == "Bool" ||
           xxmlType == "Integer" || xxmlType == "Float" ||
           xxmlType == "Double";
}

LLVMType TypeConverter::getRepresentationType(const std::string& xxmlType) {
    // Primitives except None are passed by value
    if (xxmlType == "Bool") return LLVMType::getI1Type();
    if (xxmlType == "Integer") return LLVMType::getI64Type();
    if (xxmlType == "Float") return LLVMType::getFloatType();
    if (xxmlType == "Double") return LLVMType::getDoubleType();

    // Everything else (including String and all classes) is a pointer
    return LLVMType::getPointerType();
}

} // namespace Backends
} // namespace XXML
