#pragma once

#include "audio/AudioOutput.h"

#include <memory>

namespace lectern::platform {

/// The system's default output device (CoreAudio on macOS, WASAPI on
/// Windows); a silent real-time clock where no backend exists.
[[nodiscard]] std::unique_ptr<audio::IAudioOutput> createAudioOutput();

}  // namespace lectern::platform
