// Windows.Graphics.Capture backend for displays, windows and applications
// (docs/RECORDING_ENGINE.md §12), through the raw WinRT ABI (no C++/WinRT
// dependency, builds with MSVC and MinGW-w64).
//
// A free-threaded frame pool signals an event; our MMCSS "Capture" thread
// drains the pool, keeps the newest frame, throttles conversion to the
// recording rate (trailing edge, so the last change before the screen goes
// idle is never lost) and converts on the GPU (WinD3D11.h). Frame times are
// SystemRelativeTime: QPC, the host clock domain.
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinD3D11.h"

#include <avrt.h>
#include <dwmapi.h>
#include <roapi.h>
#include <winstring.h>
#include <windows.graphics.capture.h>
#include <windows.graphics.capture.interop.h>
#include <windows.graphics.directx.direct3d11.h>

#include "capture/EncodingPresets.h"
#include "core/Clock.h"
#include "core/Log.h"
#include "core/Thread.h"

#include <algorithm>
#include <atomic>
#include <cwchar>
#include <filesystem>
#include <future>
#include <map>
#include <thread>
#include <vector>

namespace lectern::platform::win {

using namespace capture;
namespace wgc = ABI::Windows::Graphics::Capture;
namespace wgd = ABI::Windows::Graphics::DirectX;
namespace wf = ABI::Windows::Foundation;

namespace {

constexpr auto kBgra = static_cast<wgd::DirectXPixelFormat>(87);  // B8G8R8A8UIntNormalized
constexpr INT32 kPoolBuffers = 3;

// windows.graphics.directx.direct3d11.interop.h is not shipped with MinGW-w64.
LECTERN_COM_IMPL_BEGIN
struct DxgiInterfaceAccess : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetInterface(REFIID iid, void** object) = 0;
};
LECTERN_COM_IMPL_END
constexpr GUID kIIDDxgiInterfaceAccess = {0xa9b3d012, 0x3df2, 0x4ee3, {0xb8, 0xd1, 0x86, 0x95, 0xf4, 0x57, 0xd3, 0xc1}};
using CreateWinRtDeviceFn = HRESULT(WINAPI*)(IDXGIDevice*, IInspectable**);

/// Non-owning HSTRING over a literal (WindowsCreateStringReference).
class HStringRef {
public:
    explicit HStringRef(const wchar_t* s) {
        if (FAILED(WindowsCreateStringReference(s, static_cast<UINT32>(std::wcslen(s)), &header_, &string_))) {
            string_ = nullptr;
        }
    }
    [[nodiscard]] HSTRING get() const noexcept { return string_; }

private:
    HSTRING_HEADER header_{};
    HSTRING string_ = nullptr;
};

template <class T>
Result<ComPtr<T>> activationFactory(const wchar_t* runtimeClass) {
    ComPtr<T> factory;
    const HRESULT hr = RoGetActivationFactory(HStringRef(runtimeClass).get(), __uuidof(T),
                                              reinterpret_cast<void**>(factory.GetAddressOf()));
    if (FAILED(hr)) return fail(hrError(hr, "WinRT activation factory", ErrorCode::Unsupported));
    return factory;
}

void closeWinRt(IUnknown* object) {
    if (!object) return;
    ComPtr<wf::IClosable> closable;
    if (SUCCEEDED(object->QueryInterface(__uuidof(wf::IClosable), reinterpret_cast<void**>(closable.GetAddressOf())))) {
        closable->Close();
    }
}

DWORD windowsBuild() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    static const DWORD build = [] {
        OSVERSIONINFOW v{};
        v.dwOSVersionInfoSize = sizeof v;
        if (HMODULE ntdll = GetModuleHandleW(L"ntdll.dll")) {
            auto fn = reinterpret_cast<RtlGetVersionFn>(
                reinterpret_cast<void (*)()>(GetProcAddress(ntdll, "RtlGetVersion")));
            if (fn && fn(&v) == 0) return v.dwBuildNumber;
        }
        return DWORD{0};
    }();
    return build;
}

// ---------------------------------------------------------------------------
// Displays

struct Monitor {
    HMONITOR handle = nullptr;
    std::wstring device;  ///< "\\.\DISPLAY1": our display id
    bool primary = false;
};

BOOL CALLBACK collectMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM out) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof info;
    if (GetMonitorInfoW(monitor, &info)) {
        reinterpret_cast<std::vector<Monitor>*>(out)->push_back(
            {monitor, info.szDevice, (info.dwFlags & MONITORINFOF_PRIMARY) != 0});
    }
    return TRUE;
}

std::vector<Monitor> monitors() {
    std::vector<Monitor> out;
    EnumDisplayMonitors(nullptr, nullptr, &collectMonitor, reinterpret_cast<LPARAM>(&out));
    return out;
}

/// GDI device name → monitor friendly name ("DELL U2720Q", "Built-in Display").
std::map<std::wstring, std::string> monitorNames() {
    std::map<std::wstring, std::string> names;
    UINT32 pathCount = 0;
    UINT32 modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) return names;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        return names;
    }
    for (UINT32 i = 0; i < pathCount; ++i) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        DISPLAYCONFIG_TARGET_DEVICE_NAME target{};
        target.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
        target.header.size = sizeof target;
        target.header.adapterId = paths[i].targetInfo.adapterId;
        target.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) {
            continue;
        }
        const auto tech = target.outputTechnology;
        const bool internal = tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL ||
                              tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED ||
                              tech == DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED;
        std::string name = internal ? std::string("Built-in Display") : toUtf8(target.monitorFriendlyDeviceName);
        if (!name.empty()) names[source.viewGdiDeviceName] = std::move(name);
    }
    return names;
}

// ---------------------------------------------------------------------------
// Windows and applications

struct ProcessInfo {
    std::string path;
    std::string name;
};

std::string fileDescription(const std::wstring& path) {
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    if (!size) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, data.data())) return {};
    struct LangCodePage {
        WORD language;
        WORD codePage;
    };
    LangCodePage* translations = nullptr;
    UINT bytes = 0;
    if (!VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation", reinterpret_cast<void**>(&translations), &bytes) ||
        bytes < sizeof(LangCodePage)) {
        return {};
    }
    auto hex4 = [](WORD v) {
        std::wstring s(4, L'0');
        for (int i = 3; i >= 0; --i, v = static_cast<WORD>(v >> 4)) s[static_cast<std::size_t>(i)] = L"0123456789abcdef"[v & 0xF];
        return s;
    };
    const std::wstring key =
        L"\\StringFileInfo\\" + hex4(translations[0].language) + hex4(translations[0].codePage) + L"\\FileDescription";
    wchar_t* value = nullptr;
    UINT length = 0;
    if (VerQueryValueW(data.data(), key.c_str(), reinterpret_cast<void**>(&value), &length) && length > 1) {
        return toUtf8(value);
    }
    return {};
}

ProcessInfo processInfo(DWORD pid, std::map<DWORD, ProcessInfo>& cache) {
    if (auto it = cache.find(pid); it != cache.end()) return it->second;
    ProcessInfo info;
    if (HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
        wchar_t buffer[1024];
        DWORD size = static_cast<DWORD>(std::size(buffer));
        if (QueryFullProcessImageNameW(process, 0, buffer, &size)) {
            info.path = toUtf8(buffer);
            info.name = fileDescription(buffer);
            if (info.name.empty()) info.name = toUtf8(std::filesystem::path(buffer).stem().c_str());
        }
        CloseHandle(process);
    }
    cache[pid] = info;
    return info;
}

std::string windowTitle(HWND hwnd) {
    const int length = GetWindowTextLengthW(hwnd);
    if (length <= 0) return {};
    std::wstring title(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(hwnd, title.data(), length + 1);
    return toUtf8(title.c_str());
}

RECT windowBounds(HWND hwnd) {
    RECT r{};
    if (FAILED(DwmGetWindowAttribute(hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof r))) GetWindowRect(hwnd, &r);
    return r;
}

/// Top-level windows a user would pick (what Alt+Tab shows), minus ours.
bool isCapturableWindow(HWND hwnd) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) return false;
    const auto exStyle = static_cast<DWORD>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    if (exStyle & WS_EX_TOOLWINDOW) return false;
    if (GetWindow(hwnd, GW_OWNER) && !(exStyle & WS_EX_APPWINDOW)) return false;
    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof cloaked)) && cloaked) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() || GetWindowTextLengthW(hwnd) == 0) return false;
    const RECT r = windowBounds(hwnd);
    return r.right - r.left >= 64 && r.bottom - r.top >= 64;
}

BOOL CALLBACK collectWindow(HWND hwnd, LPARAM out) {
    if (isCapturableWindow(hwnd)) reinterpret_cast<std::vector<HWND>*>(out)->push_back(hwnd);
    return TRUE;
}

std::vector<HWND> capturableWindows() {
    std::vector<HWND> out;
    EnumWindows(&collectWindow, reinterpret_cast<LPARAM>(&out));
    return out;
}

std::string windowId(HWND hwnd) { return std::to_string(reinterpret_cast<std::uintptr_t>(hwnd)); }

HWND windowFromId(const std::string& id) {
    try {
        return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(std::stoull(id)));
    } catch (...) {
        return nullptr;
    }
}

// ---------------------------------------------------------------------------

using FrameArrivedHandler = wf::ITypedEventHandler<wgc::Direct3D11CaptureFramePool*, IInspectable*>;

/// FrameArrived delegate: signals an auto-reset event (agile, any thread).
LECTERN_COM_IMPL_BEGIN
class FrameSignal final : public FrameArrivedHandler {
public:
    FrameSignal() : event_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(FrameArrivedHandler)) ||
            IsEqualIID(riid, __uuidof(IAgileObject))) {
            *out = static_cast<FrameArrivedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return refs_.fetch_add(1, std::memory_order_relaxed) + 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG left = refs_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE Invoke(wgc::IDirect3D11CaptureFramePool*, IInspectable*) override {
        SetEvent(event_);
        return S_OK;
    }
    [[nodiscard]] HANDLE event() const noexcept { return event_; }

private:
    virtual ~FrameSignal() {
        if (event_) CloseHandle(event_);
    }
    HANDLE event_;
    std::atomic<ULONG> refs_{1};
};
LECTERN_COM_IMPL_END

Result<ComPtr<wgd::Direct3D11::IDirect3DDevice>> winRtDevice(ID3D11Device* device) {
    HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
    auto create = d3d11 ? reinterpret_cast<CreateWinRtDeviceFn>(reinterpret_cast<void (*)()>(
                              GetProcAddress(d3d11, "CreateDirect3D11DeviceFromDXGIDevice")))
                        : nullptr;
    if (!create) return fail(ErrorCode::Unsupported, "CreateDirect3D11DeviceFromDXGIDevice is unavailable");
    ComPtr<IDXGIDevice> dxgi;
    HRESULT hr = device->QueryInterface(IID_PPV_ARGS(&dxgi));
    ComPtr<IInspectable> inspectable;
    if (SUCCEEDED(hr)) hr = create(dxgi.Get(), &inspectable);
    ComPtr<wgd::Direct3D11::IDirect3DDevice> out;
    if (SUCCEEDED(hr)) {
        hr = inspectable->QueryInterface(__uuidof(wgd::Direct3D11::IDirect3DDevice),
                                         reinterpret_cast<void**>(out.GetAddressOf()));
    }
    if (FAILED(hr)) return fail(hrError(hr, "WinRT Direct3D device"));
    return out;
}

Result<ComPtr<ID3D11Texture2D>> frameTexture(wgc::IDirect3D11CaptureFrame* frame) {
    ComPtr<wgd::Direct3D11::IDirect3DSurface> surface;
    HRESULT hr = frame->get_Surface(&surface);
    ComPtr<DxgiInterfaceAccess> access;
    if (SUCCEEDED(hr)) hr = surface->QueryInterface(kIIDDxgiInterfaceAccess, reinterpret_cast<void**>(access.GetAddressOf()));
    ComPtr<ID3D11Texture2D> texture;
    if (SUCCEEDED(hr)) hr = access->GetInterface(IID_PPV_ARGS(&texture));
    if (FAILED(hr)) return fail(hrError(hr, "capture frame texture"));
    return texture;
}

/// What a ScreenCaptureTarget resolves to: WGC captures one monitor or one window.
struct ResolvedTarget {
    HMONITOR monitor = nullptr;
    HWND window = nullptr;
    std::string name;
};

Result<ResolvedTarget> resolveTarget(const ScreenCaptureTarget& target) {
    ResolvedTarget out;
    switch (target.kind) {
        case ScreenTargetKind::Display: {
            const auto names = monitorNames();
            int index = 0;
            for (const Monitor& m : monitors()) {
                ++index;
                const bool wanted = (target.id.empty() || target.id == "main") ? m.primary : toUtf8(m.device.c_str()) == target.id;
                if (!wanted) continue;
                out.monitor = m.handle;
                const auto it = names.find(m.device);
                out.name = it != names.end() ? it->second : "Display " + std::to_string(index);
                return out;
            }
            return fail(ErrorCode::NotFound, "display " + target.id + " not found");
        }
        case ScreenTargetKind::Window: {
            HWND hwnd = windowFromId(target.id);
            if (!hwnd || !IsWindow(hwnd)) return fail(ErrorCode::NotFound, "window " + target.id + " not found");
            std::map<DWORD, ProcessInfo> cache;
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            out.window = hwnd;
            out.name = processInfo(pid, cache).name + " — " + windowTitle(hwnd);
            return out;
        }
        case ScreenTargetKind::Application: {
            // WGC captures single windows: use the application's largest one.
            std::map<DWORD, ProcessInfo> cache;
            long long bestArea = 0;
            for (HWND hwnd : capturableWindows()) {
                DWORD pid = 0;
                GetWindowThreadProcessId(hwnd, &pid);
                const ProcessInfo info = processInfo(pid, cache);
                if (info.path != target.id && std::to_string(pid) != target.id) continue;
                const RECT r = windowBounds(hwnd);
                const long long area = static_cast<long long>(r.right - r.left) * (r.bottom - r.top);
                if (area > bestArea) {
                    bestArea = area;
                    out.window = hwnd;
                    out.name = info.name;
                }
            }
            if (!out.window) return fail(ErrorCode::NotFound, "application " + target.id + " has no capturable window");
            return out;
        }
    }
    return fail(ErrorCode::InvalidArgument, "unknown capture target");
}

// ===========================================================================

class WgcScreenSource final : public IVideoSource {
public:
    explicit WgcScreenSource(ScreenCaptureConfig config)
        : config_(std::move(config)), stopEvent_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {
        info_.kind = config_.target.kind == ScreenTargetKind::Display  ? SourceKind::Display
                     : config_.target.kind == ScreenTargetKind::Window ? SourceKind::Window
                                                                       : SourceKind::Application;
        info_.deviceId = config_.target.id;
        info_.nominalFrameRate = config_.frameRate;
    }
    ~WgcScreenSource() override {
        stop();
        if (stopEvent_) CloseHandle(stopEvent_);
    }

    Status start(IVideoFrameSink& sink) override {
        if (thread_.joinable()) return fail(ErrorCode::InvalidState, "screen capture already started");
        sink_ = &sink;
        ResetEvent(stopEvent_);
        running_.store(true, std::memory_order_release);
        std::promise<Status> ready;
        auto result = ready.get_future();
        thread_ = std::thread([this, p = std::move(ready)]() mutable { run(std::move(p)); });
        Status st = result.get();
        if (!st) {
            running_.store(false, std::memory_order_release);
            thread_.join();
            return st;
        }
        sink.onSourceEvent({SourceEvent::Type::Started, info_.name});
        return ok();
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        SetEvent(stopEvent_);
        if (thread_.joinable()) thread_.join();
        if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, info_.name});
    }

    [[nodiscard]] VideoSourceInfo info() const override { return info_; }
    [[nodiscard]] std::uint64_t droppedBySource() const override { return dropped_.load(std::memory_order_relaxed); }

private:
    void run(std::promise<Status> ready) {
        setCurrentThreadName("lectern.wgc");
        ComApartment com;
        DWORD taskIndex = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);
        if (auto st = setup(); !st) {
            teardown();
            ready.set_value(std::move(st));
        } else {
            ready.set_value(ok());
            loop();
            teardown();
        }
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    }

    Status setup() {
        auto target = resolveTarget(config_.target);
        if (!target) return fail(std::move(target).error());
        info_.name = target->name;
        window_ = target->window;

        auto device = createCaptureDevice();
        if (!device) return fail(std::move(device).error());
        device_ = std::move(*device);
        auto rtDevice = winRtDevice(device_.Get());
        if (!rtDevice) return fail(std::move(rtDevice).error());
        rtDevice_ = std::move(*rtDevice);

        auto interop = activationFactory<IGraphicsCaptureItemInterop>(RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureItem);
        if (!interop) return fail(std::move(interop).error());
        HRESULT hr = target->monitor
                         ? (*interop)->CreateForMonitor(target->monitor, __uuidof(wgc::IGraphicsCaptureItem),
                                                        reinterpret_cast<void**>(item_.GetAddressOf()))
                         : (*interop)->CreateForWindow(target->window, __uuidof(wgc::IGraphicsCaptureItem),
                                                       reinterpret_cast<void**>(item_.GetAddressOf()));
        if (FAILED(hr)) return fail(hrError(hr, "create capture item for '" + info_.name + "'"));
        hr = item_->get_Size(&poolSize_);
        if (FAILED(hr) || poolSize_.Width <= 0 || poolSize_.Height <= 0) {
            return fail(hrError(hr, "capture item has no size", ErrorCode::NotFound));
        }

        const Resolution out = fitWithin({poolSize_.Width, poolSize_.Height}, {config_.maxWidth, config_.maxHeight});
        info_.width = out.width;
        info_.height = out.height;
        if (auto gpu = createGpuNv12Converter(device_.Get(), out.width, out.height, config_.frameRate)) {
            converter_ = std::move(*gpu);
        } else {
            LEC_WARN("platform", "GPU color conversion unavailable ({}); using CPU conversion", gpu.error().message());
            converter_ = createBgraReadback(device_.Get());
        }

        auto statics = activationFactory<wgc::IDirect3D11CaptureFramePoolStatics2>(
            RuntimeClass_Windows_Graphics_Capture_Direct3D11CaptureFramePool);
        if (!statics) return fail(std::move(statics).error());
        hr = (*statics)->CreateFreeThreaded(rtDevice_.Get(), kBgra, kPoolBuffers, poolSize_, &pool_);
        if (FAILED(hr)) return fail(hrError(hr, "create capture frame pool"));
        signal_.Attach(new FrameSignal());
        hr = pool_->add_FrameArrived(signal_.Get(), &frameToken_);
        if (FAILED(hr)) return fail(hrError(hr, "subscribe to captured frames"));
        hr = pool_->CreateCaptureSession(item_.Get(), &session_);
        if (FAILED(hr)) return fail(hrError(hr, "create capture session"));

        ComPtr<wgc::IGraphicsCaptureSession2> session2;  // Windows 10 2004+
        if (SUCCEEDED(session_.As(&session2))) session2->put_IsCursorCaptureEnabled(config_.showCursor);
        ComPtr<wgc::IGraphicsCaptureSession3> session3;  // Windows 11: no yellow border
        if (SUCCEEDED(session_.As(&session3))) session3->put_IsBorderRequired(false);

        hr = session_->StartCapture();
        if (FAILED(hr)) return fail(hrError(hr, "start screen capture"));
        LEC_INFO("platform", "Windows.Graphics.Capture '{}' {}x{} → {}x{} @ {} fps via {}", info_.name, poolSize_.Width,
                 poolSize_.Height, out.width, out.height, config_.frameRate.toDouble(), converter_->name());
        return ok();
    }

    void loop() {
        const auto interval = static_cast<std::int64_t>(1e9 / std::max(1.0, config_.frameRate.toDouble()));
        const std::int64_t minSpacing = interval * 3 / 4;  // jitter margin so no output slot is missed
        std::int64_t lastConverted = HostClock::now() - interval;
        ComPtr<wgc::IDirect3D11CaptureFrame> pending;
        const HANDLE events[] = {signal_->event(), stopEvent_};
        bool reportedClosed = false;

        while (running_.load(std::memory_order_acquire)) {
            DWORD timeoutMs = 250;
            if (pending) {
                const std::int64_t wait = lastConverted + minSpacing - HostClock::now();
                timeoutMs = static_cast<DWORD>(std::clamp<std::int64_t>((wait + 999'999) / 1'000'000, 0, 250));
            }
            WaitForMultipleObjects(2, events, FALSE, timeoutMs);
            if (!running_.load(std::memory_order_acquire)) break;

            if (window_ && !reportedClosed && !IsWindow(window_)) {
                reportedClosed = true;
                sink_->onSourceEvent({SourceEvent::Type::Error, "the captured window was closed"});
            }
            // Keep only the newest frame; older ones are superseded, not lost.
            ComPtr<wgc::IDirect3D11CaptureFrame> next;
            while (SUCCEEDED(pool_->TryGetNextFrame(&next)) && next) {
                closeWinRt(pending.Get());
                pending = std::move(next);
            }
            if (!pending || HostClock::now() - lastConverted < minSpacing) continue;
            process(pending.Get());
            closeWinRt(pending.Get());
            pending.Reset();
            lastConverted = HostClock::now();
        }
        closeWinRt(pending.Get());
    }

    void process(wgc::IDirect3D11CaptureFrame* frame) {
        wf::TimeSpan time{};
        ABI::Windows::Graphics::SizeInt32 content{};
        frame->get_SystemRelativeTime(&time);
        frame->get_ContentSize(&content);
        const std::int64_t host = time.Duration > 0 ? qpc100nsToNs(static_cast<std::uint64_t>(time.Duration)) : HostClock::now();

        if (auto texture = frameTexture(frame)) {
            if (auto out = converter_->convert(texture->Get(), content.Width, content.Height)) {
                sink_->onVideoFrame(CapturedVideoFrame{std::move(*out), host, sequence_++});
            } else {
                if (dropped_.fetch_add(1, std::memory_order_relaxed) == 0) {
                    LEC_WARN("platform", "screen frame conversion failed: {}", out.error().message());
                }
            }
        } else {
            dropped_.fetch_add(1, std::memory_order_relaxed);
        }
        // A resized window delivers frames at its new size once the pool is recreated.
        if (content.Width > 0 && content.Height > 0 &&
            (content.Width != poolSize_.Width || content.Height != poolSize_.Height)) {
            poolSize_ = content;
            pool_->Recreate(rtDevice_.Get(), kBgra, kPoolBuffers, poolSize_);
        }
    }

    void teardown() {
        if (pool_ && frameToken_.value) pool_->remove_FrameArrived(frameToken_);
        frameToken_ = {};
        closeWinRt(session_.Get());
        closeWinRt(pool_.Get());
        session_.Reset();
        pool_.Reset();
        item_.Reset();
        signal_.Reset();
        converter_.reset();
        rtDevice_.Reset();
        device_.Reset();
    }

    ScreenCaptureConfig config_;
    VideoSourceInfo info_;
    IVideoFrameSink* sink_ = nullptr;
    HANDLE stopEvent_;
    std::atomic<bool> running_{false};
    std::atomic<std::uint64_t> dropped_{0};
    std::uint64_t sequence_ = 0;
    std::thread thread_;
    HWND window_ = nullptr;
    // Owned by the capture thread between setup() and teardown().
    ComPtr<ID3D11Device> device_;
    ComPtr<wgd::Direct3D11::IDirect3DDevice> rtDevice_;
    ComPtr<wgc::IGraphicsCaptureItem> item_;
    ComPtr<wgc::IDirect3D11CaptureFramePool> pool_;
    ComPtr<wgc::IGraphicsCaptureSession> session_;
    ComPtr<FrameSignal> signal_;
    EventRegistrationToken frameToken_{};
    ABI::Windows::Graphics::SizeInt32 poolSize_{};
    std::unique_ptr<FrameConverter> converter_;
};

class WgcScreenBackend final : public IScreenCaptureBackend {
public:
    [[nodiscard]] std::string name() const override { return "Windows.Graphics.Capture"; }

    Result<ScreenTargets> enumerateTargets() override {
        ScreenTargets targets;
        const auto names = monitorNames();
        int index = 0;
        for (const Monitor& m : monitors()) {
            ++index;
            DisplayInfo d;
            d.id = toUtf8(m.device.c_str());
            const auto it = names.find(m.device);
            d.name = it != names.end() ? it->second : "Display " + std::to_string(index);
            DEVMODEW mode{};
            mode.dmSize = sizeof mode;
            if (EnumDisplaySettingsW(m.device.c_str(), ENUM_CURRENT_SETTINGS, &mode)) {
                d.widthPx = static_cast<int>(mode.dmPelsWidth);
                d.heightPx = static_cast<int>(mode.dmPelsHeight);
                if (mode.dmDisplayFrequency > 1) d.refreshRate = mode.dmDisplayFrequency;
            }
            d.isMain = m.primary;
            targets.displays.push_back(std::move(d));
        }

        std::map<DWORD, ProcessInfo> cache;
        std::vector<DWORD> appPids;
        for (HWND hwnd : capturableWindows()) {
            DWORD pid = 0;
            GetWindowThreadProcessId(hwnd, &pid);
            const ProcessInfo process = processInfo(pid, cache);
            const RECT r = windowBounds(hwnd);
            WindowInfo w;
            w.id = windowId(hwnd);
            w.title = windowTitle(hwnd);
            w.applicationName = process.name;
            w.applicationId = process.path;
            w.widthPx = r.right - r.left;
            w.heightPx = r.bottom - r.top;
            targets.windows.push_back(std::move(w));
            if (std::find(appPids.begin(), appPids.end(), pid) == appPids.end()) {
                appPids.push_back(pid);
                if (!process.path.empty()) targets.applications.push_back({process.path, process.name, pid});
            }
        }
        return targets;
    }

    Result<std::unique_ptr<IVideoSource>> createSource(const ScreenCaptureConfig& config) override {
        return std::unique_ptr<IVideoSource>(std::make_unique<WgcScreenSource>(config));
    }
};

}  // namespace

bool screenCaptureSupported() {
    static const bool supported = [] {
        if (windowsBuild() < 18362) return false;  // capture-item interop needs Windows 10 1903
        ComApartment com;
        auto statics = activationFactory<wgc::IGraphicsCaptureSessionStatics>(
            RuntimeClass_Windows_Graphics_Capture_GraphicsCaptureSession);
        boolean isSupported = false;
        return statics && SUCCEEDED((*statics)->IsSupported(&isSupported)) && isSupported;
    }();
    return supported;
}

std::unique_ptr<IScreenCaptureBackend> createScreenBackend() { return std::make_unique<WgcScreenBackend>(); }

}  // namespace lectern::platform::win
