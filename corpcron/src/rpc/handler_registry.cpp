#include "corpcron/rpc/handler_registry.hpp"
#include <iostream>

namespace corpcron {

HandlerRegistry& HandlerRegistry::instance() {
    static HandlerRegistry registry;
    return registry;
}

void HandlerRegistry::registerHandler(const std::string& name, TaskHandler handler) {
    handlers_[name] = std::move(handler);
}

bool HandlerRegistry::hasHandler(const std::string& name) const {
    return handlers_.find(name) != handlers_.end();
}

std::string HandlerRegistry::execute(const std::string& name, const std::string& params) {
    auto it = handlers_.find(name);
    if (it != handlers_.end()) {
        return it->second(params);
    }
    return "Error: handler not found: " + name;
}

} // namespace corpcron