// System-wide hotkeys via RegisterHotKey on a message-only window.
//
// The window lives on the thread that creates the object (the UI thread);
// that thread's event loop (Qt's dispatcher pumps all thread messages)
// delivers WM_HOTKEY to it, so callbacks run on the UI thread as the
// interface promises. No hooks, no elevated rights.
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinCommon.h"

#include "platform/GlobalHotkeys.h"

#include <map>

namespace lectern::platform {

namespace {

constexpr const wchar_t* kWindowClass = L"LecternGlobalHotkeys";

UINT virtualKey(char key) {
    if (key >= 'A' && key <= 'Z') return static_cast<UINT>(key);
    if (key >= 'a' && key <= 'z') return static_cast<UINT>(key - 'a' + 'A');
    if (key >= '0' && key <= '9') return static_cast<UINT>(key);
    return 0;
}

/// Qt's cross-platform convention: ⌘ (Command) ↔ Ctrl, macOS Control ↔ Meta (the Windows key).
UINT modifierFlags(unsigned modifiers) {
    UINT flags = MOD_NOREPEAT;
    if (modifiers & kHotkeyCommand) flags |= MOD_CONTROL;
    if (modifiers & kHotkeyShift) flags |= MOD_SHIFT;
    if (modifiers & kHotkeyOption) flags |= MOD_ALT;
    if (modifiers & kHotkeyControl) flags |= MOD_WIN;
    return flags;
}

class WinGlobalHotkeys final : public IGlobalHotkeys {
public:
    WinGlobalHotkeys() {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof wc;
        wc.lpfnWndProc = &WinGlobalHotkeys::windowProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = kWindowClass;
        RegisterClassExW(&wc);  // fails harmlessly when already registered
        hwnd_ = CreateWindowExW(0, kWindowClass, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (hwnd_) SetWindowLongPtrW(hwnd_, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
    }

    ~WinGlobalHotkeys() override {
        removeAll();
        if (hwnd_) DestroyWindow(hwnd_);
    }

    WinGlobalHotkeys(const WinGlobalHotkeys&) = delete;
    WinGlobalHotkeys& operator=(const WinGlobalHotkeys&) = delete;

    [[nodiscard]] bool valid() const noexcept { return hwnd_ != nullptr; }

    Status add(int id, HotkeySpec spec, std::function<void()> callback) override {
        const UINT vk = virtualKey(spec.key);
        if (!vk) return fail(ErrorCode::InvalidArgument, std::string("unsupported hotkey key '") + spec.key + "'");
        if (id < 0 || id > 0xBFFF) return fail(ErrorCode::InvalidArgument, "hotkey id out of range");
        remove(id);  // re-adding an id replaces it
        if (!RegisterHotKey(hwnd_, id, modifierFlags(spec.modifiers), vk)) {
            const DWORD error = GetLastError();
            if (error == ERROR_HOTKEY_ALREADY_REGISTERED) {
                return fail(ErrorCode::AlreadyExists, "the shortcut is already used by another application");
            }
            return fail(ErrorCode::Unknown, "RegisterHotKey failed (" + std::to_string(error) + ")");
        }
        callbacks_[id] = std::move(callback);
        return ok();
    }

    void remove(int id) override {
        if (callbacks_.erase(id)) UnregisterHotKey(hwnd_, id);
    }

    void removeAll() override {
        for (const auto& entry : callbacks_) UnregisterHotKey(hwnd_, entry.first);
        callbacks_.clear();
    }

private:
    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_HOTKEY) {
            if (auto* self = reinterpret_cast<WinGlobalHotkeys*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA))) {
                self->fire(static_cast<int>(wParam));
            }
            return 0;
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void fire(int id) {
        const auto it = callbacks_.find(id);
        if (it == callbacks_.end()) return;
        const std::function<void()> callback = it->second;  // a callback may remove itself
        if (callback) callback();
    }

    HWND hwnd_ = nullptr;
    std::map<int, std::function<void()>> callbacks_;
};

}  // namespace

std::unique_ptr<IGlobalHotkeys> createGlobalHotkeys() {
    auto hotkeys = std::make_unique<WinGlobalHotkeys>();
    if (!hotkeys->valid()) return nullptr;
    return hotkeys;
}

}  // namespace lectern::platform
