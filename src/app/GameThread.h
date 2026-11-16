// A program that runs the way the original's does -- straight-line code that
// waits for keys, plays animations and shows menus in the middle of its rules --
// on a thread of its own, in lock-step with the scene: the scene resumes it and
// waits until it yields (asks the scene for something) or ends, so only one of
// the two runs at a time and a run is deterministic. GAME's city loop (and the
// simulation's routines it calls, which wait for the player deep inside) is such
// a program; this keeps it close to the C instead of turning it inside out.
//
// On the web (no threads without cross-origin isolation) the program runs on an
// Emscripten fiber instead: the same hand-over, by switching stacks on the one
// thread (needs -sASYNCIFY).
#pragma once

#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <memory>
#include <thread>

namespace reaction {

class GameThread {
public:
    // Thrown by yield() in the program when the scene is destroyed while the
    // program is still waiting: it unwinds the program's stack.
    struct Abort {};

    explicit GameThread(std::function<void()> body);
    ~GameThread();   // defined in GameThread.cpp (Fibers is incomplete here)
    GameThread(const GameThread&) = delete;
    GameThread& operator=(const GameThread&) = delete;

    // Scene side: runs the program until it yields or ends (starts it the first
    // time). Rethrows an exception the program ended with.
    void resume();
    bool done() const { return done_; }

    // Program side: hands control back to the scene until the next resume().
    void yield();

private:
    void run();

    std::function<void()> body_;
#ifdef __EMSCRIPTEN__
    struct Fibers;
    std::unique_ptr<Fibers> fibers_;
#else
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
#endif
    bool started_ = false;
    bool programTurn_ = false;
    bool done_ = false;
    bool abort_ = false;
    std::exception_ptr error_;
};

}  // namespace reaction
