#pragma once

// Separating people from their background in camera pictures (for the
// editor's camera background blur). macOS: Apple Vision's person
// segmentation (macOS 12+). Elsewhere: not available yet.

#include "core/Error.h"

#include <cstdint>
#include <vector>

namespace lectern::platform {

/// 8-bit mask: 255 where there is a person, 0 behind them.
struct PersonMask {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> alpha;  ///< width × height, row-major
};

/// Whether this system can segment people.
[[nodiscard]] bool personSegmentationAvailable();

/// Segments the people in a 32-bit BGRA image (the memory layout of a
/// little-endian QImage::Format_RGB32/ARGB32). Calls on one thread reuse a
/// sequence handler, so masks of consecutive video frames are stable.
[[nodiscard]] Result<PersonMask> segmentPeople(const std::uint8_t* bgra, int width, int height, int bytesPerRow);

}  // namespace lectern::platform
