// A minimal C++20 coroutine task for porting blocking DOS code as is: the
// original TAC waits inside its functions (wait_retrace, get_key, text_window),
// so the port's functions are coroutines that co_await those waits, and the
// scene resumes them a retrace (tick) at a time. Tasks start lazily, run when
// awaited, and hand control back to their awaiter when they finish (symmetric
// transfer, so deep chains don't grow the stack).
#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <utility>

namespace redux {

namespace detail {

struct TaskPromiseBase {
    std::coroutine_handle<> continuation;
    std::exception_ptr error;

    std::suspend_always initial_suspend() noexcept { return {}; }
    struct Final {
        bool await_ready() noexcept { return false; }
        template <class P>
        std::coroutine_handle<> await_suspend(std::coroutine_handle<P> h) noexcept {
            auto c = h.promise().continuation;
            return c ? c : std::noop_coroutine();
        }
        void await_resume() noexcept {}
    };
    Final final_suspend() noexcept { return {}; }
    void unhandled_exception() { error = std::current_exception(); }
};

}  // namespace detail

template <class T = void>
class [[nodiscard]] Task {
public:
    struct promise_type : detail::TaskPromiseBase {
        std::optional<T> value;
        Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
        void return_value(T v) { value = std::move(v); }
    };

    Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    Task& operator=(Task&& o) noexcept {
        if (this != &o) {
            if (h_) h_.destroy();
            h_ = std::exchange(o.h_, {});
        }
        return *this;
    }
    ~Task() {
        if (h_) h_.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept {
        h_.promise().continuation = c;
        return h_;
    }
    T await_resume() {
        if (h_.promise().error) std::rethrow_exception(h_.promise().error);
        return std::move(*h_.promise().value);
    }

private:
    explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
    std::coroutine_handle<promise_type> h_;
};

template <>
class [[nodiscard]] Task<void> {
public:
    struct promise_type : detail::TaskPromiseBase {
        Task get_return_object() { return Task(std::coroutine_handle<promise_type>::from_promise(*this)); }
        void return_void() {}
    };

    Task() = default;
    Task(Task&& o) noexcept : h_(std::exchange(o.h_, {})) {}
    Task& operator=(Task&& o) noexcept {
        if (this != &o) {
            if (h_) h_.destroy();
            h_ = std::exchange(o.h_, {});
        }
        return *this;
    }
    ~Task() {
        if (h_) h_.destroy();
    }

    bool await_ready() const noexcept { return false; }
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> c) noexcept {
        h_.promise().continuation = c;
        return h_;
    }
    void await_resume() {
        if (h_.promise().error) std::rethrow_exception(h_.promise().error);
    }

    // For a top-level task: run it until its first suspension.
    void start() { h_.resume(); }
    bool done() const { return !h_ || h_.done(); }
    // A top-level task's exception, if it ended with one.
    void rethrow() const {
        if (h_ && h_.done() && h_.promise().error) std::rethrow_exception(h_.promise().error);
    }

private:
    explicit Task(std::coroutine_handle<promise_type> h) : h_(h) {}
    std::coroutine_handle<promise_type> h_;
};

}  // namespace redux
