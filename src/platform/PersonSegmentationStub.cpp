#include "platform/PersonSegmentation.h"

namespace lectern::platform {

bool personSegmentationAvailable() { return false; }

Result<PersonMask> segmentPeople(const std::uint8_t*, int, int, int) {
    return fail(ErrorCode::Unsupported, "person segmentation is not available on this system");
}

}  // namespace lectern::platform
