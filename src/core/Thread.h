#pragma once

#include <string>
#include <string_view>

namespace lectern {

enum class ThreadPriority {
    Background,   ///< proxies, thumbnails
    Normal,
    High,         ///< encoders, muxers, capture pumps
    AudioRealtime ///< audio pumps that feed real-time consumers
};

/// Names the calling thread for debuggers/profilers and for log records.
/// Convention: "lectern.<component>.<id>".
void setCurrentThreadName(std::string_view name);
[[nodiscard]] const std::string& currentThreadName();

/// Best effort; failures are ignored (some platforms need privileges).
void setCurrentThreadPriority(ThreadPriority priority);

}  // namespace lectern
