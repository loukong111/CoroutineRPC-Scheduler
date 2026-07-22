#include "corpcron/scheduler/task_cancellation_registry.hpp"

namespace corpcron {

TaskCancellationRegistry& TaskCancellationRegistry::instance() {
    static TaskCancellationRegistry registry;
    return registry;
}

void TaskCancellationRegistry::registerTask(const std::string& task_id,
                                            const std::string& execution_id,
                                            CancellationSource source) {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.insert_or_assign(task_id, Entry{execution_id, std::move(source)});
}

void TaskCancellationRegistry::unregisterTask(const std::string& task_id,
                                              const std::string& execution_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sources_.find(task_id);
    if (it != sources_.end() && it->second.execution_id == execution_id) {
        sources_.erase(it);
    }
}

bool TaskCancellationRegistry::cancel(const std::string& task_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sources_.find(task_id);
    if (it == sources_.end()) return false;
    it->second.source.cancel();
    return true;
}

} // namespace corpcron
