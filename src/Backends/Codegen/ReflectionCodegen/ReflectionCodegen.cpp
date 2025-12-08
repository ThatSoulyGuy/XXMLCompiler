#include "Backends/Codegen/ReflectionCodegen/ReflectionCodegen.h"
#include <sstream>

namespace XXML {
namespace Backends {
namespace Codegen {

ReflectionCodegen::ReflectionCodegen(CodegenContext& ctx)
    : ctx_(ctx) {
    // Initialize the type-safe builders
    globalBuilder_ = std::make_unique<LLVMIR::GlobalBuilder>(ctx_.module());
    metadataBuilder_ = std::make_unique<LLVMIR::MetadataBuilder>(ctx_.module(), *globalBuilder_);
}

// Helper to mangle name for LLVM IR
static std::string mangleName(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ') {
            c = '_';
        }
    }
    return result;
}

// Helper to escape string for LLVM IR
static std::string escapeString(const std::string& str) {
    std::string result;
    for (char c : str) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\22";
        else if (c == '\n') result += "\\0A";
        else if (c == '\r') result += "\\0D";
        else result += c;
    }
    return result;
}

LLVMIR::OwnershipKind ReflectionCodegen::parseOwnership(const std::string& ownership) {
    if (ownership.empty()) return LLVMIR::OwnershipKind::Unknown;
    char c = ownership[0];
    if (c == '^') return LLVMIR::OwnershipKind::Owned;
    if (c == '&') return LLVMIR::OwnershipKind::Reference;
    if (c == '%') return LLVMIR::OwnershipKind::Copy;
    return LLVMIR::OwnershipKind::Unknown;
}

LLVMIR::ReflectionClassInfo ReflectionCodegen::convertMetadata(const ReflectionClassMetadata& metadata) const {
    LLVMIR::ReflectionClassInfo info;

    // Basic info
    info.name = metadata.name;
    info.namespaceName = metadata.namespaceName;
    info.fullName = metadata.fullName;
    info.isTemplate = metadata.isTemplate;
    info.instanceSize = static_cast<int64_t>(metadata.instanceSize);
    info.templateParams = metadata.templateParams;

    // Convert properties
    for (size_t i = 0; i < metadata.properties.size(); ++i) {
        LLVMIR::ReflectionProperty prop;
        prop.name = metadata.properties[i].first;
        prop.typeName = metadata.properties[i].second;
        prop.ownership = i < metadata.propertyOwnerships.size()
            ? parseOwnership(metadata.propertyOwnerships[i])
            : LLVMIR::OwnershipKind::Unknown;
        prop.offset = static_cast<int64_t>(i * 8);  // Simple offset calculation
        info.properties.push_back(std::move(prop));
    }

    // Convert methods
    for (size_t m = 0; m < metadata.methods.size(); ++m) {
        LLVMIR::ReflectionMethod method;
        method.name = metadata.methods[m].first;
        method.returnTypeName = metadata.methods[m].second;
        method.returnOwnership = m < metadata.methodReturnOwnerships.size()
            ? parseOwnership(metadata.methodReturnOwnerships[m])
            : LLVMIR::OwnershipKind::Unknown;
        // Use metadata if available, otherwise default to false
        method.isStatic = (m < metadata.methodIsStatic.size()) ? metadata.methodIsStatic[m] : false;
        method.isConstructor = (method.name == "Constructor");
        method.funcPtr = nullptr;  // TODO: Set actual function pointer if needed

        // Convert parameters
        if (m < metadata.methodParameters.size()) {
            for (const auto& [paramName, paramType, paramOwn] : metadata.methodParameters[m]) {
                LLVMIR::ReflectionParameter param;
                param.name = paramName;
                param.typeName = paramType;
                param.ownership = parseOwnership(paramOwn);
                method.parameters.push_back(std::move(param));
            }
        }

        info.methods.push_back(std::move(method));
    }

    return info;
}

void ReflectionCodegen::generate() {
    const auto& reflectionMetadata = ctx_.reflectionMetadata();
    if (reflectionMetadata.empty()) {
        return;
    }

    std::stringstream out;
    out << "\n; ============ Reflection Metadata ============\n\n";

    // Track string constants to avoid duplicates
    std::map<std::string, std::string> stringConstants;
    int stringCounter = 0;

    // Two-phase approach: collect all strings first, then emit them before the arrays
    // Phase 1: Collect all strings we'll need
    auto registerString = [&](const std::string& str) -> std::string {
        if (stringConstants.find(str) != stringConstants.end()) {
            return stringConstants[str];
        }
        std::string name = "@.refl.str." + std::to_string(stringCounter++);
        stringConstants[str] = name;
        return name;
    };

    // Pre-register all strings needed for all classes
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        // Class name strings
        registerString(metadata.name);
        registerString(fullName);
        registerString(metadata.namespaceName);

        // Base class name (if any)
        if (!metadata.baseClassName.empty()) {
            registerString(metadata.baseClassName);
        }

        // Property strings
        for (const auto& prop : metadata.properties) {
            registerString(prop.first);   // property name
            registerString(prop.second);  // property type
        }

        // Method strings
        for (const auto& method : metadata.methods) {
            registerString(method.first);   // method name
            registerString(method.second);  // return type
        }
    }

    // Phase 2: Emit all string constants at the top
    out << "; String constants for reflection metadata\n";
    for (const auto& [str, name] : stringConstants) {
        std::string escaped = escapeString(str);
        out << name << " = private unnamed_addr constant [" << (str.length() + 1)
            << " x i8] c\"" << escaped << "\\00\"\n";
    }
    out << "\n";

    // Helper to look up already-registered strings
    auto getStringConstant = [&](const std::string& str) -> std::string {
        return stringConstants[str];
    };

    // Generate property arrays for each class
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        std::string mangledName = mangleName(fullName);

        // Generate property info array if there are properties
        // The runtime expects a contiguous array of ReflectionPropertyInfo structs
        if (!metadata.properties.empty()) {
            out << "\n; Properties for " << fullName << "\n";

            // Generate contiguous array of property info structs
            out << "@reflection_props_" << mangledName << " = private constant ["
                << metadata.properties.size() << " x %ReflectionPropertyInfo] [\n";

            for (size_t i = 0; i < metadata.properties.size(); ++i) {
                const auto& prop = metadata.properties[i];
                std::string propName = getStringConstant(prop.first);
                std::string propType = getStringConstant(prop.second);

                // Ownership: 0=None, 1=Owned(^), 2=Ref(&), 3=Copy(%)
                int ownership = 0;
                if (i < metadata.propertyOwnerships.size()) {
                    char c = metadata.propertyOwnerships[i][0];
                    if (c == '^') ownership = 1;
                    else if (c == '&') ownership = 2;
                    else if (c == '%') ownership = 3;
                }

                int64_t offset = static_cast<int64_t>(i * 8);

                if (i > 0) out << ",\n";
                // ReflectionPropertyInfo: name, typeName, ownership, offset, annotationCount, annotations
                out << "  %ReflectionPropertyInfo { "
                    << "ptr " << propName << ", "
                    << "ptr " << propType << ", "
                    << "i32 " << ownership << ", "
                    << "i64 " << offset << ", "
                    << "i32 0, "  // annotationCount
                    << "ptr null }";  // annotations
            }
            out << "\n]\n";
        }

        // Separate constructors from regular methods
        std::vector<size_t> constructorIndices;
        std::vector<size_t> methodIndices;
        for (size_t m = 0; m < metadata.methods.size(); ++m) {
            if (metadata.methods[m].first == "Constructor") {
                constructorIndices.push_back(m);
            } else {
                methodIndices.push_back(m);
            }
        }

        // Helper lambda to emit a method info struct
        auto emitMethodInfo = [&](size_t m, bool isFirst) {
            const auto& method = metadata.methods[m];
            std::string methodName = getStringConstant(method.first);
            std::string returnType = getStringConstant(method.second);

            int returnOwnership = 0;
            if (m < metadata.methodReturnOwnerships.size()) {
                char c = metadata.methodReturnOwnerships[m][0];
                if (c == '^') returnOwnership = 1;
                else if (c == '&') returnOwnership = 2;
                else if (c == '%') returnOwnership = 3;
            }

            int paramCount = 0;
            if (m < metadata.methodParameters.size()) {
                paramCount = static_cast<int>(metadata.methodParameters[m].size());
            }

            // Use metadata if available, otherwise default to false
            bool isStatic = (m < metadata.methodIsStatic.size()) ? metadata.methodIsStatic[m] : false;
            bool isCtor = (method.first == "Constructor");

            if (!isFirst) out << ",\n";
            // ReflectionMethodInfo: name, returnType, returnOwnership, paramCount, params, funcPtr, isStatic, isCtor, annotationCount, annotations
            out << "  %ReflectionMethodInfo { "
                << "ptr " << methodName << ", "
                << "ptr " << returnType << ", "
                << "i32 " << returnOwnership << ", "
                << "i32 " << paramCount << ", "
                << "ptr null, "  // params (not implemented yet)
                << "ptr null, "  // funcPtr
                << "i1 " << (isStatic ? "true" : "false") << ", "
                << "i1 " << (isCtor ? "true" : "false") << ", "
                << "i32 0, "  // annotationCount
                << "ptr null }";  // annotations
        };

        // Generate method info array (non-constructors only)
        if (!methodIndices.empty()) {
            out << "\n; Methods for " << fullName << "\n";
            out << "@reflection_methods_" << mangledName << " = private constant ["
                << methodIndices.size() << " x %ReflectionMethodInfo] [\n";

            for (size_t i = 0; i < methodIndices.size(); ++i) {
                emitMethodInfo(methodIndices[i], i == 0);
            }
            out << "\n]\n";
        }

        // Generate constructor info array
        if (!constructorIndices.empty()) {
            out << "\n; Constructors for " << fullName << "\n";
            out << "@reflection_ctors_" << mangledName << " = private constant ["
                << constructorIndices.size() << " x %ReflectionMethodInfo] [\n";

            for (size_t i = 0; i < constructorIndices.size(); ++i) {
                emitMethodInfo(constructorIndices[i], i == 0);
            }
            out << "\n]\n";
        }
    }

    // Generate TypeInfo structs
    out << "\n; ============ Type Info Structures ============\n\n";
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        std::string mangledName = mangleName(fullName);

        std::string nameStr = getStringConstant(metadata.name);
        std::string fullNameStr = getStringConstant(fullName);
        std::string nsStr = getStringConstant(metadata.namespaceName);

        // Count constructors vs regular methods
        size_t constructorCount = 0;
        size_t methodCount = 0;
        for (const auto& method : metadata.methods) {
            if (method.first == "Constructor") {
                constructorCount++;
            } else {
                methodCount++;
            }
        }

        // ReflectionTypeInfo: name, namespaceName, fullName, isTemplate, templateParamCount,
        // templateParams, propertyCount, properties, methodCount, methods,
        // constructorCount, constructors, baseClassName, instanceSize, annotationCount, annotations
        out << "@reflection_typeinfo_" << mangledName << " = private constant %ReflectionTypeInfo {\n";
        out << "  ptr " << nameStr << ",\n";           // name
        out << "  ptr " << nsStr << ",\n";             // namespaceName
        out << "  ptr " << fullNameStr << ",\n";       // fullName
        out << "  i1 " << (metadata.isTemplate ? "true" : "false") << ",\n";  // isTemplate
        out << "  i32 " << metadata.templateParams.size() << ",\n";  // templateParamCount
        out << "  ptr null,\n";                        // templateParams
        out << "  i32 " << metadata.properties.size() << ",\n";  // propertyCount
        if (!metadata.properties.empty()) {
            out << "  ptr @reflection_props_" << mangledName << ",\n";  // properties
        } else {
            out << "  ptr null,\n";
        }
        out << "  i32 " << methodCount << ",\n";  // methodCount (excluding constructors)
        if (methodCount > 0) {
            out << "  ptr @reflection_methods_" << mangledName << ",\n";  // methods
        } else {
            out << "  ptr null,\n";
        }
        out << "  i32 " << constructorCount << ",\n";  // constructorCount
        if (constructorCount > 0) {
            out << "  ptr @reflection_ctors_" << mangledName << ",\n";  // constructors
        } else {
            out << "  ptr null,\n";
        }
        // baseClassName - emit pointer if base class exists, null otherwise
        if (!metadata.baseClassName.empty()) {
            std::string baseClassStr = getStringConstant(metadata.baseClassName);
            out << "  ptr " << baseClassStr << ",\n";  // baseClassName
        } else {
            out << "  ptr null,\n";                    // baseClassName (no base)
        }
        out << "  i64 " << metadata.instanceSize << ",\n";  // instanceSize
        out << "  i32 0,\n";                           // annotationCount
        out << "  ptr null\n";                         // annotations
        out << "}\n\n";
    }

    // Generate registration function that gets called via global_ctors
    out << "\n; ============ Type Registration ============\n\n";
    out << "define internal void @__xxml_register_reflection_types() {\n";
    out << "entry:\n";
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        std::string mangledName = mangleName(fullName);
        out << "  %reg_" << mangledName << " = call ptr @Reflection_registerType(ptr @reflection_typeinfo_" << mangledName << ")\n";
    }
    out << "  ret void\n";
    out << "}\n\n";

    // Register the constructor to run before main
    out << "; Use global_ctors to run registration before main\n";
    out << "@llvm.global_ctors = appending global [1 x { i32, ptr, ptr }] ";
    out << "[{ i32, ptr, ptr } { i32 65535, ptr @__xxml_register_reflection_types, ptr null }]\n";

    stringIR_ = out.str();
}

std::string ReflectionCodegen::getIR() const {
    return stringIR_;
}

void ReflectionCodegen::generateRegistrationCalls() {
    const auto& reflectionMetadata = ctx_.reflectionMetadata();
    if (reflectionMetadata.empty()) {
        return;
    }

    // Get main function
    auto* mainFunc = ctx_.module().getFunction("main");
    if (!mainFunc) {
        return;  // No main function, nothing to do
    }

    // Get entry block
    auto* entryBlock = mainFunc->getEntryBlock();
    if (!entryBlock) {
        return;
    }

    // Find the first non-alloca instruction to insert before
    // We want to preserve allocas at the top of the function
    LLVMIR::Instruction* insertBefore = nullptr;
    for (auto* inst : *entryBlock) {
        if (!dynamic_cast<LLVMIR::AllocaInst*>(inst)) {
            insertBefore = inst;
            break;
        }
    }

    // Set up function declaration for Reflection_registerType
    auto* ptrTy = ctx_.module().getContext().getPtrTy();
    std::vector<LLVMIR::Type*> regParamTypes = { ptrTy };
    auto* regFuncType = ctx_.module().getContext().getFunctionTy(ptrTy, regParamTypes);

    LLVMIR::Function* registerFunc = ctx_.module().getFunction("Reflection_registerType");
    if (!registerFunc) {
        registerFunc = ctx_.module().createFunction(regFuncType, "Reflection_registerType",
                                                    LLVMIR::Function::Linkage::External);
    }

    // Save current insert point
    auto* savedBlock = ctx_.builder().getInsertBlock();

    // Set insert point to before the first non-alloca instruction
    if (insertBefore) {
        ctx_.builder().setInsertPoint(entryBlock, insertBefore);
    } else {
        ctx_.builder().setInsertPoint(entryBlock);
    }

    // Generate registration calls for each type
    for (const auto& [fullName, metadata] : reflectionMetadata) {
        // Mangle the name the same way MetadataBuilder does
        std::string mangledName = fullName;
        for (char& c : mangledName) {
            if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ') {
                c = '_';
            }
        }

        std::string globalName = "reflection_typeinfo_" + mangledName;

        // Get reference to the global variable
        auto* globalVar = ctx_.module().getGlobal(globalName);
        if (globalVar) {
            // Call Reflection_registerType with the global variable pointer
            std::vector<LLVMIR::AnyValue> args = { LLVMIR::AnyValue(globalVar) };
            ctx_.builder().createCall(registerFunc, args);
        }
    }

    // Restore insert point
    if (savedBlock) {
        ctx_.builder().setInsertPoint(savedBlock);
    }
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
