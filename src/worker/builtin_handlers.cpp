#include "corpcron/worker/builtin_handlers.hpp"
#include "corpcron/rpc/handler_registry.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <stdexcept>
#include <string>
#include <thread>

namespace corpcron {

namespace {

bool deadlineExceeded(const TaskExecutionContext& context) {
    if (context.deadline_unix_ms == 0) return false;
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return now_ms >= static_cast<int64_t>(context.deadline_unix_ms);
}

int parseSleepDuration(const std::string& params) {
    int duration_ms = 0;
    const auto* begin = params.data();
    const auto* end = begin + params.size();
    const auto [ptr, error] = std::from_chars(begin, end, duration_ms);
    if (error != std::errc{} || ptr != end || duration_ms < 0 || duration_ms > 30000) {
        throw std::invalid_argument("Sleep expects milliseconds in range 0..30000");
    }
    return duration_ms;
}

} // namespace

void registerBuiltinTaskHandlers() {
    auto& registry = HandlerRegistry::instance();
    registry.registerHandler("Echo", [](const std::string& params) {
        return "Echo: " + params;
    });
    registry.registerHandler("Fail", [](const std::string& params) -> std::string {
        throw std::runtime_error(params.empty() ? "Task failed intentionally" : params);
    });
    registry.registerHandler("Uppercase", [](std::string params) {
        std::transform(params.begin(), params.end(), params.begin(), [](unsigned char ch) {
            return static_cast<char>(std::toupper(ch));
        });
        return params;
    });
    registry.registerContextHandler(
        "Sleep", [](const TaskExecutionContext& context, const std::string& params) {
            int remaining_ms = parseSleepDuration(params);
            while (remaining_ms > 0) {
                if (deadlineExceeded(context)) {
                    throw std::runtime_error("Task deadline exceeded");
                }
                const int slice_ms = std::min(remaining_ms, 25);
                std::this_thread::sleep_for(std::chrono::milliseconds(slice_ms));
                remaining_ms -= slice_ms;
            }
            if (deadlineExceeded(context)) {
                throw std::runtime_error("Task deadline exceeded");
            }
            return "Slept " + params + " ms";
        });
}

} // namespace corpcron
