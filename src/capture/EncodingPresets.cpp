#include "capture/EncodingPresets.h"

#include <algorithm>
#include <cmath>

namespace lectern::capture {

std::optional<Resolution> resolutionPreset(std::string_view id) {
    if (id == "720p") return Resolution{1280, 720};
    if (id == "1080p") return Resolution{1920, 1080};
    if (id == "1440p") return Resolution{2560, 1440};
    if (id == "2160p" || id == "4k" || id == "4K") return Resolution{3840, 2160};
    return std::nullopt;
}

QualityPreset qualityFromString(std::string_view s) noexcept {
    return s == "standard" ? QualityPreset::Standard : QualityPreset::High;
}

Resolution fitWithin(Resolution source, Resolution bounds) {
    if (source.width <= 0 || source.height <= 0) return {0, 0};
    double scale = 1.0;
    if (bounds.width > 0) scale = std::min(scale, static_cast<double>(bounds.width) / source.width);
    if (bounds.height > 0) scale = std::min(scale, static_cast<double>(bounds.height) / source.height);
    auto even = [](double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); };
    return {even(source.width * scale), even(source.height * scale)};
}

PixelRect letterbox(Resolution content, Resolution frame) {
    if (content.width <= 0 || content.height <= 0 || frame.width < 2 || frame.height < 2) return {};
    const double scale = std::min(static_cast<double>(frame.width) / content.width,
                                  static_cast<double>(frame.height) / content.height);
    auto even = [](double v) { return static_cast<int>(std::lround(v / 2.0)) * 2; };
    const int w = std::clamp(even(content.width * scale), 2, frame.width & ~1);
    const int h = std::clamp(even(content.height * scale), 2, frame.height & ~1);
    return {((frame.width - w) / 2) & ~1, ((frame.height - h) / 2) & ~1, w, h};
}

std::int64_t videoBitrate(TrackRole role, Resolution r, FrameRate fps, QualityPreset quality) {
    // Base: Mbit/s at 30 fps for the given pixel count, by tier
    // (720p: 5, 1080p: 10, 1440p: 16, 2160p: 35 for screens).
    struct Tier {
        int pixels;
        double screen30;
        double camera30;
    };
    static constexpr Tier kTiers[] = {
        {1280 * 720, 5.0, 6.0},
        {1920 * 1080, 10.0, 12.0},
        {2560 * 1440, 16.0, 20.0},
        {3840 * 2160, 35.0, 40.0},
    };
    const int pixels = std::max(1, r.width * r.height);
    double base;
    const bool camera = role == TrackRole::Camera || role == TrackRole::Phone;
    if (pixels <= kTiers[0].pixels) {
        const double ratio = static_cast<double>(pixels) / kTiers[0].pixels;
        base = (camera ? kTiers[0].camera30 : kTiers[0].screen30) * std::max(0.25, ratio);
    } else if (pixels >= kTiers[3].pixels) {
        base = camera ? kTiers[3].camera30 : kTiers[3].screen30;
    } else {
        // Interpolate linearly in pixel count between tiers.
        int i = 0;
        while (pixels > kTiers[i + 1].pixels) ++i;
        const Tier& a = kTiers[i];
        const Tier& b = kTiers[i + 1];
        const double t = static_cast<double>(pixels - a.pixels) / (b.pixels - a.pixels);
        const double va = camera ? a.camera30 : a.screen30;
        const double vb = camera ? b.camera30 : b.screen30;
        base = va + t * (vb - va);
    }
    // High frame rates need less than proportional bitrate (temporal redundancy).
    const double f = fps.isValid() ? fps.toDouble() : 30.0;
    const double fpsFactor = f > 30.0 ? 1.0 + 0.6 * (f - 30.0) / 30.0 : std::max(0.8, f / 30.0);
    const double qualityFactor = quality == QualityPreset::Standard ? 0.6 : 1.0;
    return static_cast<std::int64_t>(base * fpsFactor * qualityFactor * 1'000'000.0);
}

}  // namespace lectern::capture
