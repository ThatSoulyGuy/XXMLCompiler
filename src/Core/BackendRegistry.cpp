#include "Core/BackendRegistry.h"
#include <algorithm>

namespace XXML::Core {

BackendRegistry::BackendRegistry() {
    // Constructor
}

void BackendRegistry::registerBackend(std::string_view name, std::unique_ptr<IBackend> backend) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string nameStr{name};
    IBackend* ptr = backend.get();

    backendsByName_[nameStr] = std::move(backend);

    // Also register by target type if available
    BackendTarget target = ptr->targetType();
    backendsByTarget_[target] = ptr;

    // Set as default if it's the first backend
    if (defaultBackend_ == nullptr) {
        defaultBackend_ = ptr;
    }
}

void BackendRegistry::registerBackend(BackendTarget target, std::unique_ptr<IBackend> backend) {
    std::string name = targetToName(target);
    registerBackend(name, std::move(backend));
}

bool BackendRegistry::isRegistered(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return backendsByName_.contains(std::string(name));
}

bool BackendRegistry::isRegistered(BackendTarget target) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return backendsByTarget_.contains(target);
}

IBackend* BackendRegistry::getBackend(std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = backendsByName_.find(std::string(name));
    if (it != backendsByName_.end()) {
        return it->second.get();
    }
    return nullptr;
}

const IBackend* BackendRegistry::getBackend(std::string_view name) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = backendsByName_.find(std::string(name));
    if (it != backendsByName_.end()) {
        return it->second.get();
    }
    return nullptr;
}

IBackend* BackendRegistry::getBackend(BackendTarget target) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = backendsByTarget_.find(target);
    if (it != backendsByTarget_.end()) {
        return it->second;
    }
    return nullptr;
}

const IBackend* BackendRegistry::getBackend(BackendTarget target) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = backendsByTarget_.find(target);
    if (it != backendsByTarget_.end()) {
        return it->second;
    }
    return nullptr;
}

void BackendRegistry::setDefaultBackend(std::string_view name) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = backendsByName_.find(std::string(name));
    if (it != backendsByName_.end()) {
        defaultBackend_ = it->second.get();
    }
}

void BackendRegistry::setDefaultBackend(BackendTarget target) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = backendsByTarget_.find(target);
    if (it != backendsByTarget_.end()) {
        defaultBackend_ = it->second;
    }
}

IBackend* BackendRegistry::getDefaultBackend() {
    std::lock_guard<std::mutex> lock(mutex_);
    return defaultBackend_;
}

const IBackend* BackendRegistry::getDefaultBackend() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return defaultBackend_;
}

std::vector<std::string> BackendRegistry::getAllBackendNames() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(backendsByName_.size());

    for (const auto& [name, _] : backendsByName_) {
        result.push_back(name);
    }

    std::ranges::sort(result);
    return result;
}

std::vector<BackendTarget> BackendRegistry::getAllBackendTargets() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<BackendTarget> result;
    result.reserve(backendsByTarget_.size());

    for (const auto& [target, _] : backendsByTarget_) {
        result.push_back(target);
    }

    return result;
}

std::vector<std::string>
BackendRegistry::getBackendsWithCapability(BackendCapability capability) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;

    for (const auto& [name, backend] : backendsByName_) {
        if (backend->supportsCapability(capability)) {
            result.push_back(name);
        }
    }

    return result;
}

std::vector<std::string>
BackendRegistry::getBackendsWithFeature(std::string_view feature) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;

    for (const auto& [name, backend] : backendsByName_) {
        if (backend->supportsFeature(feature)) {
            result.push_back(name);
        }
    }

    return result;
}

void BackendRegistry::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    backendsByName_.clear();
    backendsByTarget_.clear();
    defaultBackend_ = nullptr;
}

size_t BackendRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return backendsByName_.size();
}

void BackendRegistry::registerBuiltinBackends() {
    // Note: Backends are registered from CompilationContext::initializeBuiltins()
    // to avoid circular dependencies
}

std::string BackendRegistry::targetToName(BackendTarget target) const {
    return backendTargetToString(target);
}

} // namespace XXML::Core
