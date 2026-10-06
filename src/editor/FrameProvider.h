#pragma once

#include "editor/RenderPlan.h"
#include "media/VideoReader.h"

#include <QImage>
#include <QSizeF>

#include <filesystem>
#include <map>
#include <memory>

namespace lectern::editor {

/// Decoded media for the compositor: one reader per media source, frames
/// converted to 32-bit RGB at the size a layer needs (never above the
/// source) using the source's YUV matrix and range. One thread per provider.
class FrameProvider {
public:
    explicit FrameProvider(std::filesystem::path projectDir, bool hardwareDecode = true);
    ~FrameProvider();
    FrameProvider(const FrameProvider&) = delete;
    FrameProvider& operator=(const FrameProvider&) = delete;

    void setProject(std::shared_ptr<const project::Project> project);
    /// The image for `layer` drawn into a box of `boxPixels`; null when the
    /// media is missing or cannot be decoded.
    QImage image(const VisualLayer& layer, QSizeF boxPixels);
    /// Closes all readers (they reopen on demand).
    void clear();

private:
    struct Entry;
    std::filesystem::path dir_;
    bool hardware_;
    std::shared_ptr<const project::Project> project_;
    std::map<project::MediaId, std::unique_ptr<Entry>> entries_;
    std::uint64_t calls_ = 0;
};

/// Converts a decoded frame to Format_RGB32 at `size` (BT.601/709/2020 and
/// limited/full range honored). Null image on failure.
/// `deep`: 10 bits per channel (QImage::Format_BGR30) for HDR / wide-gamut sources.
[[nodiscard]] QImage frameToImage(const AVFrame* frame, QSize size, media::SwsContextPtr& sws, bool deep = false);

}  // namespace lectern::editor
