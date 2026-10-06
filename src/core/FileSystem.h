#pragma once

#include "core/Error.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace lectern::fs {

namespace stdfs = std::filesystem;

struct AtomicWriteOptions {
    bool fsync = true;          ///< flush data and directory entry to stable storage
    bool keepBackup = false;    ///< refresh "<path>.bak" from the previous version first
};

/// Transactional replace: write "<path>.tmp", fsync, (optionally refresh
/// .bak), rename over <path>, fsync the directory. At every instant either
/// the complete old file or the complete new file exists.
Status writeFileAtomic(const stdfs::path& path, std::string_view data, const AtomicWriteOptions& options = {});

/// Reads a whole file, refusing files larger than maxBytes.
Result<std::string> readFile(const stdfs::path& path, std::uint64_t maxBytes = 256ull << 20);

Status ensureDirectory(const stdfs::path& path);
Status syncDirectory(const stdfs::path& path);

struct DiskSpace {
    std::uint64_t capacity = 0;
    std::uint64_t free = 0;
    std::uint64_t available = 0;  ///< usable by this (unprivileged) process
};
Result<DiskSpace> queryDiskSpace(const stdfs::path& path);

/// Exclusive advisory lock held for the lifetime of the object (flock /
/// LockFileEx). The OS releases it when the process dies, including on
/// SIGKILL or a crash, which makes it a reliable "owner is alive" signal.
class FileLock {
public:
    FileLock() = default;
    ~FileLock();
    FileLock(FileLock&& other) noexcept;
    FileLock& operator=(FileLock&& other) noexcept;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    /// Non-blocking. ErrorCode::DeviceBusy if another process holds it.
    static Result<FileLock> tryAcquire(const stdfs::path& path);

    [[nodiscard]] bool isHeld() const noexcept;
    void release() noexcept;

private:
#if defined(_WIN32)
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
    stdfs::path path_;
};

/// Per-user application data root (settings, logs, recording registry).
///   macOS   ~/Library/Application Support/<Product>
///   Windows %LOCALAPPDATA%\<Product>
///   Linux   $XDG_DATA_HOME/<product> or ~/.local/share/<product>
stdfs::path appDataDirectory();
stdfs::path logDirectory();
/// Default location for new projects (~/Movies/<Product>, Videos, ...).
stdfs::path defaultProjectsDirectory();

/// Replaces characters that are invalid in file names on any supported OS.
std::string sanitizeFileName(std::string_view name);

/// Returns `base` if it does not exist, otherwise "base 2", "base 3", ...
stdfs::path uniquePath(const stdfs::path& base);

}  // namespace lectern::fs
