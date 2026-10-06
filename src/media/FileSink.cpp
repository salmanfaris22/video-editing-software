#include "media/FileSink.h"

#include <cerrno>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace lectern::media {

namespace {
#if defined(_WIN32)
int platformWrite(int fd, const void* buf, unsigned size) { return _write(fd, buf, size); }
std::int64_t platformSeek(int fd, std::int64_t off, int whence) { return _lseeki64(fd, off, whence); }
std::int64_t platformSize(int fd) {
    struct _stat64 st{};
    return _fstat64(fd, &st) == 0 ? st.st_size : -1;
}
int platformClose(int fd) { return _close(fd); }
#else
ssize_t platformWrite(int fd, const void* buf, std::size_t size) { return ::write(fd, buf, size); }
std::int64_t platformSeek(int fd, std::int64_t off, int whence) { return ::lseek(fd, off, whence); }
std::int64_t platformSize(int fd) {
    struct stat st{};
    return ::fstat(fd, &st) == 0 ? st.st_size : -1;
}
int platformClose(int fd) { return ::close(fd); }
#endif
}  // namespace

Result<std::unique_ptr<FileSink>> FileSink::create(const std::filesystem::path& path, const FileSinkOptions& options) {
    std::unique_ptr<FileSink> sink(new FileSink());
    sink->path_ = path;
#if defined(_WIN32)
    sink->fd_ = _wopen(path.c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, _S_IREAD | _S_IWRITE);
#else
    sink->fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
#endif
    if (sink->fd_ < 0) {
        const int err = errno;
        return fail(errnoError(errorCodeFromErrno(err), "create " + path.string(), err));
    }
    auto* buffer = static_cast<unsigned char*>(av_malloc(static_cast<std::size_t>(options.bufferSize)));
    if (!buffer) return fail(ErrorCode::OutOfMemory, "allocate AVIO buffer");
    sink->avio_ = avio_alloc_context(buffer, options.bufferSize, 1, sink.get(), nullptr, &FileSink::writePacket,
                                     &FileSink::seek);
    if (!sink->avio_) {
        av_free(buffer);
        return fail(ErrorCode::OutOfMemory, "avio_alloc_context");
    }
    return sink;
}

FileSink::~FileSink() { (void)close(false); }

int FileSink::writePacket(void* opaque, const std::uint8_t* buf, int size) {
    auto* self = static_cast<FileSink*>(opaque);
    int done = 0;
    while (done < size) {
        const auto n = platformWrite(self->fd_, buf + done, static_cast<unsigned>(size - done));
        if (n < 0) {
            if (errno == EINTR) continue;
            const int err = errno;
            self->lastErrno_.store(err, std::memory_order_relaxed);
            return AVERROR(err);
        }
        done += static_cast<int>(n);
    }
    self->position_ += size;
    const auto pos = static_cast<std::uint64_t>(self->position_);
    if (pos > self->size_.load(std::memory_order_relaxed)) self->size_.store(pos, std::memory_order_relaxed);
    return size;
}

std::int64_t FileSink::seek(void* opaque, std::int64_t offset, int whence) {
    auto* self = static_cast<FileSink*>(opaque);
    whence &= ~AVSEEK_FORCE;
    if (whence == AVSEEK_SIZE) {
        const std::int64_t sz = platformSize(self->fd_);
        return sz >= 0 ? sz : AVERROR(errno);
    }
    const std::int64_t pos = platformSeek(self->fd_, offset, whence);
    if (pos < 0) {
        const int err = errno;
        self->lastErrno_.store(err, std::memory_order_relaxed);
        return AVERROR(err);
    }
    self->position_ = pos;
    return pos;
}

Status FileSink::flushBuffer() {
    if (!avio_) return ok();
    avio_flush(avio_);
    if (avio_->error < 0) return fail(ffError(avio_->error, "write " + path_.string(), ErrorCode::IoError));
    return ok();
}

Status FileSink::sync(bool full) {
    if (fd_ < 0) return ok();
#if defined(_WIN32)
    (void)full;
    if (_commit(fd_) != 0) {
        const int err = errno;
        return fail(errnoError(errorCodeFromErrno(err), "commit " + path_.string(), err));
    }
#else
#if defined(__APPLE__)
    if (full && ::fcntl(fd_, F_FULLFSYNC) == 0) return ok();
#else
    (void)full;
#endif
    if (::fsync(fd_) != 0) {
        const int err = errno;
        return fail(errnoError(errorCodeFromErrno(err), "fsync " + path_.string(), err));
    }
#endif
    return ok();
}

Status FileSink::close(bool syncToDisk) {
    Status result = ok();
    if (avio_) {
        avio_flush(avio_);
        if (avio_->error < 0) result = fail(ffError(avio_->error, "write " + path_.string(), ErrorCode::IoError));
        av_freep(&avio_->buffer);
        avio_context_free(&avio_);
    }
    if (fd_ >= 0) {
        if (syncToDisk && result) result = sync(true);
        platformClose(fd_);
        fd_ = -1;
    }
    return result;
}

}  // namespace lectern::media
