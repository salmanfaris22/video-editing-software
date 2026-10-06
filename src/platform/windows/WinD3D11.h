#pragma once

// Direct3D 11 helpers for screen capture (internal to src/platform/windows).
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinCommon.h"

#include <d3d11.h>

#include "core/Time.h"
#include "media/Frame.h"

#include <memory>
#include <string_view>

namespace lectern::platform::win {

/// Hardware device (WARP as a last resort) with BGRA support, video support
/// when the driver offers it, and multithread protection (capture callbacks
/// and our converter share the immediate context).
[[nodiscard]] Result<ComPtr<ID3D11Device>> createCaptureDevice();

/// Turns captured BGRA textures into CPU frames for the encoder.
class FrameConverter {
public:
    virtual ~FrameConverter() = default;
    /// `texture` (B8G8R8A8) holds the image in its top-left
    /// `contentWidth`×`contentHeight` pixels.
    [[nodiscard]] virtual Result<media::Frame> convert(ID3D11Texture2D* texture, int contentWidth,
                                                       int contentHeight) = 0;
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
};

/// GPU path: the D3D11 video processor scales and converts BGRA to NV12
/// (BT.709, limited range), letterboxed into outWidth×outHeight; only the
/// NV12 result is read back (12 bits per pixel instead of 32).
[[nodiscard]] Result<std::unique_ptr<FrameConverter>> createGpuNv12Converter(ID3D11Device* device, int outWidth,
                                                                             int outHeight, FrameRate fps);

/// Fallback when the driver has no video processor: BGRA read back as is;
/// the encoder scales and converts on the CPU.
[[nodiscard]] std::unique_ptr<FrameConverter> createBgraReadback(ID3D11Device* device);

}  // namespace lectern::platform::win
