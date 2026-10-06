#include "capture/InputEventRecorder.h"

#include "core/FileSystem.h"
#include "core/Log.h"

namespace lectern::capture {

namespace {
constexpr std::size_t kMaxPointerEvents = 500'000;
constexpr std::size_t kMaxKeyEvents = 100'000;
}  // namespace

struct InputEventRecorder::Impl {};

InputEventRecorder::InputEventRecorder(const SessionClock& clock, std::filesystem::path outputFile, int captureWidth,
                                       int captureHeight)
    : impl_(std::make_unique<Impl>()),
      clock_(clock),
      outputFile_(std::move(outputFile)),
      captureWidth_(captureWidth),
      captureHeight_(captureHeight) {
    log_.captureWidth = captureWidth_;
    log_.captureHeight = captureHeight_;
}

InputEventRecorder::~InputEventRecorder() { stop(); }

Status InputEventRecorder::start() {
    if (running_.exchange(true)) return ok();
    return startPlatformCapture(this);
}

void InputEventRecorder::stop() {
    if (!running_.exchange(false)) return;
    stopPlatformCapture(this);
}

void InputEventRecorder::ingestPointer(std::int64_t hostNs, std::string_view type, double x, double y, int button) {
    const std::optional<Time> t = clock_.toSessionTime(hostNs);
    if (!t) return;
    recordPointer(*t, type, x, y, button);
}

void InputEventRecorder::ingestKey(std::int64_t hostNs, std::string_view type, std::string key, std::uint32_t modifiers) {
    const std::optional<Time> t = clock_.toSessionTime(hostNs);
    if (!t) return;
    recordKey(*t, type, std::move(key), modifiers);
}

void InputEventRecorder::recordPointer(Time sessionTime, std::string_view type, double x, double y, int button) {
    if (!running_) return;
    std::lock_guard lock(mutex_);
    if (log_.pointer.size() >= kMaxPointerEvents) return;
    log_.pointer.push_back({sessionTime, std::string(type), x, y, button});
}

void InputEventRecorder::recordKey(Time sessionTime, std::string_view type, std::string key, std::uint32_t modifiers) {
    if (!running_) return;
    std::lock_guard lock(mutex_);
    if (log_.keys.size() >= kMaxKeyEvents) return;
    log_.keys.push_back({sessionTime, std::string(type), std::move(key), modifiers});
}

InputEventLog InputEventRecorder::snapshot() const {
    std::lock_guard lock(mutex_);
    return log_;
}

Status InputEventRecorder::flush() {
    InputEventLog copy = snapshot();
    if (copy.pointer.empty() && copy.keys.empty()) return ok();
    LEC_TRY(fs::ensureDirectory(outputFile_.parent_path()));
    return writeInputEventLog(outputFile_, copy);
}

#if !defined(LECTERN_HAS_NATIVE_INPUT_CAPTURE)
Status startPlatformCapture(InputEventRecorder*) { return ok(); }
void stopPlatformCapture(InputEventRecorder*) {}
#endif

}  // namespace lectern::capture
