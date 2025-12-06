#include "Backends/Codegen/DeclCodegen/DeclCodegen.h"
#include "Backends/TypeNormalizer.h"
#include "Parser/AST.h"
#include <iostream>

namespace XXML {
namespace Backends {
namespace Codegen {

namespace {

// Helper to convert OwnershipType to string character
std::string ownershipToString(Parser::OwnershipType ownership) {
    switch (ownership) {
        case Parser::OwnershipType::Owned: return "^";
        case Parser::OwnershipType::Reference: return "&";
        case Parser::OwnershipType::Copy: return "%";
        case Parser::OwnershipType::None: return "";
        default: return "";
    }
}

// Sanitize name for LLVM IR (replace :: with .)
std::string sanitizeIRName(const std::string& name) {
    std::string result = name;
    size_t pos = 0;
    while ((pos = result.find("::", pos)) != std::string::npos) {
        result.replace(pos, 2, ".");
        pos += 1;
    }
    return result;
}

} // anonymous namespace

void DeclCodegen::generate(Parser::ASTNode* decl) {
    if (!decl) return;

    // Dispatch based on declaration type
    if (auto* classDecl = dynamic_cast<Parser::ClassDecl*>(decl)) {
        visitClass(classDecl);
    } else if (auto* nativeDecl = dynamic_cast<Parser::NativeStructureDecl*>(decl)) {
        visitNativeStruct(nativeDecl);
    } else if (auto* ctorDecl = dynamic_cast<Parser::ConstructorDecl*>(decl)) {
        visitConstructor(ctorDecl);
    } else if (auto* dtorDecl = dynamic_cast<Parser::DestructorDecl*>(decl)) {
        visitDestructor(dtorDecl);
    } else if (auto* methodDecl = dynamic_cast<Parser::MethodDecl*>(decl)) {
        visitMethod(methodDecl);
    } else if (auto* propDecl = dynamic_cast<Parser::PropertyDecl*>(decl)) {
        visitProperty(propDecl);
    } else if (auto* enumDecl = dynamic_cast<Parser::EnumerationDecl*>(decl)) {
        visitEnumeration(enumDecl);
    } else if (auto* nsDecl = dynamic_cast<Parser::NamespaceDecl*>(decl)) {
        visitNamespace(nsDecl);
    } else if (auto* entryDecl = dynamic_cast<Parser::EntrypointDecl*>(decl)) {
        visitEntrypoint(entryDecl);
    } else if (auto* annotDecl = dynamic_cast<Parser::AnnotationDecl*>(decl)) {
        visitAnnotationDecl(annotDecl);
    }
}

void DeclCodegen::generateFunctionBody(const std::vector<std::unique_ptr<Parser::Statement>>& body) {
    for (const auto& stmt : body) {
        stmtCodegen_.generate(stmt.get());
    }
}

// === Class Declaration ===

void DeclCodegen::visitClass(Parser::ClassDecl* decl) {
    if (!decl) return;

    // Skip template class declarations - only generate instantiated versions
    if (!decl->templateParams.empty()) {
        std::cerr << "[DEBUG DeclCodegen] Skipping template class (has templateParams): " << decl->name << "\n";
        return;
    }
    std::cerr << "[DEBUG DeclCodegen] Processing class: " << decl->name << " (namespace: " << ctx_.currentNamespace() << ")\n";

    // Collect retained annotations for this class
    collectRetainedAnnotations(decl);

    // Build full class name
    std::string fullClassName;
    if (!ctx_.currentNamespace().empty()) {
        fullClassName = std::string(ctx_.currentNamespace()) + "::" + decl->name;
    } else {
        fullClassName = decl->name;
    }

    // Skip if already generated
    if (ctx_.isClassGenerated(fullClassName)) {
        return;
    }

    // Save and set current class context
    std::string previousClass = std::string(ctx_.currentClassName());
    ctx_.setCurrentClassName(fullClassName);

    // Create struct type if not already registered
    if (!ctx_.hasClass(fullClassName)) {
        // Create struct type in LLVMIR module - use sanitized name for LLVM IR
        std::string irStructName = sanitizeIRName(fullClassName);
        auto* structType = ctx_.module().createStruct(irStructName);

        // Collect property types for struct body
        std::vector<LLVMIR::Type*> fieldTypes;
        std::vector<PropertyInfo> properties;
        size_t propIndex = 0;

        for (const auto& section : decl->sections) {
            for (const auto& memberDecl : section->declarations) {
                if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(memberDecl.get())) {
                    std::string propTypeName = ctx_.resolveToQualifiedName(prop->type->typeName);
                    // NativeType is never stored as pointer - use actual LLVM type
                    bool isNativeType = propTypeName.find("NativeType<") == 0 ||
                                        prop->type->typeName.find("NativeType<") == 0;
                    // Object types (owned/reference/copy) are stored as pointers, except NativeType
                    bool isObjectType = !isNativeType &&
                                        (prop->type->ownership == Parser::OwnershipType::Owned ||
                                         prop->type->ownership == Parser::OwnershipType::Reference ||
                                         prop->type->ownership == Parser::OwnershipType::Copy);
                    auto* propType = isObjectType ? ctx_.module().getContext().getPtrTy() : ctx_.mapType(propTypeName);
                    fieldTypes.push_back(propType);

                    PropertyInfo propInfo;
                    propInfo.name = prop->name;
                    propInfo.xxmlType = propTypeName;
                    propInfo.isObjectType = isObjectType;
                    propInfo.index = propIndex++;
                    properties.push_back(propInfo);
                    std::cerr << "[DEBUG DeclCodegen]   Property '" << prop->name << "' type: " << propTypeName
                              << " (orig: " << prop->type->typeName << "), isNativeType=" << isNativeType
                              << ", isObjectType=" << isObjectType << "\n";
                }
            }
        }

        // Set struct body (empty structs get a single i8 field)
        if (fieldTypes.empty()) {
            fieldTypes.push_back(ctx_.module().getContext().getInt8Ty());
        }
        structType->setBody(fieldTypes);

        // Register class info in context
        ClassInfo classInfo;
        classInfo.name = fullClassName;
        classInfo.mangledName = fullClassName;
        classInfo.structType = structType;
        classInfo.properties = properties;
        classInfo.instanceSize = structType->getSizeInBits() / 8;
        ctx_.registerClass(fullClassName, classInfo);
    }

    ctx_.markClassGenerated(fullClassName);

    // Collect reflection metadata
    ReflectionClassMetadata metadata;
    metadata.name = decl->name;
    metadata.namespaceName = std::string(ctx_.currentNamespace());
    metadata.fullName = fullClassName;
    metadata.isTemplate = false;
    metadata.astNode = decl;

    auto* classInfo = ctx_.getClass(fullClassName);
    if (classInfo) {
        metadata.instanceSize = classInfo->instanceSize;
    }

    // Collect properties for metadata
    for (const auto& section : decl->sections) {
        for (const auto& memberDecl : section->declarations) {
            if (auto* prop = dynamic_cast<Parser::PropertyDecl*>(memberDecl.get())) {
                metadata.properties.push_back({prop->name, prop->type->typeName});
                metadata.propertyOwnerships.push_back(ownershipToString(prop->type->ownership));
                collectRetainedAnnotations(prop, fullClassName);
            }
        }
    }

    // Collect methods for metadata and generate code
    for (const auto& section : decl->sections) {
        for (const auto& memberDecl : section->declarations) {
            if (auto* method = dynamic_cast<Parser::MethodDecl*>(memberDecl.get())) {
                metadata.methods.push_back({method->name, method->returnType ? method->returnType->typeName : "None"});
                metadata.methodReturnOwnerships.push_back(method->returnType ? ownershipToString(method->returnType->ownership) : "");

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : method->parameters) {
                    params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                }
                metadata.methodParameters.push_back(params);
                collectRetainedAnnotations(method, fullClassName);

                // Generate method code
                visitMethod(method);
            } else if (auto* ctor = dynamic_cast<Parser::ConstructorDecl*>(memberDecl.get())) {
                metadata.methods.push_back({"Constructor", decl->name});
                metadata.methodReturnOwnerships.push_back("^");

                std::vector<std::tuple<std::string, std::string, std::string>> params;
                for (const auto& param : ctor->parameters) {
                    params.push_back(std::make_tuple(param->name, param->type->typeName, ownershipToString(param->type->ownership)));
                }
                metadata.methodParameters.push_back(params);

                // Generate constructor code
                visitConstructor(ctor);
            } else if (auto* dtor = dynamic_cast<Parser::DestructorDecl*>(memberDecl.get())) {
                // Generate destructor code
                visitDestructor(dtor);
            }
        }
    }

    // Store metadata
    ctx_.addReflectionMetadata(fullClassName, metadata);

    // Restore class context
    ctx_.setCurrentClassName(previousClass);
}

void DeclCodegen::visitNativeStruct(Parser::NativeStructureDecl*) {
    // Native structs are handled by preamble generation
}

// === Constructor Declaration ===

void DeclCodegen::visitConstructor(Parser::ConstructorDecl* decl) {
    if (!decl) return;

    std::string className = std::string(ctx_.currentClassName());
    std::cerr << "[DEBUG DeclCodegen] visitConstructor (class: " << className << ", isDefault: " << decl->isDefault << ")\n";
    if (className.empty()) return;

    // Build function name: ClassName_Constructor_N (N = param count)
    // Use TypeNormalizer to mangle class name for valid LLVM identifiers
    std::string mangledClassName = TypeNormalizer::mangleForLLVM(className);
    std::string funcName = mangledClassName + "_Constructor_" + std::to_string(decl->parameters.size());

    // Skip if already defined
    if (ctx_.isFunctionDefined(funcName)) return;

    // Build parameter types: first param is ptr (this), then user params
    std::vector<LLVMIR::Type*> paramTypes;
    paramTypes.push_back(ctx_.module().getContext().getPtrTy());  // 'this' pointer

    for (const auto& param : decl->parameters) {
        std::string paramTypeName = ctx_.resolveToQualifiedName(param->type->typeName);
        bool isObjectParam = param->type->ownership == Parser::OwnershipType::Owned ||
                             param->type->ownership == Parser::OwnershipType::Reference ||
                             param->type->ownership == Parser::OwnershipType::Copy;
        paramTypes.push_back(isObjectParam ? ctx_.module().getContext().getPtrTy() : ctx_.mapType(paramTypeName));
    }

    // Return type is ptr (returns 'this')
    auto* returnType = ctx_.module().getContext().getPtrTy();
    auto* funcType = ctx_.module().getContext().getFunctionTy(returnType, paramTypes);

    // Create function
    auto* func = ctx_.module().createFunction(funcType, funcName);
    if (!func) return;

    ctx_.markFunctionDefined(funcName);
    ctx_.setCurrentFunction(func);
    ctx_.setCurrentReturnType("ptr");

    // Create entry block
    auto* entryBB = func->createBasicBlock("entry");
    ctx_.setInsertPoint(entryBB);

    // Push scope for constructor body
    ctx_.pushScope();

    // Register 'this' parameter
    if (func->getArg(0)) {
        func->getArg(0)->setName("this");
        ctx_.declareParameter("this", className, LLVMIR::AnyValue(func->getArg(0)));
    }

    // Register user parameters
    for (size_t i = 0; i < decl->parameters.size(); ++i) {
        auto& param = decl->parameters[i];
        std::string paramTypeName = ctx_.resolveToQualifiedName(param->type->typeName);
        if (func->getArg(i + 1)) {
            func->getArg(i + 1)->setName(param->name);
            ctx_.declareParameter(param->name, paramTypeName, LLVMIR::AnyValue(func->getArg(i + 1)));
        }
    }

    // Initialize properties to default values (zero/null)
    auto* classInfo = ctx_.getClass(className);
    if (classInfo && func->getArg(0)) {
        auto thisPtr = LLVMIR::PtrValue(func->getArg(0));
        for (size_t i = 0; i < classInfo->properties.size(); ++i) {
            const auto& prop = classInfo->properties[i];
            auto propPtr = ctx_.builder().createStructGEP(classInfo->structType, thisPtr, i, prop.name + ".ptr");

            // Determine default value based on type
            auto* propType = ctx_.mapType(prop.xxmlType);
            if (propType->isPointer()) {
                ctx_.builder().createStore(ctx_.builder().getNullPtr(), propPtr);
            } else if (propType->isFloat()) {
                // FloatType covers both float and double
                auto* floatType = static_cast<LLVMIR::FloatType*>(propType);
                if (floatType->isDouble()) {
                    ctx_.builder().createStore(ctx_.builder().getDouble(0.0), propPtr);
                } else {
                    ctx_.builder().createStore(ctx_.builder().getFloat(0.0f), propPtr);
                }
            } else {
                // Integer types
                ctx_.builder().createStore(ctx_.builder().getInt64(0), propPtr);
            }
        }
    }

    // Generate body statements
    generateFunctionBody(decl->body);

    // Return 'this' if no explicit return
    if (!ctx_.currentBlock() || !ctx_.currentBlock()->getTerminator()) {
        auto* thisArg = func->getArg(0);
        if (thisArg) {
            ctx_.builder().createRet(LLVMIR::AnyValue(thisArg));
        } else {
            ctx_.builder().createRetVoid();
        }
    }

    // Pop scope
    ctx_.popScope();
    ctx_.setCurrentFunction(nullptr);
}

// === Destructor Declaration ===

void DeclCodegen::visitDestructor(Parser::DestructorDecl* decl) {
    if (!decl) return;

    std::string className = std::string(ctx_.currentClassName());
    if (className.empty()) return;

    // Build function name: ClassName_Destructor
    std::string mangledClassName = TypeNormalizer::mangleForLLVM(className);
    std::string funcName = mangledClassName + "_Destructor";

    // Skip if already defined
    if (ctx_.isFunctionDefined(funcName)) return;

    // Destructor takes 'this' pointer, returns void
    std::vector<LLVMIR::Type*> paramTypes;
    paramTypes.push_back(ctx_.module().getContext().getPtrTy());

    auto* voidType = ctx_.module().getContext().getVoidTy();
    auto* funcType = ctx_.module().getContext().getFunctionTy(voidType, paramTypes);

    // Create function
    auto* func = ctx_.module().createFunction(funcType, funcName);
    if (!func) return;

    ctx_.markFunctionDefined(funcName);
    ctx_.setCurrentFunction(func);
    ctx_.setCurrentReturnType("void");

    // Create entry block
    auto* entryBB = func->createBasicBlock("entry");
    ctx_.setInsertPoint(entryBB);

    // Push scope
    ctx_.pushScope();

    // Register 'this' parameter
    if (func->getArg(0)) {
        func->getArg(0)->setName("this");
        ctx_.declareParameter("this", className, LLVMIR::AnyValue(func->getArg(0)));
    }

    // Generate body statements
    generateFunctionBody(decl->body);

    // Add return void if needed
    if (!ctx_.currentBlock() || !ctx_.currentBlock()->getTerminator()) {
        ctx_.builder().createRetVoid();
    }

    // Pop scope
    ctx_.popScope();
    ctx_.setCurrentFunction(nullptr);
}

// === Method Declaration ===

void DeclCodegen::visitMethod(Parser::MethodDecl* decl) {
    if (!decl) return;

    // Skip template method declarations
    if (!decl->templateParams.empty()) return;

    // Skip native methods (handled separately)
    if (decl->isNative) return;

    std::string className = std::string(ctx_.currentClassName());
    std::cerr << "[DEBUG DeclCodegen] visitMethod: " << decl->name << " (class: " << className << ")\n";
    bool isInstanceMethod = !className.empty();

    // Build function name using TypeNormalizer for valid LLVM identifiers
    std::string funcName;
    if (isInstanceMethod) {
        std::string mangledClassName = TypeNormalizer::mangleForLLVM(className);
        funcName = mangledClassName + "_" + decl->name;
    } else {
        funcName = decl->name;
    }

    // Skip if already defined
    if (ctx_.isFunctionDefined(funcName)) return;

    // Build parameter types - owned/reference/copy types are pointers
    std::vector<LLVMIR::Type*> paramTypes;
    if (isInstanceMethod) {
        paramTypes.push_back(ctx_.module().getContext().getPtrTy());  // 'this'
    }

    for (const auto& param : decl->parameters) {
        std::string paramTypeName = ctx_.resolveToQualifiedName(param->type->typeName);
        bool isObjectParam = param->type->ownership == Parser::OwnershipType::Owned ||
                             param->type->ownership == Parser::OwnershipType::Reference ||
                             param->type->ownership == Parser::OwnershipType::Copy;
        paramTypes.push_back(isObjectParam ? ctx_.module().getContext().getPtrTy() : ctx_.mapType(paramTypeName));
    }

    // Build return type - owned/reference/copy types are pointers (except NativeType)
    std::string returnTypeName = decl->returnType ? decl->returnType->typeName : "void";
    std::cerr << "[DEBUG DeclCodegen]   Original return type: '" << returnTypeName << "'\n";
    returnTypeName = ctx_.resolveToQualifiedName(returnTypeName);
    std::cerr << "[DEBUG DeclCodegen]   Resolved return type: '" << returnTypeName << "'\n";

    // NativeType should NEVER be treated as object type - it maps directly to LLVM primitives
    bool isNativeType = returnTypeName.find("NativeType<") != std::string::npos;
    std::cerr << "[DEBUG DeclCodegen]   isNativeType=" << isNativeType
              << ", ownership=" << static_cast<int>(decl->returnType ? decl->returnType->ownership : Parser::OwnershipType::None) << "\n";

    bool isObjectReturnType = decl->returnType && !isNativeType &&
        (decl->returnType->ownership == Parser::OwnershipType::Owned ||
         decl->returnType->ownership == Parser::OwnershipType::Reference ||
         decl->returnType->ownership == Parser::OwnershipType::Copy);
    std::cerr << "[DEBUG DeclCodegen]   isObjectReturnType=" << isObjectReturnType << "\n";
    auto* returnType = isObjectReturnType ? ctx_.module().getContext().getPtrTy() : ctx_.mapType(returnTypeName);

    auto* funcType = ctx_.module().getContext().getFunctionTy(returnType, paramTypes);

    // Create function
    auto* func = ctx_.module().createFunction(funcType, funcName);
    if (!func) return;

    ctx_.markFunctionDefined(funcName);
    ctx_.setCurrentFunction(func);
    ctx_.setCurrentReturnType(returnTypeName);

    // Create entry block
    auto* entryBB = func->createBasicBlock("entry");
    ctx_.setInsertPoint(entryBB);

    // Push scope for method body
    ctx_.pushScope();

    // Register parameters
    size_t argIdx = 0;
    if (isInstanceMethod) {
        if (func->getArg(argIdx)) {
            func->getArg(argIdx)->setName("this");
            ctx_.declareParameter("this", className, LLVMIR::AnyValue(func->getArg(argIdx)));
        }
        argIdx++;
    }

    for (const auto& param : decl->parameters) {
        std::string paramTypeName = ctx_.resolveToQualifiedName(param->type->typeName);
        if (func->getArg(argIdx)) {
            func->getArg(argIdx)->setName(param->name);
            ctx_.declareParameter(param->name, paramTypeName, LLVMIR::AnyValue(func->getArg(argIdx)));
        }
        argIdx++;
    }

    // Generate body statements
    generateFunctionBody(decl->body);

    // Add default return if needed
    if (!ctx_.currentBlock() || !ctx_.currentBlock()->getTerminator()) {
        if (returnTypeName == "void" || returnTypeName.empty()) {
            ctx_.builder().createRetVoid();
        } else {
            // Return null/zero as default
            ctx_.builder().createRetVoid();  // Will need proper default value
        }
    }

    // Pop scope
    ctx_.popScope();
    ctx_.setCurrentFunction(nullptr);
}

void DeclCodegen::visitProperty(Parser::PropertyDecl*) {
    // Properties are handled in visitClass when building struct type
}

void DeclCodegen::visitEnumeration(Parser::EnumerationDecl* decl) {
    if (!decl) return;

    std::string enumName;
    if (!ctx_.currentNamespace().empty()) {
        enumName = std::string(ctx_.currentNamespace()) + "::" + decl->name;
    } else {
        enumName = decl->name;
    }

    // Register enum values
    int64_t nextValue = 0;
    for (const auto& value : decl->values) {
        if (value->hasExplicitValue) {
            nextValue = value->value;
        }
        std::string fullName = enumName + "::" + value->name;
        ctx_.registerEnumValue(fullName, nextValue);
        nextValue++;
    }
}

void DeclCodegen::visitNamespace(Parser::NamespaceDecl* decl) {
    if (!decl) return;

    std::string previousNs = std::string(ctx_.currentNamespace());
    std::string currentNs;
    if (previousNs.empty()) {
        currentNs = decl->name;
    } else {
        currentNs = previousNs + "::" + decl->name;
    }
    ctx_.setCurrentNamespace(currentNs);

    for (const auto& innerDecl : decl->declarations) {
        generate(innerDecl.get());
    }

    ctx_.setCurrentNamespace(previousNs);
}

// === Entrypoint Declaration ===

void DeclCodegen::visitEntrypoint(Parser::EntrypointDecl* decl) {
    if (!decl) return;

    // Skip if already defined
    if (ctx_.isFunctionDefined("main")) return;

    // Create main function: i32 @main()
    std::vector<LLVMIR::Type*> paramTypes;  // No parameters
    auto* returnType = ctx_.module().getContext().getInt32Ty();
    auto* funcType = ctx_.module().getContext().getFunctionTy(returnType, paramTypes);

    auto* func = ctx_.module().createFunction(funcType, "main");
    if (!func) return;

    ctx_.markFunctionDefined("main");
    ctx_.setCurrentFunction(func);
    ctx_.setCurrentReturnType("i32");

    // Create entry block
    auto* entryBB = func->createBasicBlock("entry");
    ctx_.setInsertPoint(entryBB);

    // Push scope for entrypoint body
    ctx_.pushScope();

    // Generate body statements
    generateFunctionBody(decl->body);

    // Add default return 0 if needed
    if (!ctx_.currentBlock() || !ctx_.currentBlock()->getTerminator()) {
        auto zero = ctx_.builder().getInt32(0);
        ctx_.builder().createRet(LLVMIR::AnyValue(zero));
    }

    // Pop scope
    ctx_.popScope();
    ctx_.setCurrentFunction(nullptr);
}

void DeclCodegen::visitAnnotationDecl(Parser::AnnotationDecl* decl) {
    if (!decl) return;

    if (decl->retainAtRuntime) {
        ctx_.markAnnotationRetained(decl->name);
    }
}

// === Annotation Collection Helpers ===

AnnotationArgValue DeclCodegen::evaluateAnnotationArg(Parser::Expression* expr) {
    AnnotationArgValue result;
    result.kind = AnnotationArgValue::Integer;
    result.intValue = 0;

    if (!expr) return result;

    if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Integer;
        result.intValue = intLit->value;
        return result;
    }

    if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::String;
        result.stringValue = strLit->value;
        return result;
    }

    if (auto* boolLit = dynamic_cast<Parser::BoolLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Bool;
        result.boolValue = boolLit->value;
        return result;
    }

    if (auto* floatLit = dynamic_cast<Parser::FloatLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Float;
        result.floatValue = floatLit->value;
        return result;
    }

    if (auto* doubleLit = dynamic_cast<Parser::DoubleLiteralExpr*>(expr)) {
        result.kind = AnnotationArgValue::Double;
        result.doubleValue = doubleLit->value;
        return result;
    }

    if (auto* callExpr = dynamic_cast<Parser::CallExpr*>(expr)) {
        if (!callExpr->arguments.empty()) {
            if (auto* strLit = dynamic_cast<Parser::StringLiteralExpr*>(callExpr->arguments[0].get())) {
                result.kind = AnnotationArgValue::String;
                result.stringValue = strLit->value;
                return result;
            }
            if (auto* intLit = dynamic_cast<Parser::IntegerLiteralExpr*>(callExpr->arguments[0].get())) {
                result.kind = AnnotationArgValue::Integer;
                result.intValue = intLit->value;
                return result;
            }
        }
    }

    return result;
}

void DeclCodegen::collectRetainedAnnotations(Parser::ClassDecl* decl) {
    if (!decl) return;

    std::string fullClassName = ctx_.currentNamespace().empty()
        ? decl->name
        : std::string(ctx_.currentNamespace()) + "::" + decl->name;

    for (const auto& annotation : decl->annotations) {
        if (ctx_.isAnnotationRetained(annotation->annotationName)) {
            PendingAnnotationMetadata metadata;
            metadata.annotationName = annotation->annotationName;
            metadata.targetType = "type";
            metadata.typeName = fullClassName;
            metadata.memberName = "";

            for (const auto& arg : annotation->arguments) {
                metadata.arguments.push_back({arg.first, evaluateAnnotationArg(arg.second.get())});
            }

            ctx_.addAnnotationMetadata(metadata);
        }
    }
}

void DeclCodegen::collectRetainedAnnotations(Parser::MethodDecl* decl, const std::string& className) {
    if (!decl) return;

    for (const auto& annotation : decl->annotations) {
        if (ctx_.isAnnotationRetained(annotation->annotationName)) {
            PendingAnnotationMetadata metadata;
            metadata.annotationName = annotation->annotationName;
            metadata.targetType = "method";
            metadata.typeName = className;
            metadata.memberName = decl->name;

            for (const auto& arg : annotation->arguments) {
                metadata.arguments.push_back({arg.first, evaluateAnnotationArg(arg.second.get())});
            }

            ctx_.addAnnotationMetadata(metadata);
        }
    }
}

void DeclCodegen::collectRetainedAnnotations(Parser::PropertyDecl* decl, const std::string& className) {
    if (!decl) return;

    for (const auto& annotation : decl->annotations) {
        if (ctx_.isAnnotationRetained(annotation->annotationName)) {
            PendingAnnotationMetadata metadata;
            metadata.annotationName = annotation->annotationName;
            metadata.targetType = "property";
            metadata.typeName = className;
            metadata.memberName = decl->name;

            for (const auto& arg : annotation->arguments) {
                metadata.arguments.push_back({arg.first, evaluateAnnotationArg(arg.second.get())});
            }

            ctx_.addAnnotationMetadata(metadata);
        }
    }
}

} // namespace Codegen
} // namespace Backends
} // namespace XXML
