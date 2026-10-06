#pragma once

#include "core/BoundedQueue.h"
#include "core/Thread.h"

#include <functional>
#include <future>
#include <string>
#include <thread>

namespace lectern::ui {

/// Runs tasks one at a time on a dedicated thread. View models use it for
/// anything that may block (opening devices, file I/O, starting/stopping a
/// session) so the UI thread never waits. Results are posted back with
/// QMetaObject::invokeMethod(context, ..., Qt::QueuedConnection).
class SerialExecutor {
public:
    explicit SerialExecutor(std::string name) : name_(std::move(name)), queue_(256) {
        thread_ = std::thread([this] {
            setCurrentThreadName(name_);
            while (auto task = queue_.pop()) (*task)();
        });
    }
    ~SerialExecutor() {
        queue_.close();
        if (thread_.joinable()) thread_.join();
    }
    SerialExecutor(const SerialExecutor&) = delete;
    SerialExecutor& operator=(const SerialExecutor&) = delete;

    void post(std::function<void()> task) { queue_.push(task); }
    /// Blocks until every task posted so far has run.
    void wait() {
        std::promise<void> done;
        auto finished = done.get_future();
        post([&done] { done.set_value(); });
        finished.wait();
    }

private:
    std::string name_;
    BoundedQueue<std::function<void()>> queue_;
    std::thread thread_;
};

}  // namespace lectern::ui
