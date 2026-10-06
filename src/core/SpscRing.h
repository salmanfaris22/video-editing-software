#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>

namespace lectern {

#if defined(__cpp_lib_hardware_interference_size) && !defined(__APPLE__)
inline constexpr std::size_t kCacheLine = std::hardware_destructive_interference_size;
#else
inline constexpr std::size_t kCacheLine = 64;
#endif

[[nodiscard]] constexpr std::size_t nextPowerOfTwo(std::size_t v) noexcept {
    std::size_t p = 1;
    while (p < v) p <<= 1;
    return p;
}

/// Wait-free single-producer/single-consumer ring of trivially copyable
/// elements, sized to a power of two. Safe for real-time producers: no locks,
/// no allocation after construction, no syscalls.
///
/// Indices grow monotonically and are masked on access; head - tail is the
/// fill level. Each side caches the other's index to avoid cache-line traffic.
template <class T>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>, "SpscRing requires trivially copyable elements");

public:
    explicit SpscRing(std::size_t minCapacity)
        : capacity_(nextPowerOfTwo(std::max<std::size_t>(minCapacity, 2))),
          mask_(capacity_ - 1),
          buffer_(new T[capacity_]) {}

    SpscRing(const SpscRing&) = delete;
    SpscRing& operator=(const SpscRing&) = delete;

    [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

    // ---- producer side ----------------------------------------------------
    /// Exact free space (refreshes the consumer index; a stale cached value
    /// would under-report space and make a producer drop data needlessly).
    [[nodiscard]] std::size_t writeAvailable() noexcept {
        cachedTail_ = tail_.load(std::memory_order_acquire);
        return capacity_ - (head_.load(std::memory_order_relaxed) - cachedTail_);
    }

    /// Writes up to `count` elements; returns how many were written.
    std::size_t write(const T* data, std::size_t count) noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        std::size_t free = capacity_ - (head - cachedTail_);
        if (free < count) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            free = capacity_ - (head - cachedTail_);
        }
        const std::size_t n = std::min(count, free);
        if (n == 0) return 0;
        const std::size_t start = head & mask_;
        const std::size_t first = std::min(n, capacity_ - start);
        std::memcpy(buffer_.get() + start, data, first * sizeof(T));
        if (n > first) std::memcpy(buffer_.get(), data + first, (n - first) * sizeof(T));
        head_.store(head + n, std::memory_order_release);
        return n;
    }

    bool push(const T& item) noexcept { return write(&item, 1) == 1; }

    // ---- consumer side ----------------------------------------------------
    /// Exact number of readable elements (refreshes the producer index).
    [[nodiscard]] std::size_t readAvailable() noexcept {
        cachedHead_ = head_.load(std::memory_order_acquire);
        return cachedHead_ - tail_.load(std::memory_order_relaxed);
    }

    /// Reads up to `count` elements; returns how many were read.
    std::size_t read(T* out, std::size_t count) noexcept {
        const std::size_t n = peek(out, count);
        if (n) skip(n);
        return n;
    }

    /// Copies up to `count` elements without consuming them.
    std::size_t peek(T* out, std::size_t count) noexcept {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        std::size_t avail = cachedHead_ - tail;
        if (avail < count) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            avail = cachedHead_ - tail;
        }
        const std::size_t n = std::min(count, avail);
        if (n == 0) return 0;
        const std::size_t start = tail & mask_;
        const std::size_t first = std::min(n, capacity_ - start);
        std::memcpy(out, buffer_.get() + start, first * sizeof(T));
        if (n > first) std::memcpy(out + first, buffer_.get(), (n - first) * sizeof(T));
        return n;
    }

    /// Consumes `count` elements (must be <= readAvailable()).
    void skip(std::size_t count) noexcept {
        tail_.store(tail_.load(std::memory_order_relaxed) + count, std::memory_order_release);
    }

    bool pop(T& out) noexcept { return read(&out, 1) == 1; }

    /// Empties the ring. Only while neither the producer nor the consumer is
    /// running (e.g. with the audio device stopped).
    void reset() noexcept {
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        cachedTail_ = 0;
        cachedHead_ = 0;
    }

private:
    const std::size_t capacity_;
    const std::size_t mask_;
    std::unique_ptr<T[]> buffer_;

    alignas(kCacheLine) std::atomic<std::size_t> head_{0};  // written by producer
    std::size_t cachedTail_ = 0;                              // producer's view of tail
    alignas(kCacheLine) std::atomic<std::size_t> tail_{0};  // written by consumer
    std::size_t cachedHead_ = 0;                              // consumer's view of head
};

}  // namespace lectern
