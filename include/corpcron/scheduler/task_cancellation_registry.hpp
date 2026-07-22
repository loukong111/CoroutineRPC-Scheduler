#pragma once

#include "corpcron/common/cancellation.hpp"
#include <mutex>
#include <string>
#include <unordered_map>

namespace corpcron {

class TaskCancellationRegistry {
public:
    static TaskCancellationRegistry& instance();

    void registerTask(const std::string& task_id, const std::string& execution_id,
                      CancellationSource source);
    void unregisterTask(const std::string& task_id, const std::string& execution_id);
    bool cancel(const std::string& task_id);

private:
    TaskCancellationRegistry() = default;

    std::mutex mutex_;
    struct Entry {
        std::string execution_id;
        CancellationSource source;
    };

    std::unordered_map<std::string, Entry> sources_;
};

} // namespace corpcron
