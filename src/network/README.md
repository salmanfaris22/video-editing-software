# network/ — phone camera transport (Phase 1C)

Not implemented yet. Design: [docs/PHONE_CAMERA_PROTOCOL.md](../../docs/PHONE_CAMERA_PROTOCOL.md).

Planned contents: mDNS advertiser, TLS 1.3 control server with pinned
certificates, pairing (QR token + PAKE fallback), SRTP receiver, RTP H.264
depacketizer, jitter buffer, clock-sync engine, resumable segment transfer.
