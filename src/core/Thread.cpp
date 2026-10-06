#include "core/Thread.h"

#if defined(__APPLE__)
#include <pthread.h>
#include <pthread/qos.h>
#elif defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

namespace lectern {
namespace {
thread_local std::string tlsThreadName;
}

void setCurrentThreadName(std::string_view name) {
    tlsThreadName.assign(name);
#if defined(__APPLE__)
    pthread_setname_np(tlsThreadName.c_str());
#elif defined(_WIN32)
    std::wstring wide(name.begin(), name.end());
    SetThreadDescription(GetCurrentThread(), wide.c_str());
#else
    // Linux limits names to 15 characters plus the terminator.
    std::string truncated = tlsThreadName.substr(0, 15);
    pthread_setname_np(pthread_self(), truncated.c_str());
#endif
}

const std::string& currentThreadName() { return tlsThreadName; }

void setCurrentThreadPriority(ThreadPriority priority) {
#if defined(__APPLE__)
    qos_class_t qos = QOS_CLASS_DEFAULT;
    switch (priority) {
        case ThreadPriority::Background: qos = QOS_CLASS_UTILITY; break;
        case ThreadPriority::Normal: qos = QOS_CLASS_DEFAULT; break;
        case ThreadPriority::High: qos = QOS_CLASS_USER_INITIATED; break;
        case ThreadPriority::AudioRealtime: qos = QOS_CLASS_USER_INTERACTIVE; break;
    }
    pthread_set_qos_class_self_np(qos, 0);
#elif defined(_WIN32)
    int p = THREAD_PRIORITY_NORMAL;
    switch (priority) {
        case ThreadPriority::Background: p = THREAD_PRIORITY_BELOW_NORMAL; break;
        case ThreadPriority::Normal: p = THREAD_PRIORITY_NORMAL; break;
        case ThreadPriority::High: p = THREAD_PRIORITY_ABOVE_NORMAL; break;
        case ThreadPriority::AudioRealtime: p = THREAD_PRIORITY_HIGHEST; break;
    }
    SetThreadPriority(GetCurrentThread(), p);
#else
    (void)priority;  // Needs RLIMIT_RTPRIO/rtkit on Linux; leave default scheduling.
#endif
}

}  // namespace lectern
