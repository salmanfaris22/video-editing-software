#pragma once

#include "capture/CaptureTypes.h"
#include "core/Time.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace lectern::capture {

struct Resolution {
    int width = 0;
    int height = 0;
    friend constexpr bool operator==(const Resolution&, const Resolution&) = default;
};

enum class QualityPreset { Standard, High };

/// "720p" → 1280×720, "1080p", "1440p", "2160p"/"4k"; "native" → nullopt.
[[nodiscard]] std::optional<Resolution> resolutionPreset(std::string_view id);
[[nodiscard]] QualityPreset qualityFromString(std::string_view s) noexcept;

/// Largest even-sized resolution that fits `bounds` with the source aspect
/// ratio, never upscaling. bounds of 0 mean "no limit".
[[nodiscard]] Resolution fitWithin(Resolution source, Resolution bounds);

struct PixelRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    friend constexpr bool operator==(const PixelRect&, const PixelRect&) = default;
};

/// The largest rectangle with `content`'s aspect ratio centered in `frame`
/// (letterbox/pillarbox), on even coordinates for 4:2:0 chroma. Scales up or
/// down; empty for empty inputs.
[[nodiscard]] PixelRect letterbox(Resolution content, Resolution frame);

/// Recording-master bitrates (docs/RECORDING_ENGINE.md §5.4).
[[nodiscard]] std::int64_t videoBitrate(TrackRole role, Resolution resolution, FrameRate fps, QualityPreset quality);

}  // namespace lectern::capture
