
#include "platform/macos/MacInternal.h"  // must be first (see header)

#import <AVFoundation/AVFoundation.h>
#import <CoreGraphics/CoreGraphics.h>

namespace lectern::platform::mac {

namespace {

capture::PermissionStatus fromAV(AVAuthorizationStatus s) {
    switch (s) {
        case AVAuthorizationStatusAuthorized: return capture::PermissionStatus::Granted;
        case AVAuthorizationStatusDenied: return capture::PermissionStatus::Denied;
        case AVAuthorizationStatusRestricted: return capture::PermissionStatus::Restricted;
        case AVAuthorizationStatusNotDetermined: return capture::PermissionStatus::NotDetermined;
    }
    return capture::PermissionStatus::NotDetermined;
}

/// TCC permissions: Screen Recording (also covers ScreenCaptureKit audio),
/// Camera, Microphone. Status queries never show a prompt; request() may.
class MacPermissionService final : public capture::IPermissionService {
public:
    [[nodiscard]] capture::PermissionStatus status(capture::PermissionKind kind) const override {
        switch (kind) {
            case capture::PermissionKind::ScreenCapture:
            case capture::PermissionKind::SystemAudio:
                // macOS cannot distinguish "denied" from "not asked" here.
                return CGPreflightScreenCaptureAccess() ? capture::PermissionStatus::Granted
                                                        : capture::PermissionStatus::NotDetermined;
            case capture::PermissionKind::Camera:
                return fromAV([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo]);
            case capture::PermissionKind::Microphone:
                return fromAV([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio]);
        }
        return capture::PermissionStatus::NotApplicable;
    }

    void request(capture::PermissionKind kind, std::function<void(capture::PermissionStatus)> done) override {
        switch (kind) {
            case capture::PermissionKind::ScreenCapture:
            case capture::PermissionKind::SystemAudio: {
                // Shows the system prompt once; afterwards the user must enable
                // the app in System Settings and relaunch it.
                const bool granted = CGRequestScreenCaptureAccess();
                if (done) done(granted ? capture::PermissionStatus::Granted : capture::PermissionStatus::Denied);
                return;
            }
            case capture::PermissionKind::Camera:
            case capture::PermissionKind::Microphone: {
                AVMediaType type = kind == capture::PermissionKind::Camera ? AVMediaTypeVideo : AVMediaTypeAudio;
                auto callback = std::move(done);
                [AVCaptureDevice requestAccessForMediaType:type
                                         completionHandler:^(BOOL granted) {
                                           if (callback) {
                                               callback(granted ? capture::PermissionStatus::Granted
                                                                : capture::PermissionStatus::Denied);
                                           }
                                         }];
                return;
            }
        }
    }
};

}  // namespace

std::unique_ptr<capture::IPermissionService> createPermissionService() {
    return std::make_unique<MacPermissionService>();
}

}  // namespace lectern::platform::mac
