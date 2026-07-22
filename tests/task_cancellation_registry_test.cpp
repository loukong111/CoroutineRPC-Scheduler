#include "corpcron/scheduler/task_cancellation_registry.hpp"
#include <cassert>

int main() {
    auto& registry = corpcron::TaskCancellationRegistry::instance();

    corpcron::CancellationSource old_execution;
    corpcron::CancellationSource new_execution;
    registry.registerTask("same-task", "execution-old", old_execution);
    registry.registerTask("same-task", "execution-new", new_execution);

    registry.unregisterTask("same-task", "execution-old");
    assert(registry.cancel("same-task"));
    assert(!old_execution.isCancellationRequested());
    assert(new_execution.isCancellationRequested());

    registry.unregisterTask("same-task", "execution-new");
    assert(!registry.cancel("same-task"));
    return 0;
}
