#include "core/Log.h"

#include "core/Thread.h"

#include <condition_variable>
#include <cstdio>
#include <ctime>
#include <deque>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#define LEC_ISATTY _isatty
#define LEC_FILENO _fileno
#else
#include <unistd.h>
#define LEC_ISATTY isatty
#define LEC_FILENO fileno
#endif

namespace lectern {

std::string_view toString(LogLevel level) noexcept {
    switch (level) {
        case LogLevel::Trace: return "trace";
        case LogLevel::Debug: return "debug";
        case LogLevel::Info: return "info";
        case LogLevel::Warn: return "warn";
        case LogLevel::Error: return "error";
        case LogLevel::Critical: return "critical";
        case LogLevel::Off: return "off";
    }
    return "info";
}

LogLevel logLevelFromString(std::string_view text, LogLevel fallback) noexcept {
    for (auto l : {LogLevel::Trace, LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error,
                   LogLevel::Critical, LogLevel::Off}) {
        if (toString(l) == text) return l;
    }
    return fallback;
}

namespace {

std::string formatTimestamp(std::chrono::system_clock::time_point tp) {
    const auto secs = std::chrono::time_point_cast<std::chrono::seconds>(tp);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tp - secs).count();
    const std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[40];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d %02d:%02d:%02d.%03lld", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<long long>(ms));
    return buf;
}

std::string formatLine(const LogRecord& r) {
    std::string line = formatTimestamp(r.time);
    line += " [";
    line += toString(r.level);
    line += "] [";
    line += r.category;
    line += "] ";
    if (!r.threadName.empty()) {
        line += '(';
        line += r.threadName;
        line += ") ";
    }
    line += r.message;
    return line;
}

class StderrSink final : public ILogSink {
public:
    StderrSink() : color_(LEC_ISATTY(LEC_FILENO(stderr)) != 0) {}
    void write(const LogRecord& r) override {
        const std::string line = formatLine(r);
        const char* prefix = "";
        if (color_) {
            switch (r.level) {
                case LogLevel::Warn: prefix = "\x1b[33m"; break;
                case LogLevel::Error:
                case LogLevel::Critical: prefix = "\x1b[31m"; break;
                case LogLevel::Debug:
                case LogLevel::Trace: prefix = "\x1b[90m"; break;
                default: break;
            }
        }
        std::fprintf(stderr, "%s%s%s\n", prefix, line.c_str(), (color_ && *prefix) ? "\x1b[0m" : "");
    }
    void flush() override { std::fflush(stderr); }

private:
    bool color_;
};

class RotatingFileSink final : public ILogSink {
public:
    RotatingFileSink(std::filesystem::path path, std::uint64_t maxBytes, int keep)
        : path_(std::move(path)), maxBytes_(maxBytes), keep_(keep) {
        std::error_code ec;
        std::filesystem::create_directories(path_.parent_path(), ec);
        open();
    }
    void write(const LogRecord& r) override {
        if (!out_.is_open()) return;
        const std::string line = formatLine(r);
        out_ << line << '\n';
        written_ += line.size() + 1;
        if (written_ >= maxBytes_) rotate();
    }
    void flush() override {
        if (out_.is_open()) out_.flush();
    }

private:
    void open() {
        out_.open(path_, std::ios::app);
        std::error_code ec;
        written_ = std::filesystem::exists(path_, ec) ? std::filesystem::file_size(path_, ec) : 0;
        if (ec) written_ = 0;
    }
    void rotate() {
        out_.close();
        std::error_code ec;
        for (int i = keep_ - 1; i >= 1; --i) {
            const auto from = path_.string() + "." + std::to_string(i);
            const auto to = path_.string() + "." + std::to_string(i + 1);
            std::filesystem::rename(from, to, ec);
        }
        std::filesystem::rename(path_, path_.string() + ".1", ec);
        open();
    }

    std::filesystem::path path_;
    std::uint64_t maxBytes_;
    int keep_;
    std::ofstream out_;
    std::uint64_t written_ = 0;
};

}  // namespace

std::shared_ptr<ILogSink> makeStderrSink() { return std::make_shared<StderrSink>(); }

std::shared_ptr<ILogSink> makeRotatingFileSink(const std::filesystem::path& path, std::uint64_t maxBytes, int keep) {
    return std::make_shared<RotatingFileSink>(path, maxBytes, keep);
}

struct Logger::Impl {
    static constexpr std::size_t kCapacity = 8192;

    std::mutex mutex;
    std::condition_variable cv;
    std::condition_variable drained;
    std::deque<LogRecord> queue;
    std::vector<std::shared_ptr<ILogSink>> sinks;
    std::atomic<std::uint64_t> dropped{0};
    bool stopping = false;
    bool running = false;
    bool busy = false;
    std::thread worker;

    void start() {
        running = true;
        worker = std::thread([this] { run(); });
    }

    void run() {
        setCurrentThreadName("lectern.log");
        std::vector<LogRecord> batch;
        std::unique_lock lock(mutex);
        for (;;) {
            cv.wait(lock, [this] { return stopping || !queue.empty(); });
            if (queue.empty() && stopping) break;
            batch.assign(std::make_move_iterator(queue.begin()), std::make_move_iterator(queue.end()));
            queue.clear();
            busy = true;
            auto localSinks = sinks;
            lock.unlock();
            for (const auto& rec : batch) {
                for (const auto& s : localSinks) s->write(rec);
            }
            const auto lost = dropped.exchange(0, std::memory_order_relaxed);
            if (lost > 0) {
                LogRecord warn{LogLevel::Warn, std::chrono::system_clock::now(), "log",
                               std::to_string(lost) + " log records dropped (queue full)", {}, nullptr, 0};
                for (const auto& s : localSinks) s->write(warn);
            }
            for (const auto& s : localSinks) s->flush();
            batch.clear();
            lock.lock();
            busy = false;
            drained.notify_all();
        }
    }
};

Logger::Logger() : impl_(std::make_unique<Impl>()) { impl_->start(); }

Logger::~Logger() { shutdown(); }

Logger& Logger::instance() {
    // Intentionally leaked: guarantees logging works during static destruction.
    static Logger* logger = new Logger();
    return *logger;
}

void Logger::addSink(std::shared_ptr<ILogSink> sink) {
    std::lock_guard lock(impl_->mutex);
    impl_->sinks.push_back(std::move(sink));
}

void Logger::clearSinks() {
    std::lock_guard lock(impl_->mutex);
    impl_->sinks.clear();
}

void Logger::log(LogLevel level, std::string_view category, std::string message, const char* file, int line) {
    LogRecord rec{level, std::chrono::system_clock::now(), std::string(category), std::move(message),
                  currentThreadName(), file, line};
    std::unique_lock lock(impl_->mutex);
    if (!impl_->running) {
        // After shutdown: write synchronously so late messages are not lost.
        auto sinks = impl_->sinks;
        lock.unlock();
        for (const auto& s : sinks) {
            s->write(rec);
            s->flush();
        }
        return;
    }
    if (impl_->queue.size() >= Impl::kCapacity) {
        impl_->dropped.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    impl_->queue.push_back(std::move(rec));
    lock.unlock();
    impl_->cv.notify_one();
}

void Logger::flush() {
    std::unique_lock lock(impl_->mutex);
    if (!impl_->running) return;
    impl_->cv.notify_one();
    impl_->drained.wait(lock, [this] { return impl_->queue.empty() && !impl_->busy; });
}

void Logger::shutdown() {
    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->running) return;
        impl_->stopping = true;
    }
    impl_->cv.notify_all();
    if (impl_->worker.joinable()) impl_->worker.join();
    std::lock_guard lock(impl_->mutex);
    impl_->running = false;
}

std::uint64_t Logger::droppedCount() const noexcept { return impl_->dropped.load(std::memory_order_relaxed); }

}  // namespace lectern
