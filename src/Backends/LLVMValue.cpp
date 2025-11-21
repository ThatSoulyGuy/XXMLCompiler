#include "Backends/LLVMValue.h"

namespace XXML {
namespace Backends {

std::string LLVMValue::toIR() const {
    switch (kind_) {
        case Kind::Register:
            return "%" + name_;
        case Kind::Constant:
            return name_; // Already a literal value
        case Kind::Global:
            return "@" + name_;
        case Kind::Null:
            return "null";
    }
    return "null";
}

} // namespace Backends
} // namespace XXML
