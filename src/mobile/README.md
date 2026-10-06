# mobile/ — desktop side of phone cameras (Phase 1C)

Not implemented yet. A connected phone becomes an ordinary
`capture::IVideoSource` / `IAudioSource` whose host timestamps come from the
clock-sync model, so the recording engine needs no special cases. After
recording, `PhoneTrackRelinker` swaps the preview-quality track for the
transferred high-quality file (the preview becomes its proxy).
See [docs/PHONE_CAMERA_PROTOCOL.md](../../docs/PHONE_CAMERA_PROTOCOL.md).
