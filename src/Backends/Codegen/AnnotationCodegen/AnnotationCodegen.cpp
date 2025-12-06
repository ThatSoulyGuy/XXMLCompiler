#include "Backends/Codegen/AnnotationCodegen/AnnotationCodegen.h"

namespace XXML {
namespace Backends {
namespace Codegen {

AnnotationCodegen::AnnotationCodegen(CodegenContext& ctx)
    : ctx_(ctx) {
    // Initialize the type-safe builder
    globalBuilder_ = std::make_unique<LLVMIR::GlobalBuilder>(ctx_.module());
}

void AnnotationCodegen::initializeAnnotationTypes() {
    if (annotationArgType_ != nullptr) {
        return;  // Already initialized
    }

    LLVMIR::TypeContext& typeCtx = ctx_.module().getContext();

    // AnnotationArg = { ptr (name), i32 (kind), i64 (value) }
    // For strings, the i64 holds a pointer; for other types, the raw bits
    annotationArgType_ = globalBuilder_->getOrCreateStruct("AnnotationArg");
    if (annotationArgType_->isOpaque()) {
        std::vector<LLVMIR::Type*> fields = {
            typeCtx.getPtrTy(),   // name
            typeCtx.getInt32Ty(), // kind (0=int, 1=string, 2=bool, 3=float, 4=double)
            typeCtx.getInt64Ty()  // value (raw bits or pointer)
        };
        annotationArgType_->setBody(std::move(fields));
    }

    // AnnotationInfo = { ptr (name), i32 (argCount), ptr (args array) }
    annotationInfoType_ = globalBuilder_->getOrCreateStruct("AnnotationInfo");
    if (annotationInfoType_->isOpaque()) {
        std::vector<LLVMIR::Type*> fields = {
            typeCtx.getPtrTy(),   // annotation name
            typeCtx.getInt32Ty(), // argument count
            typeCtx.getPtrTy()    // pointer to args array
        };
        annotationInfoType_->setBody(std::move(fields));
    }
}

std::vector<AnnotationCodegen::TargetAnnotations> AnnotationCodegen::groupAnnotationsByTarget() {
    std::vector<TargetAnnotations> groupedAnnotations;
    const auto& annotations = ctx_.annotationMetadata();

    for (const auto& meta : annotations) {
        bool found = false;
        for (auto& group : groupedAnnotations) {
            if (group.targetType == meta.targetType &&
                group.typeName == meta.typeName &&
                group.memberName == meta.memberName) {
                group.annotations.push_back(&meta);
                found = true;
                break;
            }
        }
        if (!found) {
            TargetAnnotations newGroup;
            newGroup.targetType = meta.targetType;
            newGroup.typeName = meta.typeName;
            newGroup.memberName = meta.memberName;
            newGroup.annotations.push_back(&meta);
            groupedAnnotations.push_back(newGroup);
        }
    }

    return groupedAnnotations;
}

void AnnotationCodegen::generateAnnotationGroup(int groupId, const TargetAnnotations& group) {
    LLVMIR::TypeContext& typeCtx = ctx_.module().getContext();

    // For each annotation in the group, create the metadata structures
    for (size_t i = 0; i < group.annotations.size(); ++i) {
        const auto* meta = group.annotations[i];
        std::string prefix = "__annotation_" + std::to_string(groupId) + "_" + std::to_string(i);

        // Create annotation name string constant
        LLVMIR::GlobalVariable* nameStr = globalBuilder_->createStringConstant(meta->annotationName);

        // Create argument structures if there are any
        if (!meta->arguments.empty()) {
            // Create argument array type
            LLVMIR::ArrayType* argsArrayType = typeCtx.getArrayTy(
                annotationArgType_,
                meta->arguments.size()
            );

            // Create array global (the emitter will handle initialization)
            std::string argsArrayName = prefix + "_args";
            LLVMIR::GlobalVariable* argsArray = ctx_.module().createGlobal(
                argsArrayType,
                argsArrayName,
                LLVMIR::GlobalVariable::Linkage::Private
            );
            argsArray->setConstant(true);

            // For each argument, create string constants for names and string values
            for (size_t j = 0; j < meta->arguments.size(); ++j) {
                const auto& arg = meta->arguments[j];
                std::string argPrefix = prefix + "_arg_" + std::to_string(j);

                // Create argument name string
                globalBuilder_->createStringConstant(arg.first);

                // If it's a string value, create the string constant
                if (arg.second.kind == AnnotationArgValue::String) {
                    globalBuilder_->createStringConstant(arg.second.stringValue);
                }
            }
        }

        // Create the annotation info struct
        std::string infoName = prefix + "_info";
        LLVMIR::GlobalVariable* infoGlobal = ctx_.module().createGlobal(
            annotationInfoType_,
            infoName,
            LLVMIR::GlobalVariable::Linkage::Private
        );
        infoGlobal->setConstant(true);

        // Suppress unused variable warnings in release builds
        (void)nameStr;
        (void)infoGlobal;
    }

    // Create infos array for this group
    if (!group.annotations.empty()) {
        LLVMIR::ArrayType* infosArrayType = typeCtx.getArrayTy(
            typeCtx.getPtrTy(),
            group.annotations.size()
        );

        std::string infosArrayName = "__annotation_" + std::to_string(groupId) + "_infos";
        LLVMIR::GlobalVariable* infosArray = ctx_.module().createGlobal(
            infosArrayType,
            infosArrayName,
            LLVMIR::GlobalVariable::Linkage::Private
        );
        infosArray->setConstant(true);
        (void)infosArray;
    }

    // Create type name string
    globalBuilder_->createStringConstant(group.typeName);

    // Create member name string if needed
    if (!group.memberName.empty()) {
        globalBuilder_->createStringConstant(group.memberName);
    }
}

void AnnotationCodegen::generate() {
    const auto& annotations = ctx_.annotationMetadata();
    if (annotations.empty()) {
        return;
    }

    // Initialize struct types
    initializeAnnotationTypes();

    // Group annotations by target and generate metadata
    auto groupedAnnotations = groupAnnotationsByTarget();

    for (size_t i = 0; i < groupedAnnotations.size(); ++i) {
        generateAnnotationGroup(static_cast<int>(i), groupedAnnotations[i]);
    }

    // All metadata is now in the Module and will be emitted via LLVMEmitter
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
