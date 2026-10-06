# Linux capture backends (not implemented yet)

Until these exist, `FallbackBackends.cpp` provides synthetic sources so the
app, CLI and tests run. Planned (docs/RECORDING_ENGINE.md §12):

| Interface | API |
|---|---|
| `IScreenCaptureBackend` | xdg-desktop-portal ScreenCast → PipeWire stream (DMA-BUF where possible), X11 fallback |
| `ICameraCaptureBackend` | V4L2 (or PipeWire camera portal) |
| `IAudioCaptureBackend` | PipeWire capture; sink monitor for system audio |
| Host clock | `CLOCK_MONOTONIC` (`HostClock` already implements it) |
