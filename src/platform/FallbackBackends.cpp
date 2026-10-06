// Platforms without native capture backends (Linux for now, or any build
// with LECTERN_NATIVE_CAPTURE=OFF): use the synthetic backends so the engine,
// UI and tests run everywhere. See docs/RECORDING_ENGINE.md §12.

#include "platform/AudioOutput.h"
#include "platform/GlobalHotkeys.h"
#include "platform/PlatformBackends.h"

#include "capture/synthetic/SyntheticSources.h"
#include "core/Log.h"

namespace lectern::platform {

capture::CaptureBackends createCaptureBackends() {
    LEC_WARN("platform", "native capture backends are not implemented on this OS yet; using synthetic sources");
    return capture::makeSyntheticBackends();
}

PlatformInfo platformInfo() {
#if defined(_WIN32)
    return PlatformInfo{"Windows", false, false, false, false};
#else
    return PlatformInfo{"Linux", false, false, false, false};
#endif
}

void initializePlatform() {}

void excludeWindowFromCapture(std::uintptr_t) {}

std::unique_ptr<IGlobalHotkeys> createGlobalHotkeys() { return nullptr; }

std::unique_ptr<audio::IAudioOutput> createAudioOutput() { return audio::makeNullAudioOutput(); }

}  // namespace lectern::platform
