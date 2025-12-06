#include "Backends/Codegen/AnnotationCodegen/AnnotationCodegen.h"
#include <format>

namespace XXML {
namespace Backends {
namespace Codegen {

AnnotationCodegen::AnnotationCodegen(CodegenContext& ctx)
    : ctx_(ctx) {}

void AnnotationCodegen::emitLine(const std::string& line) {
    output_ << line << "\n";
}

std::string AnnotationCodegen::escapeString(const std::string& str) const {
    std::string result;
    for (char c : str) {
        if (c == '\\') result += "\\\\";
        else if (c == '"') result += "\\22";
        else if (c == '\n') result += "\\0A";
        else if (c == '\r') result += "\\0D";
        else if (c == '\t') result += "\\09";
        else result += c;
    }
    return result;
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

void AnnotationCodegen::emitAnnotationGroup(int groupId, const TargetAnnotations& group) {
    std::vector<std::string> annotationInfoNames;

    for (size_t i = 0; i < group.annotations.size(); ++i) {
        const auto* meta = group.annotations[i];
        std::string prefix = std::format("@__annotation_{}_{}", groupId, i);

        // Generate annotation name string
        std::string nameStr = prefix + "_name";
        emitLine(std::format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                             nameStr, meta->annotationName.length() + 1, escapeString(meta->annotationName)));

        // Generate arguments array if there are any
        std::string argsArrayName = "null";
        if (!meta->arguments.empty()) {
            std::vector<std::string> argStructNames;

            for (size_t j = 0; j < meta->arguments.size(); ++j) {
                const auto& arg = meta->arguments[j];
                std::string argPrefix = std::format("{}_arg_{}", prefix, j);

                // Generate argument name string
                std::string argNameStr = argPrefix + "_name";
                emitLine(std::format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                                     argNameStr, arg.first.length() + 1, escapeString(arg.first)));

                // Generate argument value based on type
                std::string argStructName = argPrefix + "_struct";
                int valueType = static_cast<int>(arg.second.kind);
                std::string valueInit;
                std::string structType;

                switch (arg.second.kind) {
                    case AnnotationArgValue::Integer:
                        valueInit = std::format("i64 {}", arg.second.intValue);
                        structType = "{ ptr, i32, i64 }";
                        break;
                    case AnnotationArgValue::String: {
                        std::string strValName = argPrefix + "_strval";
                        emitLine(std::format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                                             strValName, arg.second.stringValue.length() + 1, escapeString(arg.second.stringValue)));
                        valueInit = std::format("ptr {}", strValName);
                        structType = "{ ptr, i32, ptr }";
                        break;
                    }
                    case AnnotationArgValue::Bool:
                        valueInit = std::format("i64 {}", arg.second.boolValue ? 1 : 0);
                        structType = "{ ptr, i32, i64 }";
                        break;
                    case AnnotationArgValue::Float:
                        valueInit = std::format("i64 {}", *reinterpret_cast<const uint32_t*>(&arg.second.floatValue));
                        structType = "{ ptr, i32, i64 }";
                        break;
                    case AnnotationArgValue::Double:
                        valueInit = std::format("i64 {}", *reinterpret_cast<const uint64_t*>(&arg.second.doubleValue));
                        structType = "{ ptr, i32, i64 }";
                        break;
                }

                emitLine(std::format("{} = private unnamed_addr constant {} {{ ptr {}, i32 {}, {} }}",
                                     argStructName, structType, argNameStr, valueType, valueInit));
                argStructNames.push_back(argStructName);
            }

            // Generate array of argument pointers
            argsArrayName = prefix + "_args";
            std::stringstream argsInit;
            argsInit << "[ ";
            for (size_t j = 0; j < argStructNames.size(); ++j) {
                if (j > 0) argsInit << ", ";
                argsInit << "ptr " << argStructNames[j];
            }
            argsInit << " ]";
            emitLine(std::format("{} = private unnamed_addr constant [{} x ptr] {}",
                                 argsArrayName, argStructNames.size(), argsInit.str()));
        }

        // Generate ReflectionAnnotationInfo struct
        std::string infoName = prefix + "_info";
        emitLine(std::format("{} = private unnamed_addr constant {{ ptr, i32, ptr }} {{ ptr {}, i32 {}, ptr {} }}",
                             infoName, nameStr, meta->arguments.size(),
                             meta->arguments.empty() ? "null" : argsArrayName));
        annotationInfoNames.push_back(infoName);
    }

    // Generate array of annotation infos for this target
    std::string infosArrayName = std::format("@__annotation_{}_infos", groupId);
    std::stringstream infosInit;
    infosInit << "[ ";
    for (size_t i = 0; i < annotationInfoNames.size(); ++i) {
        if (i > 0) infosInit << ", ";
        infosInit << "ptr " << annotationInfoNames[i];
    }
    infosInit << " ]";
    emitLine(std::format("{} = private unnamed_addr constant [{} x ptr] {}",
                         infosArrayName, annotationInfoNames.size(), infosInit.str()));

    // Generate type name string
    std::string typeNameStr = std::format("@__annotation_{}_typename", groupId);
    emitLine(std::format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                         typeNameStr, group.typeName.length() + 1, escapeString(group.typeName)));

    // Generate member name string if needed
    if (!group.memberName.empty()) {
        std::string memberNameStr = std::format("@__annotation_{}_membername", groupId);
        emitLine(std::format("{} = private unnamed_addr constant [{} x i8] c\"{}\\00\"",
                             memberNameStr, group.memberName.length() + 1, escapeString(group.memberName)));
    }

    // Comment for registration
    emitLine(std::format("; Registration for {} {}::{}",
                         group.targetType, group.typeName, group.memberName));
}

void AnnotationCodegen::generate() {
    const auto& annotations = ctx_.annotationMetadata();
    if (annotations.empty()) {
        return;
    }

    emitLine("");
    emitLine("; ============================================");
    emitLine("; Annotation Metadata");
    emitLine("; ============================================");
    emitLine("");

    auto groupedAnnotations = groupAnnotationsByTarget();

    for (size_t i = 0; i < groupedAnnotations.size(); ++i) {
        emitAnnotationGroup(static_cast<int>(i), groupedAnnotations[i]);
    }

    emitLine("");
}

std::string AnnotationCodegen::getIR() const {
    return output_.str();
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
