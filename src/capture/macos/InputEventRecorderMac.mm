#include "capture/InputEventRecorder.h"
#include "core/Log.h"

#import <CoreGraphics/CoreGraphics.h>

#include <atomic>
#include <string>
#include <thread>

namespace lectern::capture {

namespace {

InputEventRecorder* gRecorder = nullptr;
CFMachPortRef gTap = nullptr;
CFRunLoopSourceRef gSource = nullptr;
std::thread gThread;
std::atomic<bool> gRunning{false};

double normalizeX(CGFloat x) {
    const CGRect bounds = CGDisplayBounds(CGMainDisplayID());
    if (bounds.size.width <= 0) return 0;
    return (x - bounds.origin.x) / bounds.size.width;
}

double normalizeY(CGFloat y) {
    const CGRect bounds = CGDisplayBounds(CGMainDisplayID());
    if (bounds.size.height <= 0) return 0;
    return (y - bounds.origin.y) / bounds.size.height;
}

CGEventRef tapCallback(CGEventTapProxy, CGEventType type, CGEventRef event, void*) {
    if (!gRecorder || !gRunning.load()) return event;
    const std::int64_t hostNs = gRecorder->sessionClock().clock().nowNs();
    const CGPoint loc = CGEventGetLocation(event);
    switch (type) {
        case kCGEventMouseMoved:
        case kCGEventLeftMouseDragged:
        case kCGEventRightMouseDragged:
        case kCGEventOtherMouseDragged:
            gRecorder->ingestPointer(hostNs, "move", normalizeX(loc.x), normalizeY(loc.y), 0);
            break;
        case kCGEventLeftMouseDown:
            gRecorder->ingestPointer(hostNs, "down", normalizeX(loc.x), normalizeY(loc.y), 0);
            break;
        case kCGEventLeftMouseUp:
            gRecorder->ingestPointer(hostNs, "up", normalizeX(loc.x), normalizeY(loc.y), 0);
            break;
        case kCGEventRightMouseDown:
            gRecorder->ingestPointer(hostNs, "down", normalizeX(loc.x), normalizeY(loc.y), 1);
            break;
        case kCGEventRightMouseUp:
            gRecorder->ingestPointer(hostNs, "up", normalizeX(loc.x), normalizeY(loc.y), 1);
            break;
        case kCGEventKeyDown:
        case kCGEventKeyUp: {
            UniChar chars[8];
            UniCharCount len = 0;
            CGEventKeyboardGetUnicodeString(event, 8, &len, chars);
            std::string key;
            if (len == 1 && chars[0] >= 32 && chars[0] < 127) {
                key = static_cast<char>(chars[0]);
            } else {
                key = "key" +
                      std::to_string(CGEventGetIntegerValueField(event, kCGKeyboardEventKeycode));
            }
            gRecorder->ingestKey(hostNs, type == kCGEventKeyDown ? "down" : "up", std::move(key),
                                 static_cast<std::uint32_t>(CGEventGetFlags(event)));
            break;
        }
        default:
            break;
    }
    return event;
}

void runLoopThread() {
    @autoreleasepool {
        gTap = CGEventTapCreate(kCGSessionEventTap, kCGHeadInsertEventTap, kCGEventTapOptionListenOnly,
                                CGEventMaskBit(kCGEventMouseMoved) | CGEventMaskBit(kCGEventLeftMouseDragged) |
                                    CGEventMaskBit(kCGEventRightMouseDragged) | CGEventMaskBit(kCGEventLeftMouseDown) |
                                    CGEventMaskBit(kCGEventLeftMouseUp) | CGEventMaskBit(kCGEventRightMouseDown) |
                                    CGEventMaskBit(kCGEventRightMouseUp) | CGEventMaskBit(kCGEventKeyDown) |
                                    CGEventMaskBit(kCGEventKeyUp),
                                tapCallback, nullptr);
        if (!gTap) {
            LEC_WARN("capture", "input capture: event tap unavailable (grant Accessibility for Lectern)");
            gRunning = false;
            return;
        }
        gSource = CFMachPortCreateRunLoopSource(kCFAllocatorDefault, gTap, 0);
        CFRunLoopAddSource(CFRunLoopGetCurrent(), gSource, kCFRunLoopCommonModes);
        CGEventTapEnable(gTap, true);
        while (gRunning.load()) CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.25, true);
        CGEventTapEnable(gTap, false);
        if (gSource) {
            CFRunLoopRemoveSource(CFRunLoopGetCurrent(), gSource, kCFRunLoopCommonModes);
            CFRelease(gSource);
            gSource = nullptr;
        }
        if (gTap) {
            CFRelease(gTap);
            gTap = nullptr;
        }
    }
}

}  // namespace

Status startPlatformCapture(InputEventRecorder* recorder) {
    if (!recorder) return fail(ErrorCode::InvalidArgument, "recorder required");
    gRecorder = recorder;
    gRunning = true;
    gThread = std::thread(runLoopThread);
    return ok();
}

void stopPlatformCapture(InputEventRecorder* recorder) {
    (void)recorder;
    gRunning = false;
    if (gThread.joinable()) gThread.join();
    gRecorder = nullptr;
}

}  // namespace lectern::capture
