// Person segmentation with Apple Vision (VNGeneratePersonSegmentationRequest,
// macOS 12+), for the camera background blur.

#include "platform/PersonSegmentation.h"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Vision/Vision.h>

#include <cstring>

namespace lectern::platform {

namespace {

/// Vision objects reused by one thread (ARC forbids thread_local Objective-C
/// pointers, a C++ holder is fine).
struct VisionState {
    id sequence = nil;  ///< VNSequenceRequestHandler
    id request = nil;   ///< VNGeneratePersonSegmentationRequest
};

}  // namespace

bool personSegmentationAvailable() {
    if (@available(macOS 12.0, *)) return true;
    return false;
}

Result<PersonMask> segmentPeople(const std::uint8_t* bgra, int width, int height, int bytesPerRow) {
    if (!bgra || width <= 0 || height <= 0 || bytesPerRow < width * 4) {
        return fail(ErrorCode::InvalidArgument, "invalid image for person segmentation");
    }
    if (@available(macOS 12.0, *)) {
        @autoreleasepool {
            // One sequence handler and request per thread: Vision then keeps
            // masks of consecutive frames temporally stable.
            thread_local VisionState state;
            if (!state.sequence) {
                state.sequence = [[VNSequenceRequestHandler alloc] init];
                VNGeneratePersonSegmentationRequest* r = [[VNGeneratePersonSegmentationRequest alloc] init];
                r.qualityLevel = VNGeneratePersonSegmentationRequestQualityLevelBalanced;
                r.outputPixelFormat = kCVPixelFormatType_OneComponent8;
                state.request = r;
            }
            VNSequenceRequestHandler* sequence = state.sequence;
            VNGeneratePersonSegmentationRequest* request = state.request;

            CVPixelBufferRef image = nullptr;
            const CVReturn made = CVPixelBufferCreateWithBytes(kCFAllocatorDefault, static_cast<size_t>(width),
                                                               static_cast<size_t>(height), kCVPixelFormatType_32BGRA,
                                                               const_cast<std::uint8_t*>(bgra),
                                                               static_cast<size_t>(bytesPerRow), nullptr, nullptr,
                                                               nullptr, &image);
            if (made != kCVReturnSuccess || !image) return fail(ErrorCode::Unknown, "could not wrap the image for Vision");
            NSError* error = nil;
            const BOOL ok = [sequence performRequests:@[ request ] onCVPixelBuffer:image error:&error];
            CVPixelBufferRelease(image);
            if (!ok) {
                return fail(ErrorCode::Unknown, std::string("person segmentation failed: ") +
                                                    (error.localizedDescription ? error.localizedDescription.UTF8String : "unknown"));
            }
            VNPixelBufferObservation* observation = request.results.firstObject;
            if (!observation || !observation.pixelBuffer) return fail(ErrorCode::Unknown, "no segmentation result");
            CVPixelBufferRef mask = observation.pixelBuffer;
            CVPixelBufferLockBaseAddress(mask, kCVPixelBufferLock_ReadOnly);
            PersonMask out;
            out.width = static_cast<int>(CVPixelBufferGetWidth(mask));
            out.height = static_cast<int>(CVPixelBufferGetHeight(mask));
            const auto* base = static_cast<const std::uint8_t*>(CVPixelBufferGetBaseAddress(mask));
            const size_t stride = CVPixelBufferGetBytesPerRow(mask);
            out.alpha.resize(static_cast<std::size_t>(out.width) * static_cast<std::size_t>(out.height));
            for (int y = 0; y < out.height && base; ++y) {
                std::memcpy(out.alpha.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(out.width),
                            base + static_cast<std::size_t>(y) * stride, static_cast<std::size_t>(out.width));
            }
            CVPixelBufferUnlockBaseAddress(mask, kCVPixelBufferLock_ReadOnly);
            if (!base) return fail(ErrorCode::Unknown, "empty segmentation mask");
            return out;
        }
    }
    return fail(ErrorCode::Unsupported, "person segmentation needs macOS 12 or later");
}

}  // namespace lectern::platform
