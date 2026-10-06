#pragma once

#include "media/VideoEncoder.h"

#include <string>
#include <vector>

namespace lectern::media {

struct EncoderCapability {
    std::string name;
    VideoCodec codec = VideoCodec::H264;
    bool hardware = false;
    bool compiledIn = false;  ///< present in this FFmpeg build
    bool verified = false;    ///< actually opened and encoded a test frame
    std::string error;
};

struct HardwareCapabilities {
    std::vector<EncoderCapability> encoders;
    std::vector<std::string> hwDeviceTypes;  ///< usable hwaccel device types (decode)

    /// Opens every candidate encoder with a tiny test configuration. An
    /// encoder that is compiled in can still fail at runtime (no GPU, old
    /// driver), so only `verified` entries are trusted. Takes ~0.1–0.5 s;
    /// run once on a background thread at startup.
    [[nodiscard]] static HardwareCapabilities probe(bool verify = true);

    [[nodiscard]] bool hasHardwareEncoder(VideoCodec codec) const;
    [[nodiscard]] std::string describe() const;
};

}  // namespace lectern::media
