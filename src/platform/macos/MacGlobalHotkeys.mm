// System-wide hotkeys via Carbon RegisterEventHotKey. Still the supported way
// to register global shortcuts on macOS; unlike event taps it needs no
// Accessibility permission.

#include "platform/macos/MacInternal.h"  // must be first (see header)

#include <Carbon/Carbon.h>

#include "core/Log.h"
#include "platform/GlobalHotkeys.h"

#include <map>

namespace lectern::platform {

namespace {

constexpr OSType kSignature = 'LECT';

std::optional<UInt32> keyCodeFor(char key) {
    switch (key) {
        case 'A': return kVK_ANSI_A;
        case 'B': return kVK_ANSI_B;
        case 'C': return kVK_ANSI_C;
        case 'D': return kVK_ANSI_D;
        case 'E': return kVK_ANSI_E;
        case 'F': return kVK_ANSI_F;
        case 'G': return kVK_ANSI_G;
        case 'H': return kVK_ANSI_H;
        case 'I': return kVK_ANSI_I;
        case 'J': return kVK_ANSI_J;
        case 'K': return kVK_ANSI_K;
        case 'L': return kVK_ANSI_L;
        case 'M': return kVK_ANSI_M;
        case 'N': return kVK_ANSI_N;
        case 'O': return kVK_ANSI_O;
        case 'P': return kVK_ANSI_P;
        case 'Q': return kVK_ANSI_Q;
        case 'R': return kVK_ANSI_R;
        case 'S': return kVK_ANSI_S;
        case 'T': return kVK_ANSI_T;
        case 'U': return kVK_ANSI_U;
        case 'V': return kVK_ANSI_V;
        case 'W': return kVK_ANSI_W;
        case 'X': return kVK_ANSI_X;
        case 'Y': return kVK_ANSI_Y;
        case 'Z': return kVK_ANSI_Z;
        case '0': return kVK_ANSI_0;
        case '1': return kVK_ANSI_1;
        case '2': return kVK_ANSI_2;
        case '3': return kVK_ANSI_3;
        case '4': return kVK_ANSI_4;
        case '5': return kVK_ANSI_5;
        case '6': return kVK_ANSI_6;
        case '7': return kVK_ANSI_7;
        case '8': return kVK_ANSI_8;
        case '9': return kVK_ANSI_9;
        default: return std::nullopt;
    }
}

UInt32 carbonModifiers(unsigned m) {
    UInt32 out = 0;
    if (m & kHotkeyCommand) out |= cmdKey;
    if (m & kHotkeyShift) out |= shiftKey;
    if (m & kHotkeyOption) out |= optionKey;
    if (m & kHotkeyControl) out |= controlKey;
    return out;
}

class MacGlobalHotkeys final : public IGlobalHotkeys {
public:
    ~MacGlobalHotkeys() override {
        removeAll();
        if (handler_) RemoveEventHandler(handler_);
    }

    Status add(int id, HotkeySpec spec, std::function<void()> callback) override {
        const auto keyCode = keyCodeFor(spec.key);
        if (!keyCode) return fail(ErrorCode::InvalidArgument, std::string("unsupported hotkey key '") + spec.key + "'");
        if (!handler_) {
            const EventTypeSpec type{kEventClassKeyboard, kEventHotKeyPressed};
            if (const OSStatus st = InstallApplicationEventHandler(&MacGlobalHotkeys::onHotKey, 1, &type, this, &handler_);
                st != noErr) {
                return fail(ErrorCode::Internal, "install hotkey handler", st);
            }
        }
        remove(id);
        const EventHotKeyID hotKeyId{kSignature, static_cast<UInt32>(id)};
        EventHotKeyRef ref = nullptr;
        const OSStatus st = RegisterEventHotKey(*keyCode, carbonModifiers(spec.modifiers), hotKeyId,
                                                GetApplicationEventTarget(), 0, &ref);
        if (st == eventHotKeyExistsErr) {
            return fail(ErrorCode::AlreadyExists, "the shortcut is already used by another application", st);
        }
        if (st != noErr || !ref) return fail(ErrorCode::Internal, "RegisterEventHotKey", st);
        entries_[id] = Entry{ref, std::move(callback)};
        return ok();
    }

    void remove(int id) override {
        if (auto it = entries_.find(id); it != entries_.end()) {
            UnregisterEventHotKey(it->second.ref);
            entries_.erase(it);
        }
    }

    void removeAll() override {
        for (auto& [id, entry] : entries_) UnregisterEventHotKey(entry.ref);
        entries_.clear();
    }

private:
    struct Entry {
        EventHotKeyRef ref = nullptr;
        std::function<void()> callback;
    };

    // Runs on the main thread (Carbon events are dispatched by the main run loop).
    static OSStatus onHotKey(EventHandlerCallRef, EventRef event, void* userData) {
        EventHotKeyID hotKeyId{};
        if (GetEventParameter(event, kEventParamDirectObject, typeEventHotKeyID, nullptr, sizeof(hotKeyId), nullptr,
                              &hotKeyId) != noErr ||
            hotKeyId.signature != kSignature) {
            return eventNotHandledErr;
        }
        auto* self = static_cast<MacGlobalHotkeys*>(userData);
        if (auto it = self->entries_.find(static_cast<int>(hotKeyId.id)); it != self->entries_.end() && it->second.callback) {
            it->second.callback();
            return noErr;
        }
        return eventNotHandledErr;
    }

    EventHandlerRef handler_ = nullptr;
    std::map<int, Entry> entries_;
};

}  // namespace

std::unique_ptr<IGlobalHotkeys> createGlobalHotkeys() { return std::make_unique<MacGlobalHotkeys>(); }

}  // namespace lectern::platform
