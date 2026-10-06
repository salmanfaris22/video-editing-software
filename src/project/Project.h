#pragma once

#include "core/Error.h"
#include "core/Time.h"
#include "core/Uuid.h"
#include "timeline/Timeline.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace lectern::project {

using ProjectId = Id<struct ProjectTag>;
using timeline::MediaId;

enum class MediaKind { Video, Audio, Image };
enum class MediaRole { Screen, Camera, Phone, Microphone, SystemAudio, Imported };

[[nodiscard]] std::string_view toString(MediaKind kind) noexcept;
[[nodiscard]] std::string_view toString(MediaRole role) noexcept;

struct VideoMetadata {
    std::string codec;
    int width = 0;
    int height = 0;
    FrameRate frameRate;
    std::string pixelFormat;
    std::string colorSpace;
    std::string colorRange;
    int rotation = 0;
    bool variableFrameRate = false;
    /// FFmpeg names ("bt709", "bt2020", "smpte432" = Display P3; "arib-std-b67" = HLG,
    /// "smpte2084" = PQ). Empty in projects from before they were recorded: Rec.709.
    std::string colorPrimaries;
    std::string colorTransfer;
    friend bool operator==(const VideoMetadata&, const VideoMetadata&) = default;
};

struct AudioMetadata {
    std::string codec;
    int sampleRate = 0;
    int channels = 0;
    friend bool operator==(const AudioMetadata&, const AudioMetadata&) = default;
};

/// Persisted subset of probe results (independent of media::MediaInfo so the
/// file format does not change when the probe evolves).
struct MediaMetadata {
    std::string container;
    Time start;
    Time duration;
    std::optional<VideoMetadata> video;
    std::optional<AudioMetadata> audio;
    friend bool operator==(const MediaMetadata&, const MediaMetadata&) = default;
};

/// Content fingerprint for relinking moved media (PROJECT_FORMAT.md §5).
struct Fingerprint {
    std::uint64_t size = 0;
    std::string partialHash;  ///< "fnv1a64:<hex>" of the first and last MiB
    friend bool operator==(const Fingerprint&, const Fingerprint&) = default;
};

struct RecordingRef {
    std::string sessionId;
    std::string trackId;
    friend bool operator==(const RecordingRef&, const RecordingRef&) = default;
};

struct MediaSource {
    MediaId id;
    MediaKind kind = MediaKind::Video;
    MediaRole role = MediaRole::Imported;
    std::string name;
    std::string path;  ///< relative to the project folder when inside it
    Fingerprint fingerprint;
    MediaMetadata info;
    std::optional<RecordingRef> recording;
    std::optional<std::string> proxyPath;
    Time syncOffset;  ///< manual/auto sync adjustment applied to all clips of this media
    friend bool operator==(const MediaSource&, const MediaSource&) = default;
};

struct CanvasSettings {
    int width = 1920;
    int height = 1080;
    FrameRate frameRate{30, 1};
    std::string aspect = "16:9";
    std::string backgroundType = "color";
    std::string backgroundColor = "#0E0F13";
    friend bool operator==(const CanvasSettings&, const CanvasSettings&) = default;
};

/// A screen or camera slot placed by hand on the canvas (canvas fractions;
/// may extend past the canvas edges).
struct SlotRect {
    double x = 0;
    double y = 0;
    double w = 1;
    double h = 1;
    friend bool operator==(const SlotRect&, const SlotRect&) = default;
};

/// Hand-placed slots replacing a layout preset's defaults.
struct LayoutCustomization {
    std::optional<SlotRect> screen;
    std::optional<SlotRect> camera;
    [[nodiscard]] bool empty() const noexcept { return !screen && !camera; }
    friend bool operator==(const LayoutCustomization&, const LayoutCustomization&) = default;
};

/// Key of a layout customization: the preset and the canvas aspect
/// ("pip.bottom-right.rounded@16:9"), so each aspect keeps its own arrangement.
[[nodiscard]] std::string layoutKey(std::string_view preset, int canvasWidth, int canvasHeight);

/// Project-wide look (docs/RENDERING_PIPELINE.md §4): how the recorded
/// screen and camera sit on the canvas, and how subtitles are drawn.
/// Lengths are fractions of the canvas height so they survive resizing.
struct StyleSettings {
    std::string backgroundColor2;          ///< gradient end color; empty = solid background
    double screenPadding = 0.0;            ///< inset around the screen layer (0–0.25)
    double screenRadius = 0.0;             ///< screen corner radius (0–0.08)
    double screenShadow = 0.0;             ///< drop shadow strength (0–1)
    std::string cameraShape = "rounded";   ///< "rect", "rounded", "circle" (circle layouts force circle)
    double cameraBorder = 0.0;             ///< border width (0–0.02)
    std::string cameraBorderColor = "#FFFFFF";
    bool cameraMirror = false;
    double subtitleSize = 0.045;           ///< text height
    std::string subtitleColor = "#FFFFFF";
    std::string subtitleBackground = "#B3000000";  ///< #AARRGGBB; empty = none
    double subtitlePosition = 0.88;        ///< vertical center of the subtitle band
    std::map<std::string, LayoutCustomization> layouts;  ///< by layoutKey()
    friend bool operator==(const StyleSettings&, const StyleSettings&) = default;
};

struct RecordingEntry {
    std::string sessionId;
    std::string manifest;  ///< relative path to session.json
    std::string startedAt;
    Time duration;
    std::string state;
    friend bool operator==(const RecordingEntry&, const RecordingEntry&) = default;
};

struct ExportSettings {
    std::string container = "mp4";
    std::string videoCodec = "h264";
    int width = 1920;
    int height = 1080;
    std::optional<FrameRate> frameRate;  ///< nullopt = same as project
    std::string quality = "high";
    std::string audioCodec = "aac";
    int audioBitrate = 192'000;
    friend bool operator==(const ExportSettings&, const ExportSettings&) = default;
};

/// The authoritative editor document (docs/PROJECT_FORMAT.md). Owned by C++;
/// the UI only observes it.
struct Project {
    /// 2: clip transforms are adjustments relative to the layout slot (v1
    /// stored an absolute camera placement), plus `style` and track gain.
    static constexpr int kFormatVersion = 2;

    ProjectId id;
    std::string title;
    std::string description;
    std::string createdAt;
    std::string modifiedAt;
    CanvasSettings canvas;
    StyleSettings style;
    std::vector<MediaSource> media;
    timeline::Timeline timeline;
    std::vector<RecordingEntry> recordings;
    ExportSettings exportSettings;

    [[nodiscard]] static Project createEmpty(std::string title);
    [[nodiscard]] const MediaSource* findMedia(const MediaId& mediaId) const;
    [[nodiscard]] MediaSource* findMedia(const MediaId& mediaId);
    /// Timeline invariants + media references + limits.
    [[nodiscard]] Status validate() const;

    friend bool operator==(const Project&, const Project&) = default;
};

/// size + FNV-1a-64 over the first and last MiB of the file.
[[nodiscard]] Result<Fingerprint> computeFingerprint(const std::filesystem::path& path);

}  // namespace lectern::project
