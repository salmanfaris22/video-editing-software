#include "core/FileSystem.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>

#if defined(_WIN32)
#include <windows.h>
#include <shlobj.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#ifndef LECTERN_PRODUCT_NAME
#define LECTERN_PRODUCT_NAME "Lectern"
#endif

namespace lectern::fs {

namespace {

#if !defined(_WIN32)
Status writeAllFd(int fd, std::string_view data, const stdfs::path& path) {
    std::size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = ::write(fd, data.data() + done, data.size() - done);
        if (n < 0) {
            if (errno == EINTR) continue;
            const int err = errno;
            return fail(errnoError(errorCodeFromErrno(err), "write " + path.string(), err));
        }
        done += static_cast<std::size_t>(n);
    }
    return ok();
}

Status fsyncFd(int fd, const stdfs::path& path) {
#if defined(__APPLE__)
    // F_FULLFSYNC also flushes the drive's write cache.
    if (::fcntl(fd, F_FULLFSYNC) == 0) return ok();
#endif
    if (::fsync(fd) != 0) {
        const int err = errno;
        return fail(errnoError(errorCodeFromErrno(err), "fsync " + path.string(), err));
    }
    return ok();
}
#endif

std::string productDirName(bool lowercase) {
    std::string name = LECTERN_PRODUCT_NAME;
    if (lowercase) {
        for (char& c : name) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return name;
}

stdfs::path homeDirectory() {
#if defined(_WIN32)
    if (const char* p = std::getenv("USERPROFILE")) return p;
    return stdfs::temp_directory_path();
#else
    if (const char* p = std::getenv("HOME")) return p;
    return stdfs::temp_directory_path();
#endif
}

}  // namespace

Status ensureDirectory(const stdfs::path& path) {
    std::error_code ec;
    stdfs::create_directories(path, ec);
    if (ec) return fail(ErrorCode::IoError, "create directory " + path.string() + ": " + ec.message(), ec.value());
    return ok();
}

Status syncDirectory(const stdfs::path& path) {
#if defined(_WIN32)
    (void)path;  // NTFS metadata is journaled; MoveFileEx(WRITE_THROUGH) covers ordering.
    return ok();
#else
    const int fd = ::open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        const int err = errno;
        return fail(errnoError(errorCodeFromErrno(err), "open directory " + path.string(), err));
    }
    const int rc = ::fsync(fd);
    const int err = errno;
    ::close(fd);
    // Some filesystems (e.g. network shares) reject fsync on directories.
    if (rc != 0 && err != EINVAL && err != ENOTSUP) {
        return fail(errnoError(errorCodeFromErrno(err), "fsync directory " + path.string(), err));
    }
    return ok();
#endif
}

Status writeFileAtomic(const stdfs::path& path, std::string_view data, const AtomicWriteOptions& options) {
    const stdfs::path dir = path.has_parent_path() ? path.parent_path() : stdfs::path(".");
    LEC_TRY(ensureDirectory(dir));
    const stdfs::path tmp = stdfs::path(path.string() + ".tmp");

#if defined(_WIN32)
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return fail(ErrorCode::IoError, "open " + tmp.string());
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        out.flush();
        if (!out) return fail(ErrorCode::IoError, "write " + tmp.string());
    }
    if (options.fsync) {
        HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (h != INVALID_HANDLE_VALUE) {
            FlushFileBuffers(h);
            CloseHandle(h);
        }
    }
    std::error_code ec;
    const bool exists = stdfs::exists(path, ec);
    if (exists) {
        const std::wstring backup = options.keepBackup ? (path.wstring() + L".bak") : std::wstring();
        if (!ReplaceFileW(path.c_str(), tmp.c_str(), backup.empty() ? nullptr : backup.c_str(),
                          REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) {
            return fail(ErrorCode::IoError, "replace " + path.string(), static_cast<std::int64_t>(GetLastError()));
        }
    } else if (!MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return fail(ErrorCode::IoError, "rename " + tmp.string(), static_cast<std::int64_t>(GetLastError()));
    }
    return ok();
#else
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        const int err = errno;
        return fail(errnoError(errorCodeFromErrno(err), "open " + tmp.string(), err));
    }
    Status st = writeAllFd(fd, data, tmp);
    if (st && options.fsync) st = fsyncFd(fd, tmp);
    ::close(fd);
    if (!st) {
        std::error_code ec;
        stdfs::remove(tmp, ec);
        return st;
    }

    std::error_code ec;
    if (options.keepBackup && stdfs::exists(path, ec)) {
        // Refresh .bak atomically via a hard link to the current version, so
        // the current file is never moved away (there is no window without it).
        const stdfs::path bak = stdfs::path(path.string() + ".bak");
        const stdfs::path bakTmp = stdfs::path(path.string() + ".bak.tmp");
        stdfs::remove(bakTmp, ec);
        if (::link(path.c_str(), bakTmp.c_str()) == 0) {
            if (::rename(bakTmp.c_str(), bak.c_str()) != 0) stdfs::remove(bakTmp, ec);
        } else {
            stdfs::copy_file(path, bak, stdfs::copy_options::overwrite_existing, ec);
        }
    }

    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        const int err = errno;
        stdfs::remove(tmp, ec);
        return fail(errnoError(errorCodeFromErrno(err), "rename " + tmp.string(), err));
    }
    if (options.fsync) LEC_TRY(syncDirectory(dir));
    return ok();
#endif
}

Result<std::string> readFile(const stdfs::path& path, std::uint64_t maxBytes) {
    std::error_code ec;
    const auto size = stdfs::file_size(path, ec);
    if (ec) {
        return fail(ec == std::errc::no_such_file_or_directory ? ErrorCode::NotFound : ErrorCode::IoError,
                    "stat " + path.string() + ": " + ec.message(), ec.value());
    }
    if (size > maxBytes) {
        return fail(ErrorCode::InvalidArgument,
                    "file " + path.string() + " is too large (" + std::to_string(size) + " bytes)");
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) return fail(ErrorCode::IoError, "open " + path.string());
    std::string data(static_cast<std::size_t>(size), '\0');
    in.read(data.data(), static_cast<std::streamsize>(size));
    if (static_cast<std::uint64_t>(in.gcount()) != size) return fail(ErrorCode::IoError, "short read " + path.string());
    return data;
}

Result<DiskSpace> queryDiskSpace(const stdfs::path& path) {
    std::error_code ec;
    // Walk up to an existing directory (the target may not exist yet).
    stdfs::path probe = path;
    while (!probe.empty() && !stdfs::exists(probe, ec)) probe = probe.parent_path();
    if (probe.empty()) probe = ".";
    const auto info = stdfs::space(probe, ec);
    if (ec) return fail(ErrorCode::IoError, "query disk space for " + path.string() + ": " + ec.message(), ec.value());
    return DiskSpace{info.capacity, info.free, info.available};
}

// ---------------------------------------------------------------------------
// FileLock

FileLock::~FileLock() { release(); }

FileLock::FileLock(FileLock&& other) noexcept { *this = std::move(other); }

FileLock& FileLock::operator=(FileLock&& other) noexcept {
    if (this != &other) {
        release();
#if defined(_WIN32)
        handle_ = other.handle_;
        other.handle_ = nullptr;
#else
        fd_ = other.fd_;
        other.fd_ = -1;
#endif
        path_ = std::move(other.path_);
    }
    return *this;
}

bool FileLock::isHeld() const noexcept {
#if defined(_WIN32)
    return handle_ != nullptr;
#else
    return fd_ >= 0;
#endif
}

void FileLock::release() noexcept {
#if defined(_WIN32)
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));  // closing releases the lock
        handle_ = nullptr;
    }
#else
    if (fd_ >= 0) {
        ::flock(fd_, LOCK_UN);
        ::close(fd_);
        fd_ = -1;
    }
#endif
}

Result<FileLock> FileLock::tryAcquire(const stdfs::path& path) {
    if (path.has_parent_path()) LEC_TRY(ensureDirectory(path.parent_path()));
    FileLock lock;
    lock.path_ = path;
#if defined(_WIN32)
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        return fail(ErrorCode::IoError, "open lock " + path.string(), static_cast<std::int64_t>(GetLastError()));
    }
    OVERLAPPED ov{};
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        CloseHandle(h);
        return fail(ErrorCode::DeviceBusy, "lock held by another process: " + path.string());
    }
    lock.handle_ = h;
#else
    const int fd = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0) {
        const int err = errno;
        return fail(errnoError(errorCodeFromErrno(err), "open lock " + path.string(), err));
    }
    if (::flock(fd, LOCK_EX | LOCK_NB) != 0) {
        const int err = errno;
        ::close(fd);
        if (err == EWOULDBLOCK) return fail(ErrorCode::DeviceBusy, "lock held by another process: " + path.string());
        return fail(errnoError(ErrorCode::IoError, "flock " + path.string(), err));
    }
    lock.fd_ = fd;
#endif
    return lock;
}

// ---------------------------------------------------------------------------

stdfs::path appDataDirectory() {
    if (const char* overrideDir = std::getenv("LECTERN_APP_DATA_DIR")) return overrideDir;
#if defined(__APPLE__)
    return homeDirectory() / "Library" / "Application Support" / productDirName(false);
#elif defined(_WIN32)
    if (const char* p = std::getenv("LOCALAPPDATA")) return stdfs::path(p) / productDirName(false);
    return homeDirectory() / "AppData" / "Local" / productDirName(false);
#else
    if (const char* p = std::getenv("XDG_DATA_HOME"); p && *p) return stdfs::path(p) / productDirName(true);
    return homeDirectory() / ".local" / "share" / productDirName(true);
#endif
}

stdfs::path logDirectory() {
#if defined(__APPLE__)
    if (std::getenv("LECTERN_APP_DATA_DIR")) return appDataDirectory() / "logs";
    return homeDirectory() / "Library" / "Logs" / productDirName(false);
#else
    return appDataDirectory() / "logs";
#endif
}

stdfs::path defaultProjectsDirectory() {
#if defined(__APPLE__)
    return homeDirectory() / "Movies" / productDirName(false);
#elif defined(_WIN32)
    return homeDirectory() / "Videos" / productDirName(false);
#else
    if (const char* p = std::getenv("XDG_VIDEOS_DIR"); p && *p) return stdfs::path(p) / productDirName(false);
    return homeDirectory() / "Videos" / productDirName(false);
#endif
}

std::string sanitizeFileName(std::string_view name) {
    std::string out;
    out.reserve(name.size());
    for (const char c : name) {
        const auto uc = static_cast<unsigned char>(c);
        if (uc < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
            c == '>' || c == '|') {
            out += '-';
        } else {
            out += c;
        }
    }
    // Trim spaces/dots at the ends (invalid on Windows).
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    std::size_t start = 0;
    while (start < out.size() && out[start] == ' ') ++start;
    out.erase(0, start);
    if (out.empty()) out = "Untitled";
    if (out.size() > 120) out.resize(120);
    return out;
}

stdfs::path uniquePath(const stdfs::path& base) {
    std::error_code ec;
    if (!stdfs::exists(base, ec)) return base;
    const stdfs::path parent = base.parent_path();
    const std::string stem = base.stem().string();
    const std::string ext = base.extension().string();
    for (int i = 2; i < 10000; ++i) {
        stdfs::path candidate = parent / (stem + " " + std::to_string(i) + ext);
        if (!stdfs::exists(candidate, ec)) return candidate;
    }
    return parent / (stem + " " + std::to_string(std::rand()) + ext);
}

}  // namespace lectern::fs
