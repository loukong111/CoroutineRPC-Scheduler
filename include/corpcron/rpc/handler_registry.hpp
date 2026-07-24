#pragma once
#include <cstdint>
#include <functional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace corpcron {

struct TaskExecutionContext {
    std::string task_id;
    std::string execution_id;
    uint64_t deadline_unix_ms = 0;
};

using TaskHandler = std::function<std::string(const std::string& params)>;
using ContextTaskHandler =
    std::function<std::string(const TaskExecutionContext& context,
                              const std::string& params)>;

class HandlerRegistry {
public:
    static HandlerRegistry& instance();

    void registerHandler(const std::string& name, TaskHandler handler);
    void registerContextHandler(const std::string& name, ContextTaskHandler handler);
    bool hasHandler(const std::string& name) const;
    std::vector<std::string> handlerNames() const;
    std::string execute(const std::string& name, const std::string& params);
    std::string execute(const std::string& name, const TaskExecutionContext& context,
                        const std::string& params);

private:
    HandlerRegistry() = default;
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, ContextTaskHandler> handlers_;
};

} // namespace corpcron
