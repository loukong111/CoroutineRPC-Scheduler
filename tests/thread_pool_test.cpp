#include "corpcron/common/thread_pool.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <thread>

int main() {
    std::atomic<int> completed{0};
    corpcron::DynamicThreadPool pool(1, 2, 0, 1);

    assert(pool.enqueue([]() { throw std::runtime_error("expected test exception"); }));
    for (int i = 0; i < 8; ++i) {
        assert(pool.enqueue([&completed]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            completed.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    const auto completion_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (completed.load(std::memory_order_relaxed) != 8 &&
           std::chrono::steady_clock::now() < completion_deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(completed.load(std::memory_order_relaxed) == 8);
    std::this_thread::sleep_for(std::chrono::milliseconds(1100));
    assert(pool.enqueue([&completed]() { completed.fetch_add(1, std::memory_order_relaxed); }));
    assert(pool.enqueue([&completed]() { completed.fetch_add(1, std::memory_order_relaxed); }));

    pool.stop();
    assert(completed.load(std::memory_order_relaxed) == 10);
    assert(pool.taskCount() == 0);
    assert(!pool.enqueue([]() {}));
    pool.stop();

    std::atomic<bool> blocker_started{false};
    std::atomic<bool> release_blocker{false};
    std::atomic<int> bounded_completed{0};
    corpcron::DynamicThreadPool bounded_pool(1, 1, 1, 5, 1);
    assert(bounded_pool.enqueue([&]() {
        blocker_started.store(true, std::memory_order_release);
        while (!release_blocker.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        bounded_completed.fetch_add(1, std::memory_order_relaxed);
    }));
    while (!blocker_started.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    assert(bounded_pool.enqueue([&]() {
        bounded_completed.fetch_add(1, std::memory_order_relaxed);
    }));
    assert(!bounded_pool.enqueue([]() {}));
    release_blocker.store(true, std::memory_order_release);
    bounded_pool.stop();
    assert(bounded_completed.load(std::memory_order_relaxed) == 2);
    return 0;
}
