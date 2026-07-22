#pragma once

#include <atomic>
#include <functional>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace corpcron {

class CancellationToken {
public:
    using Predicate = std::function<bool()>;

    CancellationToken() = default;
    explicit CancellationToken(Predicate predicate) : predicate_(std::move(predicate)) {}

    bool isCancellationRequested() const {
        return predicate_ && predicate_();
    }

private:
    Predicate predicate_;
};

class CancellationSource {
public:
    CancellationSource() : canceled_(std::make_shared<std::atomic<bool>>(false)) {}

    CancellationToken token() const {
        auto state = canceled_;
        return CancellationToken([state]() { return state && state->load(std::memory_order_relaxed); });
    }

    void cancel() const {
        if (canceled_) canceled_->store(true, std::memory_order_relaxed);
    }

    void reset() {
        canceled_ = std::make_shared<std::atomic<bool>>(false);
    }

    bool isCancellationRequested() const {
        return canceled_ && canceled_->load(std::memory_order_relaxed);
    }

private:
    std::shared_ptr<std::atomic<bool>> canceled_;
};

inline CancellationToken linkCancellationTokens(std::initializer_list<CancellationToken> tokens) {
    std::vector<CancellationToken> linked(tokens);
    return CancellationToken([linked = std::move(linked)]() {
        for (const auto& token : linked) {
            if (token.isCancellationRequested()) return true;
        }
        return false;
    });
}

} // namespace corpcron
