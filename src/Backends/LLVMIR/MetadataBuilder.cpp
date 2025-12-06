#include "Backends/LLVMIR/MetadataBuilder.h"
#include <stdexcept>
#include <algorithm>

namespace XXML {
namespace Backends {
namespace LLVMIR {

// ============================================================================
// Helper Functions
// ============================================================================

OwnershipKind parseOwnership(std::string_view marker) {
    if (marker.empty()) return OwnershipKind::Unknown;
    char c = marker[0];
    if (c == '^') return OwnershipKind::Owned;
    if (c == '&') return OwnershipKind::Reference;
    if (c == '%') return OwnershipKind::Copy;
    return OwnershipKind::Unknown;
}

// ============================================================================
// MetadataBuilder Implementation
// ============================================================================

MetadataBuilder::MetadataBuilder(Module& module, GlobalBuilder& globalBuilder)
    : module_(module), globalBuilder_(globalBuilder) {
}

// ============================================================================
// Struct Type Initialization
// ============================================================================

void MetadataBuilder::initializeReflectionTypes() {
    if (areReflectionTypesInitialized()) {
        return;
    }

    TypeContext& ctx = module_.getContext();

    // %ReflectionPropertyInfo = type { ptr, ptr, i32, i64 }
    // (name, type, ownership, offset)
    propertyInfoType_ = globalBuilder_.getOrCreateStruct("ReflectionPropertyInfo");
    if (propertyInfoType_->isOpaque()) {
        std::vector<Type*> fields = {
            ctx.getPtrTy(),   // name
            ctx.getPtrTy(),   // type
            ctx.getInt32Ty(), // ownership
            ctx.getInt64Ty()  // offset
        };
        propertyInfoType_->setBody(std::move(fields));
    }

    // %ReflectionParameterInfo = type { ptr, ptr, i32 }
    // (name, type, ownership)
    parameterInfoType_ = globalBuilder_.getOrCreateStruct("ReflectionParameterInfo");
    if (parameterInfoType_->isOpaque()) {
        std::vector<Type*> fields = {
            ctx.getPtrTy(),   // name
            ctx.getPtrTy(),   // type
            ctx.getInt32Ty()  // ownership
        };
        parameterInfoType_->setBody(std::move(fields));
    }

    // %ReflectionMethodInfo = type { ptr, ptr, i32, i32, ptr, ptr, i1, i1 }
    // (name, returnType, returnOwnership, paramCount, params, funcPtr, isStatic, isCtor)
    methodInfoType_ = globalBuilder_.getOrCreateStruct("ReflectionMethodInfo");
    if (methodInfoType_->isOpaque()) {
        std::vector<Type*> fields = {
            ctx.getPtrTy(),   // name
            ctx.getPtrTy(),   // returnType
            ctx.getInt32Ty(), // returnOwnership
            ctx.getInt32Ty(), // paramCount
            ctx.getPtrTy(),   // params
            ctx.getPtrTy(),   // funcPtr
            ctx.getInt1Ty(),  // isStatic
            ctx.getInt1Ty()   // isCtor
        };
        methodInfoType_->setBody(std::move(fields));
    }

    // %ReflectionTemplateParamInfo = type { ptr }
    // (name)
    templateParamInfoType_ = globalBuilder_.getOrCreateStruct("ReflectionTemplateParamInfo");
    if (templateParamInfoType_->isOpaque()) {
        std::vector<Type*> fields = {
            ctx.getPtrTy()  // name
        };
        templateParamInfoType_->setBody(std::move(fields));
    }

    // %ReflectionTypeInfo = type { ptr, ptr, ptr, i1, i32, ptr, i32, ptr, i32, ptr, i32, ptr, ptr, i64 }
    // (name, namespace, fullName, isTemplate, tparamCount, tparams, propCount, props, methodCount, methods, baseCount, bases, createInstance, instanceSize)
    typeInfoType_ = globalBuilder_.getOrCreateStruct("ReflectionTypeInfo");
    if (typeInfoType_->isOpaque()) {
        std::vector<Type*> fields = {
            ctx.getPtrTy(),   // name
            ctx.getPtrTy(),   // namespace
            ctx.getPtrTy(),   // fullName
            ctx.getInt1Ty(),  // isTemplate
            ctx.getInt32Ty(), // templateParamCount
            ctx.getPtrTy(),   // templateParams
            ctx.getInt32Ty(), // propertyCount
            ctx.getPtrTy(),   // properties
            ctx.getInt32Ty(), // methodCount
            ctx.getPtrTy(),   // methods
            ctx.getInt32Ty(), // baseCount
            ctx.getPtrTy(),   // bases
            ctx.getPtrTy(),   // createInstance
            ctx.getInt64Ty()  // instanceSize
        };
        typeInfoType_->setBody(std::move(fields));
    }
}

bool MetadataBuilder::areReflectionTypesInitialized() const {
    return propertyInfoType_ != nullptr &&
           parameterInfoType_ != nullptr &&
           methodInfoType_ != nullptr &&
           templateParamInfoType_ != nullptr &&
           typeInfoType_ != nullptr;
}

// ============================================================================
// Get Reflection Struct Types
// ============================================================================

StructType* MetadataBuilder::getPropertyInfoType() {
    if (!propertyInfoType_) initializeReflectionTypes();
    return propertyInfoType_;
}

StructType* MetadataBuilder::getParameterInfoType() {
    if (!parameterInfoType_) initializeReflectionTypes();
    return parameterInfoType_;
}

StructType* MetadataBuilder::getMethodInfoType() {
    if (!methodInfoType_) initializeReflectionTypes();
    return methodInfoType_;
}

StructType* MetadataBuilder::getTemplateParamInfoType() {
    if (!templateParamInfoType_) initializeReflectionTypes();
    return templateParamInfoType_;
}

StructType* MetadataBuilder::getTypeInfoType() {
    if (!typeInfoType_) initializeReflectionTypes();
    return typeInfoType_;
}

// ============================================================================
// Metadata Generation
// ============================================================================

ReflectionMetadataResult MetadataBuilder::generateClassMetadata(const ReflectionClassInfo& info) {
    ReflectionMetadataResult result;

    // Ensure reflection types are initialized
    initializeReflectionTypes();

    // Mangle the class name for use in global variable names
    std::string mangledName = mangleName(info.fullName);

    // Create parameter arrays for each method first
    for (size_t m = 0; m < info.methods.size(); ++m) {
        const auto& method = info.methods[m];
        if (!method.parameters.empty()) {
            GlobalVariable* paramArray = createParameterArray(mangledName, m, method.parameters);
            result.methodParamArrays.push_back(paramArray);
        } else {
            result.methodParamArrays.push_back(nullptr);
        }
    }

    // Create property array
    if (!info.properties.empty()) {
        result.propertiesArray = createPropertyArray(mangledName, info.properties);
    }

    // Create method array
    if (!info.methods.empty()) {
        result.methodsArray = createMethodArray(mangledName, info.methods, result.methodParamArrays);
    }

    // Create template param array
    if (!info.templateParams.empty()) {
        result.templateParamsArray = createTemplateParamArray(mangledName, info.templateParams);
    }

    // Create the main type info
    result.typeInfo = createTypeInfo(mangledName, info,
                                     result.propertiesArray,
                                     result.methodsArray,
                                     result.templateParamsArray);

    return result;
}

std::vector<ReflectionMetadataResult> MetadataBuilder::generateAllMetadata(
    std::span<const ReflectionClassInfo> classes) {

    std::vector<ReflectionMetadataResult> results;
    results.reserve(classes.size());

    for (const auto& classInfo : classes) {
        results.push_back(generateClassMetadata(classInfo));
    }

    return results;
}

// ============================================================================
// Individual Component Builders
// ============================================================================

GlobalVariable* MetadataBuilder::createPropertyArray(
    std::string_view mangledClassName,
    std::span<const ReflectionProperty> properties) {

    if (properties.empty()) return nullptr;

    // Create array type
    ArrayType* arrayType = module_.getContext().getArrayTy(
        getPropertyInfoType(),
        properties.size()
    );

    // Create global variable name
    std::string globalName = "reflection_props_" + std::string(mangledClassName);

    // Create the global (initializer will be handled by emitter)
    GlobalVariable* gv = module_.createGlobal(
        arrayType,
        globalName,
        GlobalVariable::Linkage::Private
    );
    gv->setConstant(true);

    // Note: The actual struct initialization values need to be stored somewhere
    // for the emitter to generate. For now, we rely on the emitter having
    // access to the ReflectionClassInfo separately.

    return gv;
}

GlobalVariable* MetadataBuilder::createParameterArray(
    std::string_view mangledClassName,
    size_t methodIndex,
    std::span<const ReflectionParameter> parameters) {

    if (parameters.empty()) return nullptr;

    // Create array type
    ArrayType* arrayType = module_.getContext().getArrayTy(
        getParameterInfoType(),
        parameters.size()
    );

    // Create global variable name
    std::string globalName = "reflection_method_" + std::string(mangledClassName) +
                             "_params_" + std::to_string(methodIndex);

    // Create the global
    GlobalVariable* gv = module_.createGlobal(
        arrayType,
        globalName,
        GlobalVariable::Linkage::Private
    );
    gv->setConstant(true);

    return gv;
}

GlobalVariable* MetadataBuilder::createMethodArray(
    std::string_view mangledClassName,
    std::span<const ReflectionMethod> methods,
    const std::vector<GlobalVariable*>& paramArrays) {

    if (methods.empty()) return nullptr;

    // Create array type
    ArrayType* arrayType = module_.getContext().getArrayTy(
        getMethodInfoType(),
        methods.size()
    );

    // Create global variable name
    std::string globalName = "reflection_methods_" + std::string(mangledClassName);

    // Create the global
    GlobalVariable* gv = module_.createGlobal(
        arrayType,
        globalName,
        GlobalVariable::Linkage::Private
    );
    gv->setConstant(true);

    return gv;
}

GlobalVariable* MetadataBuilder::createTemplateParamArray(
    std::string_view mangledClassName,
    std::span<const std::string> templateParams) {

    if (templateParams.empty()) return nullptr;

    // Create array type
    ArrayType* arrayType = module_.getContext().getArrayTy(
        getTemplateParamInfoType(),
        templateParams.size()
    );

    // Create global variable name
    std::string globalName = "reflection_tparams_" + std::string(mangledClassName);

    // Create the global
    GlobalVariable* gv = module_.createGlobal(
        arrayType,
        globalName,
        GlobalVariable::Linkage::Private
    );
    gv->setConstant(true);

    return gv;
}

GlobalVariable* MetadataBuilder::createTypeInfo(
    std::string_view mangledClassName,
    const ReflectionClassInfo& info,
    GlobalVariable* propsArray,
    GlobalVariable* methodsArray,
    GlobalVariable* tparamsArray) {

    // Create global variable name
    std::string globalName = "reflection_typeinfo_" + std::string(mangledClassName);

    // Create the global
    GlobalVariable* gv = module_.createGlobal(
        getTypeInfoType(),
        globalName,
        GlobalVariable::Linkage::Private
    );
    gv->setConstant(true);

    return gv;
}

// ============================================================================
// String Helpers
// ============================================================================

GlobalVariable* MetadataBuilder::getOrCreateString(std::string_view content) {
    std::string contentStr(content);

    auto it = stringCache_.find(contentStr);
    if (it != stringCache_.end()) {
        return it->second;
    }

    GlobalVariable* gv = globalBuilder_.createStringConstant(content);
    stringCache_[contentStr] = gv;
    return gv;
}

std::string MetadataBuilder::mangleName(std::string_view fullName) {
    std::string mangledName(fullName);
    for (char& c : mangledName) {
        if (c == ':' || c == '<' || c == '>' || c == ',' || c == ' ') {
            c = '_';
        }
    }
    return mangledName;
}

} // namespace LLVMIR
} // namespace Backends
} // namespace XXML
