#include "Backends/LLVMType.h"

namespace XXML {
namespace Backends {

std::string LLVMType::toString() const {
    switch (kind_) {
        case Kind::Void:   return "void";
        case Kind::I1:     return "i1";
        case Kind::I8:     return "i8";
        case Kind::I32:    return "i32";
        case Kind::I64:    return "i64";
        case Kind::Float:  return "float";
        case Kind::Double: return "double";
        case Kind::Ptr:    return "ptr";
        case Kind::Struct: return structName_.empty() ? "ptr" : structName_;
        case Kind::Unknown: return "ptr"; // Default to pointer for unknown types
    }
    return "ptr";
}

} // namespace Backends
} // namespace XXML
