#include "corpcron/rpc/handler_registry.hpp"
#include <algorithm>
#include <mutex>
#include <stdexcept>

namespace corpcron {

HandlerRegistry& HandlerRegistry::instance() {
    static HandlerRegistry registry;
    return registry;
}

void HandlerRegistry::registerHandler(const std::string& name, TaskHandler handler) {
    if (name.empty() || !handler) {
        throw std::invalid_argument("Handler name and callback are required");
    }
    registerContextHandler(
        name, [handler = std::move(handler)](const TaskExecutionContext&,
                                             const std::string& params) {
            return handler(params);
        });
}

void HandlerRegistry::registerContextHandler(const std::string& name,
                                             ContextTaskHandler handler) {
    if (name.empty() || !handler) {
        throw std::invalid_argument("Handler name and callback are required");
    }
    std::unique_lock<std::shared_mutex> lock(mutex_);
    handlers_[name] = std::move(handler);
}

bool HandlerRegistry::hasHandler(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return handlers_.find(name) != handlers_.end();
}

std::vector<std::string> HandlerRegistry::handlerNames() const {
    std::vector<std::string> names;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        names.reserve(handlers_.size());
        for (const auto& [name, _] : handlers_) names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

std::string HandlerRegistry::execute(const std::string& name, const std::string& params) {
    return execute(name, TaskExecutionContext{}, params);
}

std::string HandlerRegistry::execute(const std::string& name,
                                     const TaskExecutionContext& context,
                                     const std::string& params) {
    ContextTaskHandler handler;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = handlers_.find(name);
        if (it == handlers_.end()) {
            throw std::runtime_error("Handler not found: " + name);
        }
        handler = it->second;
    }
    return handler(context, params);
}

} // namespace corpcron
