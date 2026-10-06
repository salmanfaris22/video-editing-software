# Windows capture backends (not implemented yet)

Until these exist, `FallbackBackends.cpp` provides synthetic sources so the
app, CLI and tests run. Planned (docs/RECORDING_ENGINE.md §12):

| Interface | API |
|---|---|
| `IScreenCaptureBackend` | Windows.Graphics.Capture (`GraphicsCaptureItem`, `Direct3D11CaptureFramePool`), BGRA→NV12 via `ID3D11VideoProcessor`, frames as `AV_PIX_FMT_D3D11` |
| `ICameraCaptureBackend` | Media Foundation source reader (D3D11-aware) |
| `IAudioCaptureBackend` | WASAPI event-driven capture; WASAPI loopback for system audio |
| Host clock | QPC (`HostClock` already implements it) |
