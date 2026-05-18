#pragma once
#include <functional>
#include <string>
#include <unordered_map>

namespace corpcron {

using TaskHandler = std::function<std::string(const std::string& params)>;

class HandlerRegistry {
public:
    static HandlerRegistry& instance();

    void registerHandler(const std::string& name, TaskHandler handler);
    bool hasHandler(const std::string& name) const;
    std::string execute(const std::string& name, const std::string& params);

private:
    HandlerRegistry() = default;
    std::unordered_map<std::string, TaskHandler> handlers_;
};

} // namespace corpcron