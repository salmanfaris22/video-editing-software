#pragma once

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

namespace lectern {

/// Cost of one item for BoundedQueue capacity accounting (1 = count-based).
struct UnitCost {
    template <class T>
    std::size_t operator()(const T&) const noexcept {
        return 1;
    }
};

enum class PushResult { Ok, Full, Closed };

/// Multi-producer/multi-consumer bounded queue (mutex + condition variables).
///
/// Capacity is measured in "cost" units: item count by default, bytes when a
/// cost function such as packet size is supplied. A single item larger than
/// the whole capacity is still accepted when the queue is empty, so oversized
/// items cannot deadlock a pipeline.
///
/// `tryPush` never blocks and only moves from the argument on success, so the
/// caller keeps the item on failure (e.g. to count and release a dropped frame).
template <class T, class Cost = UnitCost>
class BoundedQueue {
public:
    explicit BoundedQueue(std::size_t capacity, Cost cost = Cost{}) : capacity_(capacity), cost_(cost) {}

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;

    /// Non-blocking push. On Full/Closed the item is left untouched.
    PushResult tryPush(T& item) {
        const std::size_t c = cost_(item);
        {
            std::lock_guard lock(mutex_);
            if (closed_) return PushResult::Closed;
            if (!fits(c)) return PushResult::Full;
            used_ += c;
            items_.push_back(std::move(item));
        }
        notEmpty_.notify_one();
        return PushResult::Ok;
    }

    /// Blocks up to `timeout` for space. On Full/Closed the item is untouched.
    template <class Rep, class Period>
    PushResult pushFor(T& item, std::chrono::duration<Rep, Period> timeout) {
        const std::size_t c = cost_(item);
        {
            std::unique_lock lock(mutex_);
            if (!notFull_.wait_for(lock, timeout, [&] { return closed_ || fits(c); })) return PushResult::Full;
            if (closed_) return PushResult::Closed;
            used_ += c;
            items_.push_back(std::move(item));
        }
        notEmpty_.notify_one();
        return PushResult::Ok;
    }

    /// Blocks until space is available or the queue is closed.
    PushResult push(T& item) {
        const std::size_t c = cost_(item);
        {
            std::unique_lock lock(mutex_);
            notFull_.wait(lock, [&] { return closed_ || fits(c); });
            if (closed_) return PushResult::Closed;
            used_ += c;
            items_.push_back(std::move(item));
        }
        notEmpty_.notify_one();
        return PushResult::Ok;
    }

    std::optional<T> tryPop() {
        std::unique_lock lock(mutex_);
        return popLocked(lock);
    }

    /// Waits up to `timeout`; returns nullopt on timeout or when closed and drained.
    template <class Rep, class Period>
    std::optional<T> popFor(std::chrono::duration<Rep, Period> timeout) {
        std::unique_lock lock(mutex_);
        notEmpty_.wait_for(lock, timeout, [&] { return closed_ || !items_.empty(); });
        return popLocked(lock);
    }

    /// Blocks until an item arrives or the queue is closed and drained.
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        notEmpty_.wait(lock, [&] { return closed_ || !items_.empty(); });
        return popLocked(lock);
    }

    /// Rejects further pushes and wakes all waiters. Remaining items can still be popped.
    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    /// Removes all items (e.g. on cancel) and returns how many were discarded.
    std::size_t clear() {
        std::size_t n;
        {
            std::lock_guard lock(mutex_);
            n = items_.size();
            items_.clear();
            used_ = 0;
        }
        notFull_.notify_all();
        return n;
    }

    [[nodiscard]] bool isClosed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }
    [[nodiscard]] std::size_t size() const {
        std::lock_guard lock(mutex_);
        return items_.size();
    }
    [[nodiscard]] std::size_t usedCost() const {
        std::lock_guard lock(mutex_);
        return used_;
    }
    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

private:
    bool fits(std::size_t c) const noexcept { return used_ == 0 || used_ + c <= capacity_; }

    std::optional<T> popLocked(std::unique_lock<std::mutex>& lock) {
        if (items_.empty()) return std::nullopt;
        T item = std::move(items_.front());
        items_.pop_front();
        used_ -= cost_(item);
        lock.unlock();
        notFull_.notify_one();
        return item;
    }

    const std::size_t capacity_;
    Cost cost_;
    mutable std::mutex mutex_;
    std::condition_variable notEmpty_;
    std::condition_variable notFull_;
    std::deque<T> items_;
    std::size_t used_ = 0;
    bool closed_ = false;
};

}  // namespace lectern
