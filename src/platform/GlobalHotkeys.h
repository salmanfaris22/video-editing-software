#pragma once

#include "core/Error.h"

#include <functional>
#include <memory>

namespace lectern::platform {

/// Modifier flags. `Command` is ⌘ on macOS and Ctrl elsewhere.
enum HotkeyModifier : unsigned {
    kHotkeyCommand = 1u << 0,
    kHotkeyShift = 1u << 1,
    kHotkeyOption = 1u << 2,  ///< ⌥ / Alt
    kHotkeyControl = 1u << 3, ///< ⌃ on macOS
};

struct HotkeySpec {
    char key = 'R';          ///< 'A'–'Z' or '0'–'9'
    unsigned modifiers = kHotkeyCommand | kHotkeyShift;
};

/// System-wide keyboard shortcuts that work while another application is in
/// front — essential while recording, when the user is working in the app
/// being captured.
class IGlobalHotkeys {
public:
    virtual ~IGlobalHotkeys() = default;
    /// Registers a hotkey. `callback` runs on the main (UI) thread.
    /// ErrorCode::AlreadyExists if another application owns the combination.
    virtual Status add(int id, HotkeySpec spec, std::function<void()> callback) = 0;
    virtual void remove(int id) = 0;
    virtual void removeAll() = 0;
};

/// The platform implementation, or nullptr where none exists yet
/// (Windows: RegisterHotKey + native event filter; Linux: GlobalShortcuts portal).
[[nodiscard]] std::unique_ptr<IGlobalHotkeys> createGlobalHotkeys();

}  // namespace lectern::platform
