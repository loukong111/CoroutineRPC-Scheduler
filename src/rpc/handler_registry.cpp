#include "corpcron/rpc/handler_registry.hpp"
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
    std::unique_lock<std::shared_mutex> lock(mutex_);
    handlers_[name] = std::move(handler);
}

bool HandlerRegistry::hasHandler(const std::string& name) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return handlers_.find(name) != handlers_.end();
}

std::string HandlerRegistry::execute(const std::string& name, const std::string& params) {
    TaskHandler handler;
    {
        std::shared_lock<std::shared_mutex> lock(mutex_);
        auto it = handlers_.find(name);
        if (it == handlers_.end()) {
            throw std::runtime_error("Handler not found: " + name);
        }
        handler = it->second;
    }
    return handler(params);
}

} // namespace corpcron
