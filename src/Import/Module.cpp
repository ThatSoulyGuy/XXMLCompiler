#include "../../include/Import/Module.h"
#include <fstream>
#include <sstream>

namespace XXML {
namespace Import {

Module::Module(const std::string& name, const std::string& path)
    : moduleName(name), filePath(path), isParsed(false),
      isAnalyzed(false), isCompiled(false), isSTLFile(false) {
    // Create symbol table for this module with its name
    exportedSymbols = std::make_unique<Semantic::SymbolTable>(moduleName);
    // Register this module's symbol table in the global registry
    exportedSymbols->registerModule();
}

bool Module::loadFromFile() {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    fileContent = buffer.str();
    file.close();

    return !fileContent.empty();
}

std::string Module::toString() const {
    std::stringstream ss;
    ss << "Module{" << moduleName << " @ " << filePath;
    if (!imports.empty()) {
        ss << ", imports: [";
        for (size_t i = 0; i < imports.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << imports[i];
        }
        ss << "]";
    }
    ss << "}";
    return ss.str();
}

} // namespace Import
} // namespace XXML
