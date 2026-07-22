#include "corpcron/net/epoll_loop.hpp"
#include <cassert>
#include <chrono>

int main() {
    corpcron::EpollLoop loop;
    assert(loop.init());
    loop.stop();

    const auto started = std::chrono::steady_clock::now();
    assert(loop.run());
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started);

    assert(loop.isStopping());
    assert(elapsed < std::chrono::milliseconds(100));
    assert(!loop.addCoroutine(-1, 0, []() {}));
    return 0;
}
