#pragma once

// Installs the platform's person segmentation (platform/PersonSegmentation.h)
// as the compositor's segmenter for the camera background blur.
// Header-only on purpose: the editor library stays independent of the
// platform layer; executables linking both (the app, lectern-export) call it.

#include "editor/Compositor.h"
#include "platform/PersonSegmentation.h"

#include <cstring>

namespace lectern::editor {

inline void installPlatformSegmenter() {
    if (!platform::personSegmentationAvailable()) return;
    setPersonSegmenter([](const QImage& image) -> QImage {
        const QImage src = image.format() == QImage::Format_RGB32 ? image : image.convertToFormat(QImage::Format_RGB32);
        auto mask = platform::segmentPeople(src.constBits(), src.width(), src.height(), static_cast<int>(src.bytesPerLine()));
        if (!mask || mask->width <= 0 || mask->height <= 0) return {};
        QImage out(mask->width, mask->height, QImage::Format_Grayscale8);
        for (int y = 0; y < mask->height; ++y) {
            std::memcpy(out.scanLine(y), mask->alpha.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(mask->width),
                        static_cast<std::size_t>(mask->width));
        }
        return out;
    });
}

}  // namespace lectern::editor
