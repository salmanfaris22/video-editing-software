#pragma once

// Shared helpers for the Windows backends (internal to src/platform/windows).
//
// Verification status: compiled with MinGW-w64 against the Windows headers;
// NOT yet run on Windows (docs/RECORDING_ENGINE.md §12).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <wrl/client.h>

#include "capture/CaptureInterfaces.h"
#include "core/Error.h"

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>

namespace lectern::platform::win {

using Microsoft::WRL::ComPtr;

// COM objects are destroyed by Release() on their concrete type, so the
// interfaces' non-virtual destructors are correct; silence GCC's warning
// around COM implementation classes only.
#if defined(__GNUC__)
#define LECTERN_COM_IMPL_BEGIN _Pragma("GCC diagnostic push") _Pragma("GCC diagnostic ignored \"-Wnon-virtual-dtor\"")
#define LECTERN_COM_IMPL_END _Pragma("GCC diagnostic pop")
#else
#define LECTERN_COM_IMPL_BEGIN
#define LECTERN_COM_IMPL_END
#endif

// Backend factories (one translation unit each).
[[nodiscard]] std::unique_ptr<capture::IAudioCaptureBackend> createAudioBackend();
[[nodiscard]] std::unique_ptr<capture::ICameraCaptureBackend> createCameraBackend();
[[nodiscard]] std::unique_ptr<capture::IScreenCaptureBackend> createScreenBackend();
[[nodiscard]] std::unique_ptr<capture::IPermissionService> createPermissionService();
/// Windows.Graphics.Capture is usable (Windows 10 1903 or later).
[[nodiscard]] bool screenCaptureSupported();

[[nodiscard]] inline Error hrError(HRESULT hr, std::string_view what, ErrorCode code = ErrorCode::DeviceBusy) {
    char hex[24];
    std::snprintf(hex, sizeof hex, " (0x%08lX)", static_cast<unsigned long>(hr));
    if (hr == E_ACCESSDENIED) code = ErrorCode::PermissionDenied;
    return Error(code, std::string(what) + hex, static_cast<std::int64_t>(hr));
}

[[nodiscard]] inline std::string toUtf8(const wchar_t* s) {
    if (!s || !*s) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (n <= 1) return {};
    std::string out(static_cast<std::size_t>(n - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}

[[nodiscard]] inline std::wstring toWide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (n <= 1) return {};
    std::wstring out(static_cast<std::size_t>(n - 1), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

/// Joins the multithreaded COM apartment for the calling thread (balanced in
/// the destructor). Threads already in an STA (e.g. Qt's UI thread) keep it.
class ComApartment {
public:
    ComApartment() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComApartment() {
        if (SUCCEEDED(hr_)) CoUninitialize();
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;
    [[nodiscard]] bool usable() const noexcept { return SUCCEEDED(hr_) || hr_ == RPC_E_CHANGED_MODE; }

private:
    HRESULT hr_;
};

/// WASAPI `u64QPCPosition` and Windows.Graphics.Capture `SystemRelativeTime`
/// are QPC time in 100 ns units — the same timeline as HostClock::now().
[[nodiscard]] constexpr std::int64_t qpc100nsToNs(std::uint64_t value) noexcept {
    return static_cast<std::int64_t>(value) * 100;
}

}  // namespace lectern::platform::win
