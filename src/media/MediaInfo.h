#pragma once

#include "core/Time.h"

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lectern::media {

// Plain data describing a media file. Deliberately free of FFmpeg types so
// any layer can consume probe results.

enum class StreamType { Video, Audio, Subtitle, Data, Attachment, Unknown };
enum class MediaKind { Video, Audio, Image, Unknown };

struct VideoStreamInfo {
    int width = 0;
    int height = 0;
    Rational sampleAspectRatio{1, 1};
    FrameRate averageFrameRate;
    FrameRate realFrameRate;  ///< lowest framerate with which all timestamps can be represented
    bool variableFrameRate = false;
    int rotationDegrees = 0;  ///< clockwise display rotation: 0, 90, 180, 270
    std::string pixelFormat;
    int bitDepth = 8;
    bool hasAlpha = false;
    std::string colorSpace;
    std::string colorPrimaries;
    std::string colorTransfer;
    std::string colorRange;
    bool hdr = false;
    std::int64_t frameCount = 0;  ///< 0 if unknown
};

struct AudioStreamInfo {
    int sampleRate = 0;
    int channels = 0;
    std::string channelLayout;
    std::string sampleFormat;
    int bitDepth = 0;
};

struct StreamInfo {
    int index = -1;
    StreamType type = StreamType::Unknown;
    std::string codec;
    std::string codecLongName;
    std::string profile;
    std::int64_t bitRate = 0;
    Time start;
    Time duration;
    std::string language;
    bool isDefault = false;
    bool isAttachedPicture = false;  ///< cover art, not a real video stream
    std::optional<VideoStreamInfo> video;
    std::optional<AudioStreamInfo> audio;
};

struct MediaInfo {
    std::string path;
    std::string container;
    std::string containerLongName;
    MediaKind kind = MediaKind::Unknown;
    Time start;
    Time duration;
    std::int64_t bitRate = 0;
    std::uint64_t fileSize = 0;
    std::vector<StreamInfo> streams;
    std::map<std::string, std::string> tags;
    int bestVideoStream = -1;
    int bestAudioStream = -1;

    [[nodiscard]] const StreamInfo* video() const {
        return bestVideoStream >= 0 ? &streams[static_cast<std::size_t>(bestVideoStream)] : nullptr;
    }
    [[nodiscard]] const StreamInfo* audio() const {
        return bestAudioStream >= 0 ? &streams[static_cast<std::size_t>(bestAudioStream)] : nullptr;
    }
};

[[nodiscard]] std::string_view toString(StreamType type) noexcept;
[[nodiscard]] std::string_view toString(MediaKind kind) noexcept;

}  // namespace lectern::media
