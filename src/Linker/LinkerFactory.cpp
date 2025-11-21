#include "Linker/LinkerInterface.h"

namespace XXML {
namespace Linker {

// Forward declarations of factory functions from implementation files
extern std::unique_ptr<ILinker> createMSVCLinker();
extern std::unique_ptr<ILinker> createGNULinker();

std::unique_ptr<ILinker> LinkerFactory::createLinker(bool preferLLD) {
    // Try platform-specific linker first
#if defined(__MINGW32__) || defined(__MINGW64__)
    // Built with MinGW: always prefer GNU linker to avoid MSVC/link.exe issues
    // MSVC link.exe often conflicts with /usr/bin/link in MSYS2/Git Bash
    auto linker = createGNULinker();
    if (linker && linker->isAvailable()) {
        return linker;
    }
    // Fallback to MSVC if GNU not available
    linker = createMSVCLinker();
    if (linker && linker->isAvailable()) {
        return linker;
    }
#elif defined(_WIN32)
    // Native MSVC build: prefer MSVC linker
    auto linker = createMSVCLinker();
    if (linker && linker->isAvailable()) {
        return linker;
    }

    // Fallback to GNU linker (MinGW)
    linker = createGNULinker();
    if (linker && linker->isAvailable()) {
        return linker;
    }
#else
    // On Unix/Linux/macOS, use GNU linker (gcc/clang)
    auto linker = createGNULinker();
    if (linker && linker->isAvailable()) {
        return linker;
    }
#endif

    // No linker available
    return nullptr;
}

std::unique_ptr<ILinker> LinkerFactory::createLinkerByName(const std::string& name) {
    if (name == "msvc") {
        auto linker = createMSVCLinker();
        if (linker && linker->isAvailable()) {
            return linker;
        }
    } else if (name == "gnu" || name == "gcc" || name == "clang") {
        auto linker = createGNULinker();
        if (linker && linker->isAvailable()) {
            return linker;
        }
    }

    return nullptr;
}

} // namespace Linker
} // namespace XXML
