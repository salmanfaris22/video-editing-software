// Windows backend set, permissions and process setup.
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinCommon.h"

#include <mfapi.h>
#include <shellapi.h>

#include "capture/synthetic/SyntheticSources.h"
#include "core/Log.h"
#include "platform/PlatformBackends.h"

#include <mutex>

namespace lectern::platform {

namespace win {
namespace {

using capture::PermissionKind;
using capture::PermissionStatus;

/// Windows privacy switches ("Let desktop apps access your camera"):
/// HKLM is the device-wide policy, HKCU the user's global and desktop-app
/// (NonPackaged) switches. Absent values mean allowed.
std::wstring consentValue(HKEY root, const std::wstring& key) {
    wchar_t value[32] = {};
    DWORD size = sizeof value;
    if (RegGetValueW(root, key.c_str(), L"Value", RRF_RT_REG_SZ, nullptr, value, &size) != ERROR_SUCCESS) return {};
    return value;
}

PermissionStatus consentStatus(const wchar_t* capability) {
    const std::wstring key =
        std::wstring(L"Software\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\") + capability;
    if (consentValue(HKEY_LOCAL_MACHINE, key) == L"Deny") return PermissionStatus::Restricted;
    if (consentValue(HKEY_CURRENT_USER, key) == L"Deny") return PermissionStatus::Denied;
    if (consentValue(HKEY_CURRENT_USER, key + L"\\NonPackaged") == L"Deny") return PermissionStatus::Denied;
    return PermissionStatus::Granted;
}

class WinPermissionService final : public capture::IPermissionService {
public:
    [[nodiscard]] PermissionStatus status(PermissionKind kind) const override {
        switch (kind) {
            case PermissionKind::Camera: return consentStatus(L"webcam");
            case PermissionKind::Microphone: return consentStatus(L"microphone");
            case PermissionKind::ScreenCapture:
            case PermissionKind::SystemAudio: return PermissionStatus::Granted;  // no consent model for desktop apps
        }
        return PermissionStatus::NotApplicable;
    }

    void request(PermissionKind kind, std::function<void(PermissionStatus)> done) override {
        // Windows has no runtime prompt for desktop apps; when access is off,
        // open the matching Settings page so the user can turn it on.
        const PermissionStatus current = status(kind);
        if (current == PermissionStatus::Denied) {
            const wchar_t* page = kind == PermissionKind::Camera ? L"ms-settings:privacy-webcam" : L"ms-settings:privacy-microphone";
            ShellExecuteW(nullptr, L"open", page, nullptr, nullptr, SW_SHOWNORMAL);
        }
        if (done) done(current);
    }
};

std::string windowsVersion() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW v{};
    v.dwOSVersionInfoSize = sizeof v;
    if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void (*)()>(GetProcAddress(ntdll, "RtlGetVersion")));
        if (fn && fn(&v) == 0) {
            const char* family = v.dwMajorVersion == 10 && v.dwBuildNumber >= 22000 ? "Windows 11" : "Windows 10";
            if (v.dwMajorVersion < 10) family = "Windows";
            return std::string(family) + " (build " + std::to_string(v.dwBuildNumber) + ")";
        }
    }
    return "Windows";
}

}  // namespace

std::unique_ptr<capture::IPermissionService> createPermissionService() { return std::make_unique<WinPermissionService>(); }

}  // namespace win

capture::CaptureBackends createCaptureBackends() {
    capture::CaptureBackends b;
    if (win::screenCaptureSupported()) {
        b.screen = win::createScreenBackend();
    } else {
        LEC_WARN("platform", "Windows.Graphics.Capture needs Windows 10 1903 or later; screen capture is simulated");
        b.screen = capture::makeSyntheticBackends().screen;
    }
    b.camera = win::createCameraBackend();
    b.audio = win::createAudioBackend();
    b.permissions = win::createPermissionService();
    return b;
}

PlatformInfo platformInfo() {
    PlatformInfo info;
    info.os = win::windowsVersion();
    info.nativeScreen = win::screenCaptureSupported();
    info.nativeCamera = true;
    info.nativeAudio = true;
    info.nativeSystemAudio = true;  // WASAPI loopback
    return info;
}

void initializePlatform() {
    static std::once_flag once;
    std::call_once(once, [] {
        // Physical pixels for window/monitor geometry in the CLI tools (the Qt
        // app has already chosen the same mode; the call then fails harmlessly).
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        // Keep the COM multithreaded apartment alive for objects created on
        // short-lived worker threads (Media Foundation, WASAPI enumeration).
        CO_MTA_USAGE_COOKIE cookie{};
        if (FAILED(CoIncrementMTAUsage(&cookie))) LEC_WARN("platform", "CoIncrementMTAUsage failed");
        if (const HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL); FAILED(hr)) {
            LEC_ERROR("platform", "Media Foundation is unavailable: {}", win::hrError(hr, "MFStartup").message());
        }
    });
}

void excludeWindowFromCapture(std::uintptr_t nativeWindow) {
    // Windows 10 2004+: the window is left out of every screen capture.
    if (nativeWindow && !SetWindowDisplayAffinity(reinterpret_cast<HWND>(nativeWindow), WDA_EXCLUDEFROMCAPTURE)) {
        LEC_DEBUG("platform", "SetWindowDisplayAffinity failed ({})", GetLastError());
    }
}

}  // namespace lectern::platform
