# Phone Camera Protocol (Phase 1C design)

Goal: an Android phone or iPhone on the same LAN acts as a **wireless camera**
with a low-latency preview on the desktop and a **high-quality local
recording** that is transferred afterwards and replaces the preview-quality
track automatically, in sync with every other track.

Status: **designed, not implemented.** The recording engine's interfaces
(`IVideoSource`, host-time timestamps, manifest, relinking hooks) are already
shaped for it.

---

## 1. Requirements

| # | Requirement |
|---|---|
| R1 | LAN only; no internet dependency for normal use |
| R2 | Preview latency ≤ 150 ms glass-to-glass on good Wi-Fi |
| R3 | Final camera quality independent of Wi-Fi quality (local HQ recording) |
| R4 | Phone keeps recording through temporary disconnects; desktop catches up later |
| R5 | Frame-accurate placement on the session timeline (clock offset + drift) |
| R6 | No unauthenticated access; explicit user approval for new phones; encrypted |
| R7 | Implementable natively on Android (Kotlin), iOS (Swift), desktop (C++) on Windows/macOS/Linux |

## 2. Options evaluated

| Option | Strengths | Weaknesses | Verdict |
|---|---|---|---|
| **WebRTC (libwebrtc)** | Mature congestion control, jitter buffer, NACK/FEC, DTLS-SRTP, mobile SDKs with hardware codecs | Huge desktop dependency (GN/Chromium toolchain, about 30 MB, frequent security updates); ICE/SDP machinery solves NAT traversal we do not need on a LAN; congestion control tuned for the internet; signaling still needed | Rejected for Phase 1 |
| **libdatachannel** (WebRTC transport subset) | Small, CMake-friendly, DTLS-SRTP, H.264 RTP packetizer | Still needs our own jitter buffer, congestion control and depacketizer on every side; brings SDP/ICE complexity anyway | Fallback option |
| **RTP/SRTP over UDP + TLS control** | Minimal dependencies (libsrtp2, OpenSSL / platform TLS), no head-of-line blocking, full control over timestamps; standard packet formats (RFC 6184, 3550, 4585, 7714) are easy to debug with Wireshark | We implement jitter buffer, NACK/PLI handling and a simple bitrate controller (bounded scope on a LAN) | **Chosen** |
| QUIC (msquic / Network.framework / quiche) | One connection for control, datagrams and files; TLS 1.3 built in; connection migration | No system QUIC API on Android (native library + JNI); datagram support uneven; little production evidence for this use | Revisit for Phase 2 (internet relay) |
| TCP/TLS for everything | Simplest | Head-of-line blocking on Wi-Fi loss causes preview stalls of hundreds of ms | Used only for control and file transfer |

**Why the simple stack is enough:** the dual strategy lowers the stakes for
the live stream. The preview only needs to look good *enough* and stay
low-latency, because the HQ file is the source of truth. That removes most
of the reasons to pay for WebRTC's complexity. The transport is behind an
interface (`IPreviewTransport`), so QUIC datagrams or libdatachannel can
replace it without touching capture, sync, or UI code.

## 3. Channels

```
 Phone                                                   Desktop
 ─────                                                   ───────
 TLS 1.3 / TCP  ── control (JSON lines, ≤ 64 KiB msgs) ──▶ :port (mTLS, pinned)
 SRTP / UDP     ── preview video (H.264 RTP) ────────────▶ :udp  (keys from control)
               ◀── SRTCP: RR, NACK, PLI, sync pings ─────
 TLS 1.3 / TCP  ── HQ segment transfer (resumable) ──────▶ :port (same listener, separate connection)
```

* The desktop is the **server** (the phone scans the desktop's QR code). The
  phone is the client.
* All channels are encrypted and authenticated. SRTP keys (AES-128-GCM,
  RFC 7714) are generated per session by the desktop and delivered over the
  mutually authenticated TLS channel.

## 4. Discovery

* The desktop advertises `_lectern._tcp.local` via mDNS/DNS-SD (Bonjour on
  macOS, `DnsServiceRegister` on Windows 10+, Avahi on Linux). TXT records
  contain `v=1`, `id=<sha256(desktop cert)[:8]>`, `n=<display name>`, and
  **no secrets**.
* Advertising and listening happen **only** while the "Add camera" dialog or
  the recording screen with a paired phone is open.
* Phones browse with `NWBrowser` (iOS; requires `NSLocalNetworkUsageDescription`
  and `NSBonjourServices`) or `NsdManager` (Android, with a multicast lock).
  They reconnect to known desktops by `id`, which survives DHCP address
  changes.

## 5. Pairing

```
Desktop                                       Phone
───────                                       ─────
"Add camera → Connect phone"
  create one-time token T (128-bit, 120 s, single use)
  start TLS listener (ephemeral port)
  show QR:
   lectern://pair?v=1&h=192.168.1.20,fe80::1&p=53817
                 &fp=<SHA-256 of desktop cert, base64url>
                 &t=<T>&n=Salman's%20MacBook&s=<pairing id>
  + 6-digit fallback code
                                              scan QR → TLS connect
                                              verify server cert SHA-256 == fp  (MITM-proof)
                                              present client cert (phone identity)
                              ◀── PairRequest{token:T, name, model, os, appVersion}
  verify T (constant-time, unexpired, unused)
  UI: "Allow “Salman's iPhone” to connect as a camera?"
      user approves ──────────────────────▶ PairResult{ok, desktopName}
  store trusted device {phoneCertFp, name, pairedAt}
                                              store trusted desktop {fp, id, name}
```

* **Identities:** each desktop and phone has a long-lived self-signed ECDSA
  P-256 certificate. Private keys live in the OS keystore (macOS Keychain,
  Windows DPAPI/CNG, Linux libsecret, iOS Keychain, Android Keystore).
* **Later connections:** mutual TLS where each side pins the other's
  fingerprint. A desktop listener rejects any client certificate not in the
  trusted list **during the TLS handshake**, so unauthenticated peers never
  reach protocol parsing.
* **Manual 6-digit code** (when QR scanning is impossible): the code is too
  short for a plain shared secret, so it feeds a PAKE (CPace) run inside a TLS
  session and bound to it with the TLS exporter. Three failed attempts
  regenerate the code.
* Trusted devices can be revoked from Settings → Connected cameras.

## 6. Control protocol

Length-prefixed (u32 big-endian) UTF-8 JSON messages, max 64 KiB, with
strict schema validation. Core messages:

| Message | Direction | Purpose |
|---|---|---|
| `Hello` | both | protocol version, capabilities (codecs, max resolution/fps, lenses) |
| `SessionPrepare` | D→P | session id, preview profile (e.g. 720p30 2.5 Mbit/s), HQ profile (e.g. 1080p30/4K30 HEVC 20 Mbit/s + audio), UDP port, SRTP master key/salt |
| `Start` / `Pause` / `Resume` / `Stop` | D→P | with the desktop host time of the action |
| `Status` | P→D (1 Hz) | battery, charging, thermal state, free storage, recording state, encoder fps and bitrate, dropped frames |
| `CameraControl` | D→P | lens (front/back/ultra-wide), zoom, focus/exposure point, exposure bias, stabilization, torch |
| `SegmentAvailable` / `TransferRequest` / `TransferAck` | both | HQ segment catalog and transfer |
| `Error` | both | code + message |

## 7. Clock synchronization

The desktop host clock is the master. Phone timestamps use the phone's
monotonic clock: `mach_absolute_time` on iOS (the `CMClock` host clock, which
AVFoundation sample buffers use), and `elapsedRealtimeNanos` on Android
(Camera2 sensor timestamps share this base when
`SENSOR_INFO_TIMESTAMP_SOURCE == REALTIME`. For `UNKNOWN`, the app measures
the sensor→`elapsedRealtimeNanos` offset at frame arrival using a
minimum-latency filter).

**Measurement.** Every 250 ms (1 s when idle) over SRTCP (UDP, which avoids
TCP retransmission jitter):

```
desktop t1 ──ping──▶ phone t2
desktop t4 ◀──pong── phone t3
rtt    = (t4 − t1) − (t3 − t2)
offset = ((t2 − t1) + (t3 − t4)) / 2        (phone − desktop)
```

**Filtering.** Queuing only ever adds delay, so within each 2 s bucket only
the sample with the minimum `rtt` is kept. A weighted least-squares line
`offset(t) = θ₀ + ε·t` is fit over the last 60 s of buckets, which estimates
offset and **drift** (ε, ppm). Expected accuracy on a healthy LAN is 1–3 ms.
After a disconnect the model keeps extrapolating, which is accurate to about
1 ms/min at 20 ppm, and re-converges within seconds of reconnecting.

**Mapping.** `desktopHostNs = phoneNs − offset(phoneNs)`. The result feeds the
same `SessionClock` as local sources, so phone tracks obey the same
start/pause/stop rules.

**Refinement.** If the phone also records audio, Auto Sync (GCC-PHAT
against the desktop microphone) refines the final offset after transfer and
reports its confidence.

## 8. Preview stream

* The phone runs a **second** hardware encoder (VideoToolbox / MediaCodec;
  both platforms support concurrent sessions): 720p30 H.264 Baseline/Main,
  1–3 Mbit/s, keyframe every 2 s plus on PLI, no B-frames.
* RTP packetization per RFC 6184 (single NAL + FU-A), MTU 1200, 90 kHz RTP
  clock, and an RFC 8285 header extension carrying the 64-bit phone capture
  time.
* Desktop: adaptive jitter buffer (target 40–100 ms), generic NACK (RFC 4585)
  when RTT allows retransmission before the playout deadline, PLI when a
  frame is lost, VideoToolbox/D3D11VA/VA-API hardware decode.
* Bitrate control on the phone (AIMD): loss > 2 % → −15 %; loss < 0.5 % for
  5 s → +5 %, within the preview profile's bounds.
* The desktop **also records** the preview packets as a fallback track
  (remux without re-encoding into `media/phone/phone-<sid>-preview.mkv` with
  session timestamps). If HQ transfer never completes, the project still has
  the phone camera at preview quality.

## 9. High-quality local recording on the phone

* Separate encoder session at the HQ profile (e.g. 1080p30 or 4K30 HEVC,
  15–40 Mbit/s, plus AAC or PCM audio when phone audio is enabled).
* **Crash-safe, append-only segment format**, identical on iOS and Android:
  ```
  <session>/seg-00001.h264|.hevc   Annex-B elementary stream
  <session>/seg-00001.idx          per frame: phone ns, byte offset, size, keyframe flag
  <session>/seg-00001.pcm|.aac     audio + .aidx (sample index ↔ phone ns)
  <session>/session.json           profile, lens, orientation, clock-sync summary
  ```
  This avoids Android `MediaMuxer`, which writes `moov` last and so leaves
  an unrecoverable file on crash, and keeps exact per-frame timestamps for
  variable frame rate. Segments roll every 60 s, each starting with a
  keyframe.
* Recording continues if the control channel drops (R4). On iOS the app must
  stay in the foreground (camera access is suspended in the background), so
  the app keeps the screen awake and shows a large recording indicator.
  Android uses a foreground service.

## 10. Transfer and relink

* **Trickle transfer:** completed segments are transferred during recording
  when the preview has bandwidth headroom (throttled to stay below 50 % of
  measured capacity). The remaining segments go after `Stop`. This makes the
  post-recording wait short.
* Resumable: `TransferRequest{segment, offset}`, 1 MiB chunks, SHA-256 per
  segment verified before acceptance. Missing segments are retried after
  reconnects.
* The desktop muxes the segments (no re-encode) into
  `media/phone/phone-<sid>-hq.mkv` with timestamps mapped to session time
  (§7). It then **relinks**: the phone `MediaSource` switches its primary
  path to the HQ file and keeps the preview file as its proxy. Clips are
  untouched because both files share session time. The operation is a
  project command and can be undone.

## 11. Disconnects

| Situation | Behavior |
|---|---|
| Wi-Fi blip < 5 s | jitter buffer and NACK; preview freezes briefly; HQ unaffected |
| Longer outage | desktop shows "Phone reconnecting"; preview track gets a timestamp gap; phone keeps recording HQ; clock model extrapolates |
| Reconnect | mTLS resumes (TLS session resumption), clock re-converges, missing segments are queued for transfer |
| Phone app killed | segments up to the last completed one (≤ 60 s loss, or less if fragments are flushed every 1 s) are recoverable when the app restarts; the preview fallback covers the rest |

## 12. Threat model

| Threat | Mitigation |
|---|---|
| Eavesdropping on Wi-Fi | TLS 1.3, SRTP AES-GCM |
| MITM during pairing | QR carries the desktop certificate fingerprint (pinned); manual code uses a PAKE |
| Rogue LAN device connecting | single-use 120 s token + explicit user approval + mTLS pinning afterwards |
| Replay | TLS; SRTP replay window; per-session SRTP keys |
| Token leak (e.g. QR visible while screen sharing) | short lifetime, single use, user approval still required |
| LAN DoS / scanning | listener only active when needed; handshake-level rejection; rate limits |
| Lost or stolen phone | revoke from desktop trusted list (fingerprint removed) |
| Malformed packets | bounded parsers, size limits, fuzzing of RTP/control parsers in CI |

## 13. Implementation plan

**Desktop** (`src/network`, `src/mobile`):
`MdnsAdvertiser`, `TlsServer` (OpenSSL 3), `PairingService`, `TrustStore`,
`ControlSession`, `SrtpReceiver` (libsrtp2), `RtpH264Depacketizer`,
`JitterBuffer`, `ClockSyncEngine`, `PhoneCameraSource : IVideoSource`,
`SegmentTransferClient`, `PhoneTrackRelinker`.

**Android** (`mobile/android`, Kotlin): Camera2 with multiple output
surfaces (preview view + two `MediaCodec` input surfaces), segment writer,
RTP packetizer, libsrtp via a JNI shim, `SSLSocket` with a pinning
`TrustManager`, `NsdManager`, foreground service, battery/thermal/storage
monitors, CameraX-free for timestamp control.

**iOS** (`mobile/ios`, Swift): `AVCaptureSession` +
`AVCaptureVideoDataOutput` → two `VTCompressionSession`s, segment writer,
`NWConnection` (TLS with `sec_protocol_options_set_verify_block` pinning;
UDP), `NWBrowser`, libsrtp as an XCFramework.

**macOS note:** Continuity Camera iPhones already appear as AVFoundation
cameras in the desktop camera list (preview quality only, no HQ recording).
The companion app remains the cross-platform, high-quality path.
