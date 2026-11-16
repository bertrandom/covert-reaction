#include "app/GameThread.h"

namespace redux {

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

}  // namespace redux
