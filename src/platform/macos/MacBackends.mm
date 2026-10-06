#include "platform/macos/MacInternal.h"  // must be first (see header)

#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>

#include "platform/PlatformBackends.h"

namespace lectern::platform {

capture::CaptureBackends createCaptureBackends() {
    capture::CaptureBackends b;
    b.screen = mac::createScreenBackend();
    b.camera = mac::createCameraBackend();
    b.audio = mac::createAudioBackend();
    b.permissions = mac::createPermissionService();
    return b;
}

PlatformInfo platformInfo() {
    PlatformInfo info;
    @autoreleasepool {
        NSString* version = NSProcessInfo.processInfo.operatingSystemVersionString;
        info.os = "macOS " + std::string(version ? version.UTF8String : "");
    }
    info.nativeScreen = true;
    info.nativeCamera = true;
    info.nativeAudio = true;
    info.nativeSystemAudio = true;  // ScreenCaptureKit audio (macOS 13+)
    return info;
}

void initializePlatform() {
    // Establishes the WindowServer connection that ScreenCaptureKit needs,
    // also in command-line tools that never create an NSApplication.
    (void)CGMainDisplayID();
}

// ScreenCaptureKit filters exclude the whole application instead.
void excludeWindowFromCapture(std::uintptr_t) {}

}  // namespace lectern::platform
