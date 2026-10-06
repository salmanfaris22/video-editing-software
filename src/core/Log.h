#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <memory>
#include <string>
#include <string_view>

namespace lectern {

enum class LogLevel : std::uint8_t { Trace, Debug, Info, Warn, Error, Critical, Off };

[[nodiscard]] std::string_view toString(LogLevel level) noexcept;
[[nodiscard]] LogLevel logLevelFromString(std::string_view text, LogLevel fallback = LogLevel::Info) noexcept;

struct LogRecord {
    LogLevel level = LogLevel::Info;
    std::chrono::system_clock::time_point time;
    std::string category;
    std::string message;
    std::string threadName;
    const char* file = nullptr;
    int line = 0;
};

class ILogSink {
public:
    virtual ~ILogSink() = default;
    virtual void write(const LogRecord& record) = 0;
    virtual void flush() {}
};

/// Writes to stderr (colored when attached to a terminal).
std::shared_ptr<ILogSink> makeStderrSink();
/// Size-rotated file sink: path, path.1, ..., path.<keep>.
std::shared_ptr<ILogSink> makeRotatingFileSink(const std::filesystem::path& path,
                                               std::uint64_t maxBytes = 10ull << 20, int keep = 5);

/// Process-wide asynchronous logger.
///
/// This is the one deliberate global in the codebase: every module must be
/// able to log without plumbing a logger through every constructor.
/// Callers format on their own thread; a background thread writes to sinks.
/// When the bounded queue is full, records are dropped and counted, and
/// logging never blocks the caller. Real-time audio callbacks must not log
/// at all (they bump atomic counters instead).
class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level) noexcept { level_.store(level, std::memory_order_relaxed); }
    [[nodiscard]] LogLevel level() const noexcept { return level_.load(std::memory_order_relaxed); }
    [[nodiscard]] bool shouldLog(LogLevel level) const noexcept {
        return level >= level_.load(std::memory_order_relaxed) && level != LogLevel::Off;
    }

    void addSink(std::shared_ptr<ILogSink> sink);
    void clearSinks();

    void log(LogLevel level, std::string_view category, std::string message, const char* file, int line);

    /// Blocks until queued records are written (call before exit / after crashes are handled).
    void flush();
    /// Flushes and stops the writer thread. Later log calls write synchronously.
    void shutdown();

    [[nodiscard]] std::uint64_t droppedCount() const noexcept;

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    Logger();
    ~Logger();
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<LogLevel> level_{LogLevel::Info};
};

template <class... Args>
void logFormatted(LogLevel level, std::string_view category, const char* file, int line,
                  std::format_string<Args...> fmt, Args&&... args) {
    Logger& logger = Logger::instance();
    if (!logger.shouldLog(level)) return;
    logger.log(level, category, std::format(fmt, std::forward<Args>(args)...), file, line);
}

}  // namespace lectern

#define LEC_LOG(level, category, ...) ::lectern::logFormatted(level, category, __FILE__, __LINE__, __VA_ARGS__)
#define LEC_TRACE(category, ...) LEC_LOG(::lectern::LogLevel::Trace, category, __VA_ARGS__)
#define LEC_DEBUG(category, ...) LEC_LOG(::lectern::LogLevel::Debug, category, __VA_ARGS__)
#define LEC_INFO(category, ...) LEC_LOG(::lectern::LogLevel::Info, category, __VA_ARGS__)
#define LEC_WARN(category, ...) LEC_LOG(::lectern::LogLevel::Warn, category, __VA_ARGS__)
#define LEC_ERROR(category, ...) LEC_LOG(::lectern::LogLevel::Error, category, __VA_ARGS__)
#define LEC_CRITICAL(category, ...) LEC_LOG(::lectern::LogLevel::Critical, category, __VA_ARGS__)
