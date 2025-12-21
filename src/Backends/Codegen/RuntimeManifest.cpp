#include "Backends/Codegen/RuntimeManifest.h"

namespace XXML {
namespace Backends {
namespace Codegen {

RuntimeManifest::RuntimeManifest() {
    loadDefaults();
}

void RuntimeManifest::addFunction(const std::string& name,
                                   const std::string& returnType,
                                   const std::vector<std::string>& paramTypes,
                                   const std::string& category,
                                   const std::string& xxmlReturnType) {
    RuntimeFunctionInfo info;
    info.name = name;
    info.returnType = returnType;
    info.paramTypes = paramTypes;
    info.category = category;
    info.xxmlReturnType = xxmlReturnType;
    info.isDefinition = false;

    functions_[name] = info;

    // Track category
    if (categories_.find(category) == categories_.end()) {
        categoryOrder_.push_back(category);
    }
    categories_[category].push_back(name);
}

void RuntimeManifest::addDefinition(const std::string& name,
                                     const std::string& returnType,
                                     const std::vector<std::string>& paramTypes,
                                     const std::string& category,
                                     const std::string& definition,
                                     const std::string& xxmlReturnType) {
    RuntimeFunctionInfo info;
    info.name = name;
    info.returnType = returnType;
    info.paramTypes = paramTypes;
    info.category = category;
    info.xxmlReturnType = xxmlReturnType;
    info.isDefinition = true;
    info.definition = definition;

    functions_[name] = info;

    if (categories_.find(category) == categories_.end()) {
        categoryOrder_.push_back(category);
    }
    categories_[category].push_back(name);
}

const RuntimeFunctionInfo* RuntimeManifest::getFunction(const std::string& name) const {
    auto it = functions_.find(name);
    if (it != functions_.end()) {
        return &it->second;
    }
    return nullptr;
}

std::string RuntimeManifest::getLLVMReturnType(const std::string& name) const {
    auto* info = getFunction(name);
    return info ? info->returnType : "";
}

std::string RuntimeManifest::getXXMLReturnType(const std::string& name) const {
    auto* info = getFunction(name);
    return info ? info->xxmlReturnType : "";
}

bool RuntimeManifest::hasFunction(const std::string& name) const {
    return functions_.find(name) != functions_.end();
}

void RuntimeManifest::emitDeclarations(std::stringstream& out) const {
    for (const auto& category : categoryOrder_) {
        emitDeclarations(out, category);
    }
}

void RuntimeManifest::emitDeclarations(std::stringstream& out, const std::string& category) const {
    auto it = categories_.find(category);
    if (it == categories_.end()) return;

    out << "; " << category << "\n";

    for (const auto& funcName : it->second) {
        auto funcIt = functions_.find(funcName);
        if (funcIt == functions_.end()) continue;

        const auto& info = funcIt->second;

        if (info.isDefinition) {
            // Emit inline definition
            out << info.definition << "\n";
        } else {
            // Emit declaration
            out << "declare " << info.returnType << " @" << info.name << "(";
            for (size_t i = 0; i < info.paramTypes.size(); ++i) {
                if (i > 0) out << ", ";
                out << info.paramTypes[i];
            }
            out << ")\n";
        }
    }
    out << "\n";
}

std::vector<const RuntimeFunctionInfo*> RuntimeManifest::getFunctionsInCategory(const std::string& category) const {
    std::vector<const RuntimeFunctionInfo*> result;
    auto it = categories_.find(category);
    if (it != categories_.end()) {
        for (const auto& name : it->second) {
            if (auto* info = getFunction(name)) {
                result.push_back(info);
            }
        }
    }
    return result;
}

std::vector<std::string> RuntimeManifest::getCategories() const {
    return categoryOrder_;
}

std::vector<std::string> RuntimeManifest::getAllFunctionNames() const {
    std::vector<std::string> names;
    names.reserve(functions_.size());
    for (const auto& pair : functions_) {
        names.push_back(pair.first);
    }
    return names;
}

void RuntimeManifest::loadDefaults() {
    // ============================================
    // Memory Management
    // ============================================
    addFunction("xxml_malloc", "ptr", {"i64"}, "Memory Management");
    addFunction("xxml_free", "void", {"ptr"}, "Memory Management");
    addFunction("xxml_memcpy", "ptr", {"ptr", "ptr", "i64"}, "Memory Management");
    addFunction("xxml_memset", "ptr", {"ptr", "i32", "i64"}, "Memory Management");
    addFunction("xxml_ptr_read", "ptr", {"ptr"}, "Memory Management");
    addFunction("xxml_ptr_write", "void", {"ptr", "ptr"}, "Memory Management");
    addFunction("xxml_read_byte", "i8", {"ptr"}, "Memory Management", "NativeType<int8>");
    addFunction("xxml_write_byte", "void", {"ptr", "i8"}, "Memory Management");
    addFunction("xxml_int64_read", "i64", {"ptr"}, "Memory Management", "NativeType<int64>");
    addFunction("xxml_int64_write", "void", {"ptr", "i64"}, "Memory Management");

    // ============================================
    // Integer Operations
    // ============================================
    addFunction("Integer_Constructor", "ptr", {"i64"}, "Integer Operations", "Integer^");
    addFunction("Integer_getValue", "i64", {"ptr"}, "Integer Operations", "NativeType<int64>");
    addFunction("Integer_toInt64", "i64", {"ptr"}, "Integer Operations", "NativeType<int64>");

    // Integer_toInt32 - inline definition (truncates i64 to i32)
    addDefinition("Integer_toInt32", "i32", {"ptr"}, "Integer Operations",
        "define i32 @Integer_toInt32(ptr %this) {\n"
        "  %val_ptr = getelementptr inbounds %Integer, ptr %this, i32 0, i32 0\n"
        "  %val64 = load i64, ptr %val_ptr, align 8\n"
        "  %val32 = trunc i64 %val64 to i32\n"
        "  ret i32 %val32\n"
        "}",
        "NativeType<int32>");

    addFunction("Integer_add", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_subtract", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_multiply", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_divide", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_modulo", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_negate", "ptr", {"ptr"}, "Integer Operations");
    addFunction("Integer_abs", "ptr", {"ptr"}, "Integer Operations");
    addFunction("Integer_equals", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_notEquals", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_lessThan", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_greaterThan", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_lessOrEqual", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_greaterOrEqual", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_not", "ptr", {"ptr"}, "Integer Operations");
    addFunction("Integer_bitwiseAnd", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_bitwiseOr", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_bitwiseXor", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_bitwiseNot", "ptr", {"ptr"}, "Integer Operations");
    addFunction("Integer_shiftLeft", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_shiftRight", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_toString", "ptr", {"ptr"}, "Integer Operations");

    // Integer_hash - inline definition (absolute value for hashcode)
    addDefinition("Integer_hash", "i64", {"ptr"}, "Integer Operations",
        "define i64 @Integer_hash(ptr %this) {\n"
        "  %val_ptr = getelementptr inbounds %Integer, ptr %this, i32 0, i32 0\n"
        "  %val = load i64, ptr %val_ptr, align 8\n"
        "  %is_neg = icmp slt i64 %val, 0\n"
        "  %neg_val = sub i64 0, %val\n"
        "  %result = select i1 %is_neg, i64 %neg_val, i64 %val\n"
        "  ret i64 %result\n"
        "}",
        "NativeType<int64>");

    addFunction("Integer_addAssign", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_subtractAssign", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_multiplyAssign", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_divideAssign", "ptr", {"ptr", "ptr"}, "Integer Operations");
    addFunction("Integer_moduloAssign", "ptr", {"ptr", "ptr"}, "Integer Operations");

    // ============================================
    // Float Operations
    // ============================================
    addFunction("Float_Constructor", "ptr", {"float"}, "Float Operations", "Float^");
    addFunction("Float_getValue", "float", {"ptr"}, "Float Operations", "NativeType<float>");
    addFunction("Float_toString", "ptr", {"ptr"}, "Float Operations");
    addFunction("xxml_float_to_string", "ptr", {"float"}, "Float Operations");
    addFunction("Float_addAssign", "ptr", {"ptr", "ptr"}, "Float Operations");
    addFunction("Float_subtractAssign", "ptr", {"ptr", "ptr"}, "Float Operations");
    addFunction("Float_multiplyAssign", "ptr", {"ptr", "ptr"}, "Float Operations");
    addFunction("Float_divideAssign", "ptr", {"ptr", "ptr"}, "Float Operations");

    // ============================================
    // Double Operations
    // ============================================
    addFunction("Double_Constructor", "ptr", {"double"}, "Double Operations", "Double^");
    addFunction("Double_getValue", "double", {"ptr"}, "Double Operations", "NativeType<double>");
    addFunction("Double_toString", "ptr", {"ptr"}, "Double Operations");
    addFunction("Double_addAssign", "ptr", {"ptr", "ptr"}, "Double Operations");
    addFunction("Double_subtractAssign", "ptr", {"ptr", "ptr"}, "Double Operations");
    addFunction("Double_multiplyAssign", "ptr", {"ptr", "ptr"}, "Double Operations");
    addFunction("Double_divideAssign", "ptr", {"ptr", "ptr"}, "Double Operations");

    // ============================================
    // String Operations
    // ============================================
    addFunction("String_Constructor", "ptr", {"ptr"}, "String Operations", "String^");
    addFunction("String_FromCString", "ptr", {"ptr"}, "String Operations");
    addFunction("String_toCString", "ptr", {"ptr"}, "String Operations");
    addFunction("String_length", "ptr", {"ptr"}, "String Operations");
    addFunction("String_concat", "ptr", {"ptr", "ptr"}, "String Operations");
    addFunction("String_append", "ptr", {"ptr", "ptr"}, "String Operations");
    addFunction("String_equals", "i1", {"ptr", "ptr"}, "String Operations", "NativeType<bool>");
    addFunction("String_isEmpty", "i1", {"ptr"}, "String Operations", "NativeType<bool>");
    addFunction("String_destroy", "void", {"ptr"}, "String Operations");
    addFunction("String_copy", "ptr", {"ptr"}, "String Operations");
    addFunction("String_charAt", "ptr", {"ptr", "ptr"}, "String Operations");
    addFunction("String_setCharAt", "void", {"ptr", "ptr", "ptr"}, "String Operations");

    // Object_append - inline wrapper for String_append
    addDefinition("Object_append", "ptr", {"ptr", "ptr"}, "String Operations",
        "define ptr @Object_append(ptr %this, ptr %other) {\n"
        "  %result = tail call ptr @String_append(ptr %this, ptr %other)\n"
        "  ret ptr %result\n"
        "}");

    // ============================================
    // Bool Operations
    // ============================================
    addFunction("Bool_Constructor", "ptr", {"i1"}, "Bool Operations", "Bool^");
    addFunction("Bool_getValue", "i1", {"ptr"}, "Bool Operations", "NativeType<bool>");
    addFunction("Bool_and", "ptr", {"ptr", "ptr"}, "Bool Operations");
    addFunction("Bool_or", "ptr", {"ptr", "ptr"}, "Bool Operations");
    addFunction("Bool_not", "ptr", {"ptr"}, "Bool Operations");
    addFunction("Bool_xor", "ptr", {"ptr", "ptr"}, "Bool Operations");
    addFunction("Bool_toInteger", "ptr", {"ptr"}, "Bool Operations");
    addFunction("None_Constructor", "ptr", {}, "Bool Operations");
    addFunction("Byte_Constructor", "ptr", {"i8"}, "Bool Operations", "Byte^");

    // ============================================
    // List Operations
    // ============================================
    addFunction("List_Constructor", "ptr", {}, "List Operations");
    addFunction("List_add", "void", {"ptr", "ptr"}, "List Operations");
    addFunction("List_get", "ptr", {"ptr", "i64"}, "List Operations");
    addFunction("List_size", "i64", {"ptr"}, "List Operations");

    // ============================================
    // Console I/O
    // ============================================
    addFunction("Console_print", "void", {"ptr"}, "Console I/O");
    addFunction("Console_printLine", "void", {"ptr"}, "Console I/O");
    addFunction("Console_printInt", "void", {"i64"}, "Console I/O");
    addFunction("Console_printBool", "void", {"i1"}, "Console I/O");

    // ============================================
    // System Functions
    // ============================================
    addFunction("xxml_exit", "void", {"i32"}, "System Functions");
    addFunction("exit", "void", {"i32"}, "System Functions");

    // ============================================
    // Reflection Runtime
    // ============================================
    addFunction("Reflection_registerType", "ptr", {"ptr"}, "Reflection Runtime");
    addFunction("Reflection_getTypeInfo", "ptr", {"ptr"}, "Reflection Runtime");
    addFunction("Reflection_getTypeCount", "i32", {}, "Reflection Runtime", "NativeType<int32>");
    addFunction("Reflection_getAllTypeNames", "ptr", {}, "Reflection Runtime");

    // Reflection Syscall Functions
    addFunction("xxml_reflection_getTypeByName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getFullName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getNamespace", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_isTemplate", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_type_getTemplateParamCount", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_type_getPropertyCount", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_type_getProperty", "ptr", {"ptr", "i64"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getPropertyByName", "ptr", {"ptr", "ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getMethodCount", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_type_getMethod", "ptr", {"ptr", "i64"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getMethodByName", "ptr", {"ptr", "ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getInstanceSize", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_type_getBaseClassName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_hasBaseClass", "i64", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getConstructorCount", "i64", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_getConstructor", "ptr", {"ptr", "i64"}, "Reflection Syscall");
    addFunction("xxml_reflection_property_getName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_property_getTypeName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_property_getOwnership", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_property_getOffset", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_property_getValuePtr", "ptr", {"ptr", "ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_property_setValuePtr", "void", {"ptr", "ptr", "ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_method_getName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_method_getReturnType", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_method_getReturnOwnership", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_method_getParameterCount", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_method_getParameter", "ptr", {"ptr", "i64"}, "Reflection Syscall");
    addFunction("xxml_reflection_method_isStatic", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_method_isConstructor", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_method_invoke", "ptr", {"ptr", "ptr", "ptr", "i64"}, "Reflection Syscall");
    addFunction("xxml_reflection_method_getFunctionPointer", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_parameter_getName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_parameter_getTypeName", "ptr", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_parameter_getOwnership", "i64", {"ptr"}, "Reflection Syscall", "NativeType<int64>");
    addFunction("xxml_reflection_type_isSendable", "i64", {"ptr"}, "Reflection Syscall");
    addFunction("xxml_reflection_type_isSharable", "i64", {"ptr"}, "Reflection Syscall");

    // ============================================
    // Annotation Runtime
    // ============================================
    addFunction("Annotation_registerForType", "void", {"ptr", "i32", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_registerForMethod", "void", {"ptr", "ptr", "i32", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_registerForProperty", "void", {"ptr", "ptr", "i32", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getCountForType", "i32", {"ptr"}, "Annotation Runtime");
    addFunction("Annotation_getForType", "ptr", {"ptr", "i32"}, "Annotation Runtime");
    addFunction("Annotation_getCountForMethod", "i32", {"ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getForMethod", "ptr", {"ptr", "ptr", "i32"}, "Annotation Runtime");
    addFunction("Annotation_getCountForProperty", "i32", {"ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getForProperty", "ptr", {"ptr", "ptr", "i32"}, "Annotation Runtime");
    addFunction("Annotation_typeHas", "i1", {"ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_methodHas", "i1", {"ptr", "ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_propertyHas", "i1", {"ptr", "ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getByNameForType", "ptr", {"ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getByNameForMethod", "ptr", {"ptr", "ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getByNameForProperty", "ptr", {"ptr", "ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getArgument", "ptr", {"ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getIntArg", "i64", {"ptr", "ptr", "i64"}, "Annotation Runtime");
    addFunction("Annotation_getStringArg", "ptr", {"ptr", "ptr", "ptr"}, "Annotation Runtime");
    addFunction("Annotation_getBoolArg", "i1", {"ptr", "ptr", "i1"}, "Annotation Runtime");
    addFunction("Annotation_getDoubleArg", "double", {"ptr", "ptr", "double"}, "Annotation Runtime");

    // ============================================
    // Annotation Arg/Info Syscalls (called by Language::Reflection XXML wrappers)
    // ============================================
    // AnnotationArg Syscalls
    addFunction("xxml_Language_Reflection_AnnotationArg_getName", "ptr", {"ptr"}, "Annotation Arg Syscall");
    addFunction("xxml_Language_Reflection_AnnotationArg_getType", "i64", {"ptr"}, "Annotation Arg Syscall");
    addFunction("xxml_Language_Reflection_AnnotationArg_asInteger", "i64", {"ptr"}, "Annotation Arg Syscall");
    addFunction("xxml_Language_Reflection_AnnotationArg_asString", "ptr", {"ptr"}, "Annotation Arg Syscall");
    addFunction("xxml_Language_Reflection_AnnotationArg_asBool", "i64", {"ptr"}, "Annotation Arg Syscall");
    addFunction("xxml_Language_Reflection_AnnotationArg_asDouble", "double", {"ptr"}, "Annotation Arg Syscall");

    // AnnotationInfo Syscalls
    addFunction("xxml_Language_Reflection_AnnotationInfo_getName", "ptr", {"ptr"}, "Annotation Info Syscall");
    addFunction("xxml_Language_Reflection_AnnotationInfo_getArgumentCount", "i64", {"ptr"}, "Annotation Info Syscall");
    addFunction("xxml_Language_Reflection_AnnotationInfo_getArgument", "ptr", {"ptr", "i64"}, "Annotation Info Syscall");
    addFunction("xxml_Language_Reflection_AnnotationInfo_getArgumentByName", "ptr", {"ptr", "ptr"}, "Annotation Info Syscall");
    addFunction("xxml_Language_Reflection_AnnotationInfo_hasArgument", "i64", {"ptr", "ptr"}, "Annotation Info Syscall");

    // NOTE: Annotation Info and Arg Language Bindings (Language_Reflection_*)
    // are now compiled from XXML since Language::Reflection is treated as a
    // non-runtime module (has XXML code). The XXML classes wrap the C runtime
    // annotation functions (xxml_* prefix from "Annotation Runtime" above).

    // ============================================
    // Processor API
    // ============================================
    addFunction("xxml_Processor_getTargetKind", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getTargetName", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getTypeName", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getClassName", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getNamespaceName", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getSourceFile", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getLineNumber", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getColumnNumber", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getPropertyCount", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getPropertyNameAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getPropertyTypeAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getPropertyOwnershipAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getMethodCount", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getMethodNameAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getMethodReturnTypeAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_hasMethod", "i64", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_hasProperty", "i64", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_getBaseClassName", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_isClassFinal", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getParameterCount", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getParameterNameAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getParameterTypeAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getReturnTypeName", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_isMethodStatic", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_hasDefaultValue", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getOwnership", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getTargetValue", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_hasTargetValue", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getTargetValueType", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_message", "void", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_warning", "void", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_warningAt", "void", {"ptr", "ptr", "ptr", "i64", "i64"}, "Processor API");
    addFunction("xxml_Processor_error", "void", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_errorAt", "void", {"ptr", "ptr", "ptr", "i64", "i64"}, "Processor API");
    addFunction("xxml_Processor_argGetName", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_argAsInt", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_argAsString", "ptr", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_argAsBool", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_argAsDouble", "double", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getArgCount", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getArgAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getArg", "ptr", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationArgCount", "i64", {"ptr"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationArg", "ptr", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationArgNameAt", "ptr", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationArgTypeAt", "i64", {"ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_hasAnnotationArg", "i64", {"ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationIntArg", "i64", {"ptr", "ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationStringArg", "ptr", {"ptr", "ptr", "ptr"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationBoolArg", "i64", {"ptr", "ptr", "i64"}, "Processor API");
    addFunction("xxml_Processor_getAnnotationDoubleArg", "double", {"ptr", "ptr", "double"}, "Processor API");

    // ============================================
    // Derive API
    // ============================================
    addFunction("xxml_Derive_getClassName", "ptr", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_getNamespaceName", "ptr", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_getSourceFile", "ptr", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_getLineNumber", "i64", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_getColumnNumber", "i64", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_getPropertyCount", "i64", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_getPropertyNameAt", "ptr", {"ptr", "i64"}, "Derive API");
    addFunction("xxml_Derive_getPropertyTypeAt", "ptr", {"ptr", "i64"}, "Derive API");
    addFunction("xxml_Derive_getPropertyOwnershipAt", "ptr", {"ptr", "i64"}, "Derive API");
    addFunction("xxml_Derive_hasProperty", "ptr", {"ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_getMethodCount", "i64", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_getMethodNameAt", "ptr", {"ptr", "i64"}, "Derive API");
    addFunction("xxml_Derive_getMethodReturnTypeAt", "ptr", {"ptr", "i64"}, "Derive API");
    addFunction("xxml_Derive_hasMethod", "ptr", {"ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_getBaseClassName", "ptr", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_isClassFinal", "ptr", {"ptr"}, "Derive API");
    addFunction("xxml_Derive_typeHasMethod", "ptr", {"ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_typeHasProperty", "ptr", {"ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_typeImplementsTrait", "ptr", {"ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_isBuiltinType", "ptr", {"ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_addMethod", "ptr", {"ptr", "ptr", "ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_addStaticMethod", "ptr", {"ptr", "ptr", "ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_addMethodAST", "ptr", {"ptr", "ptr", "ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_addStaticMethodAST", "ptr", {"ptr", "ptr", "ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_addProperty", "ptr", {"ptr", "ptr", "ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_substituteSplice", "ptr", {"ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_substituteSpliceAll", "ptr", {"ptr", "ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_error", "void", {"ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_warning", "void", {"ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_message", "void", {"ptr", "ptr"}, "Derive API");
    addFunction("xxml_Derive_hasErrors", "ptr", {"ptr"}, "Derive API");

    // ============================================
    // Dynamic Value Methods
    // ============================================
    addFunction("__DynamicValue_toString", "ptr", {"ptr"}, "Dynamic Value");
    addFunction("__DynamicValue_greaterThan", "i1", {"ptr", "ptr"}, "Dynamic Value");
    addFunction("__DynamicValue_lessThan", "i1", {"ptr", "ptr"}, "Dynamic Value");
    addFunction("__DynamicValue_equals", "i1", {"ptr", "ptr"}, "Dynamic Value");
    addFunction("__DynamicValue_add", "ptr", {"ptr", "ptr"}, "Dynamic Value");
    addFunction("__DynamicValue_sub", "ptr", {"ptr", "ptr"}, "Dynamic Value");
    addFunction("__DynamicValue_mul", "ptr", {"ptr", "ptr"}, "Dynamic Value");
    addFunction("__DynamicValue_div", "ptr", {"ptr", "ptr"}, "Dynamic Value");

    // ============================================
    // Utility Functions
    // ============================================
    addFunction("xxml_string_create", "ptr", {"ptr"}, "Utility Functions");
    addFunction("xxml_string_concat", "ptr", {"ptr", "ptr"}, "Utility Functions");
    addFunction("xxml_string_hash", "i64", {"ptr"}, "Utility Functions", "NativeType<int64>");
    addFunction("xxml_string_cstr", "ptr", {"ptr"}, "Utility Functions");
    addFunction("xxml_string_length", "i64", {"ptr"}, "Utility Functions");
    addFunction("xxml_string_copy", "ptr", {"ptr"}, "Utility Functions");
    addFunction("xxml_string_equals", "i64", {"ptr", "ptr"}, "Utility Functions");
    addFunction("xxml_string_startsWith", "i64", {"ptr", "ptr"}, "Utility Functions");
    addFunction("xxml_string_charAt", "ptr", {"ptr", "i64"}, "Utility Functions");
    addFunction("xxml_string_setCharAt", "void", {"ptr", "i64", "ptr"}, "Utility Functions");
    addFunction("xxml_string_destroy", "void", {"ptr"}, "Utility Functions");
    addFunction("xxml_string_replace", "ptr", {"ptr", "ptr", "ptr"}, "Utility Functions");
    addFunction("String_replace", "ptr", {"ptr", "ptr", "ptr"}, "Utility Functions");
    addFunction("xxml_splice_wrap_string", "ptr", {"ptr"}, "Utility Functions");
    addFunction("xxml_ptr_is_null", "i64", {"ptr"}, "Utility Functions", "NativeType<int64>");
    addFunction("xxml_ptr_null", "ptr", {}, "Utility Functions");

    // ============================================
    // Threading Functions
    // ============================================
    addFunction("xxml_Thread_create", "ptr", {"ptr", "ptr"}, "Threading");
    addFunction("xxml_Thread_join", "i64", {"ptr"}, "Threading");
    addFunction("xxml_Thread_detach", "i64", {"ptr"}, "Threading");
    addFunction("xxml_Thread_isJoinable", "i1", {"ptr"}, "Threading");
    addFunction("xxml_Thread_sleep", "void", {"i64"}, "Threading");
    addFunction("xxml_Thread_yield", "void", {}, "Threading");
    addFunction("xxml_Thread_currentId", "i64", {}, "Threading");
    addFunction("xxml_Thread_spawn_lambda", "ptr", {"ptr"}, "Threading");

    // ============================================
    // Mutex Functions
    // ============================================
    addFunction("xxml_Mutex_create", "ptr", {}, "Mutex");
    addFunction("xxml_Mutex_destroy", "void", {"ptr"}, "Mutex");
    addFunction("xxml_Mutex_lock", "i64", {"ptr"}, "Mutex");
    addFunction("xxml_Mutex_unlock", "i64", {"ptr"}, "Mutex");
    addFunction("xxml_Mutex_tryLock", "i1", {"ptr"}, "Mutex");

    // ============================================
    // Condition Variable Functions
    // ============================================
    addFunction("xxml_CondVar_create", "ptr", {}, "Condition Variable");
    addFunction("xxml_CondVar_destroy", "void", {"ptr"}, "Condition Variable");
    addFunction("xxml_CondVar_wait", "i64", {"ptr", "ptr"}, "Condition Variable");
    addFunction("xxml_CondVar_waitTimeout", "i64", {"ptr", "ptr", "i64"}, "Condition Variable");
    addFunction("xxml_CondVar_signal", "i64", {"ptr"}, "Condition Variable");
    addFunction("xxml_CondVar_broadcast", "i64", {"ptr"}, "Condition Variable");

    // ============================================
    // Atomic Functions
    // ============================================
    addFunction("xxml_Atomic_create", "ptr", {"i64"}, "Atomic");
    addFunction("xxml_Atomic_destroy", "void", {"ptr"}, "Atomic");
    addFunction("xxml_Atomic_load", "i64", {"ptr"}, "Atomic");
    addFunction("xxml_Atomic_store", "void", {"ptr", "i64"}, "Atomic");
    addFunction("xxml_Atomic_add", "i64", {"ptr", "i64"}, "Atomic");
    addFunction("xxml_Atomic_sub", "i64", {"ptr", "i64"}, "Atomic");
    addFunction("xxml_Atomic_compareAndSwap", "i1", {"ptr", "i64", "i64"}, "Atomic");
    addFunction("xxml_Atomic_exchange", "i64", {"ptr", "i64"}, "Atomic");

    // ============================================
    // TLS Functions
    // ============================================
    addFunction("xxml_TLS_create", "ptr", {}, "TLS");
    addFunction("xxml_TLS_destroy", "void", {"ptr"}, "TLS");
    addFunction("xxml_TLS_get", "ptr", {"ptr"}, "TLS");
    addFunction("xxml_TLS_set", "void", {"ptr", "ptr"}, "TLS");

    // ============================================
    // File I/O Functions
    // ============================================
    addFunction("xxml_File_open", "ptr", {"ptr", "ptr"}, "File I/O");
    addFunction("xxml_File_close", "void", {"ptr"}, "File I/O");
    addFunction("xxml_File_read", "i64", {"ptr", "ptr", "i64"}, "File I/O");
    addFunction("xxml_File_write", "i64", {"ptr", "ptr", "i64"}, "File I/O");
    addFunction("xxml_File_readLine", "ptr", {"ptr"}, "File I/O");
    addFunction("xxml_File_writeString", "i64", {"ptr", "ptr"}, "File I/O");
    addFunction("xxml_File_writeLine", "i64", {"ptr", "ptr"}, "File I/O");
    addFunction("xxml_File_readAll", "ptr", {"ptr"}, "File I/O");
    addFunction("xxml_File_seek", "i64", {"ptr", "i64", "i64"}, "File I/O");
    addFunction("xxml_File_tell", "i64", {"ptr"}, "File I/O");
    addFunction("xxml_File_size", "i64", {"ptr"}, "File I/O");
    addFunction("xxml_File_eof", "i1", {"ptr"}, "File I/O");
    addFunction("xxml_File_flush", "i64", {"ptr"}, "File I/O");
    addFunction("xxml_File_exists", "i1", {"ptr"}, "File I/O");
    addFunction("xxml_File_delete", "i1", {"ptr"}, "File I/O");
    addFunction("xxml_File_rename", "i1", {"ptr", "ptr"}, "File I/O");
    addFunction("xxml_File_copy", "i1", {"ptr", "ptr"}, "File I/O");
    addFunction("xxml_File_sizeByPath", "i64", {"ptr"}, "File I/O");
    addFunction("xxml_File_readAllByPath", "ptr", {"ptr"}, "File I/O");

    // ============================================
    // Directory Functions
    // ============================================
    addFunction("xxml_Dir_create", "i1", {"ptr"}, "Directory");
    addFunction("xxml_Dir_exists", "i1", {"ptr"}, "Directory");
    addFunction("xxml_Dir_delete", "i1", {"ptr"}, "Directory");
    addFunction("xxml_Dir_getCurrent", "ptr", {}, "Directory");
    addFunction("xxml_Dir_setCurrent", "i1", {"ptr"}, "Directory");

    // ============================================
    // Path Functions
    // ============================================
    addFunction("xxml_Path_join", "ptr", {"ptr", "ptr"}, "Path");
    addFunction("xxml_Path_getFileName", "ptr", {"ptr"}, "Path");
    addFunction("xxml_Path_getDirectory", "ptr", {"ptr"}, "Path");
    addFunction("xxml_Path_getExtension", "ptr", {"ptr"}, "Path");
    addFunction("xxml_Path_isAbsolute", "i1", {"ptr"}, "Path");
    addFunction("xxml_Path_getAbsolute", "ptr", {"ptr"}, "Path");

    // ============================================
    // FFI Runtime Functions
    // ============================================
    addFunction("xxml_FFI_loadLibrary", "ptr", {"ptr"}, "FFI Runtime");
    addFunction("xxml_FFI_getSymbol", "ptr", {"ptr", "ptr"}, "FFI Runtime");
    addFunction("xxml_FFI_freeLibrary", "void", {"ptr"}, "FFI Runtime");
    addFunction("xxml_FFI_getError", "ptr", {}, "FFI Runtime");
    addFunction("xxml_FFI_libraryExists", "i1", {"ptr"}, "FFI Runtime");

    // FFI Type Conversion
    addFunction("xxml_FFI_stringToCString", "ptr", {"ptr"}, "FFI Type Conversion");
    addFunction("xxml_FFI_cstringToString", "ptr", {"ptr"}, "FFI Type Conversion");
    addFunction("xxml_FFI_integerToInt64", "i64", {"ptr"}, "FFI Type Conversion");
    addFunction("xxml_FFI_int64ToInteger", "ptr", {"i64"}, "FFI Type Conversion");
    addFunction("xxml_FFI_floatToC", "float", {"ptr"}, "FFI Type Conversion");
    addFunction("xxml_FFI_cToFloat", "ptr", {"float"}, "FFI Type Conversion");
    addFunction("xxml_FFI_doubleToC", "double", {"ptr"}, "FFI Type Conversion");
    addFunction("xxml_FFI_cToDouble", "ptr", {"double"}, "FFI Type Conversion");
    addFunction("xxml_FFI_boolToC", "i1", {"ptr"}, "FFI Type Conversion");
    addFunction("xxml_FFI_cToBool", "ptr", {"i1"}, "FFI Type Conversion");

    // NOTE: Language::Reflection API (Language_Reflection_Type_*, etc.) are now
    // compiled from XXML since Language::Reflection is treated as a non-runtime
    // module (has XXML code). These XXML classes wrap the C runtime reflection
    // functions (reflection_* prefix from "Reflection Runtime" above).
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
