#include "app/GameThread.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/fiber.h>

#include <vector>
#endif

namespace reaction {

#ifdef __EMSCRIPTEN__

// The program's fiber and the scene's (the main context), with their stacks.
struct GameThread::Fibers {
    static constexpr std::size_t kStack = 1 << 20, kAsyncify = 1 << 20;
    emscripten_fiber_t scene{}, program{};
    std::vector<char> sceneAsyncify = std::vector<char>(kAsyncify);
    std::vector<char> programStack = std::vector<char>(kStack), programAsyncify = std::vector<char>(kAsyncify);
};

GameThread::~GameThread() {
    if (!started_ || done_) return;
    abort_ = true;      // let the program unwind its stack (yield() throws Abort)
    resume();
}

void GameThread::run() {
    try {
        if (!abort_) body_();
    } catch (const Abort&) {
    } catch (...) {
        error_ = std::current_exception();
    }
    done_ = true;
    programTurn_ = false;
    emscripten_fiber_swap(&fibers_->program, &fibers_->scene);   // never comes back
}

void GameThread::resume() {
    if (done_) return;
    if (!started_) {
        started_ = true;
        fibers_ = std::make_unique<Fibers>();
        Fibers& f = *fibers_;
        emscripten_fiber_init_from_current_context(&f.scene, f.sceneAsyncify.data(), f.sceneAsyncify.size());
        emscripten_fiber_init(
            &f.program, [](void* self) { static_cast<GameThread*>(self)->run(); }, this,
            f.programStack.data(), f.programStack.size(), f.programAsyncify.data(), f.programAsyncify.size());
    }
    programTurn_ = true;
    emscripten_fiber_swap(&fibers_->scene, &fibers_->program);
    if (done_ && error_) {
        auto e = error_;
        error_ = nullptr;
        std::rethrow_exception(e);
    }
}

void GameThread::yield() {
    programTurn_ = false;
    emscripten_fiber_swap(&fibers_->program, &fibers_->scene);
    if (abort_) throw Abort{};
}

#else

GameThread::~GameThread() {
    if (!started_) return;
    {
        std::unique_lock lk(m_);
        if (!done_) {
            abort_ = true;
            programTurn_ = true;
            cv_.notify_all();
            cv_.wait(lk, [this] { return done_; });
        }
    }
    if (thread_.joinable()) thread_.join();
}

void GameThread::run() {
    {
        std::unique_lock lk(m_);
        cv_.wait(lk, [this] { return programTurn_; });
    }
    try {
        if (!abort_) body_();
    } catch (const Abort&) {
    } catch (...) {
        error_ = std::current_exception();
    }
    std::unique_lock lk(m_);
    done_ = true;
    programTurn_ = false;
    cv_.notify_all();
}

void GameThread::resume() {
    std::unique_lock lk(m_);
    if (done_) return;
    if (!started_) {
        started_ = true;
        thread_ = std::thread([this] { run(); });
    }
    programTurn_ = true;
    cv_.notify_all();
    cv_.wait(lk, [this] { return !programTurn_; });
    if (done_ && error_) {
        auto e = error_;
        error_ = nullptr;
        std::rethrow_exception(e);
    }
}

void GameThread::yield() {
    std::unique_lock lk(m_);
    programTurn_ = false;
    cv_.notify_all();
    cv_.wait(lk, [this] { return programTurn_; });
    if (abort_) throw Abort{};
}

#endif

GameThread::GameThread(std::function<void()> body) : body_(std::move(body)) {}

}  // namespace reaction
