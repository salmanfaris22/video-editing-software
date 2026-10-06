# Lectern — system requirements

Minimum and recommended hardware and software for running Lectern as an end
user, plus what you need to build and test from source. Platform support matches
[ARCHITECTURE.md](ARCHITECTURE.md) and [README.md](../README.md).

---

## macOS (primary platform, verified)

| | Minimum | Recommended |
|---|---|---|
| **OS** | macOS **14.0 (Sonoma)** or newer | Latest macOS supported by your Mac |
| **Architecture** | 64-bit (Apple Silicon or Intel) | Apple Silicon |
| **Memory** | **8 GB** RAM | **16 GB** RAM or more |
| **Storage** | Enough free space for project folders (screen/camera/audio MKV tracks, exports) | Fast **SSD**; spare capacity for long recordings |
| **GPU** | Not required for core features | Any GPU that supports **VideoToolbox** H.264/HEVC (built into Apple Silicon and recent Intel Macs) |
| **Display** | Any | Retina/high-DPI for comfortable UI; recording resolution is independent |

**Why macOS 14+:** Distribution builds target `CMAKE_OSX_DEPLOYMENT_TARGET=14.0`
for ScreenCaptureKit audio, aspect-ratio handling, and related AVFoundation APIs
(see root [CMakeLists.txt](../CMakeLists.txt)).

**Permissions (required for real recording):**

- **Screen Recording** — relaunch the app after granting if capture stays black.
- **Camera** — optional, for webcam / Continuity Camera.
- **Microphone** — optional; system audio uses ScreenCaptureKit on supported setups.

**Observed on reference hardware** ([PERFORMANCE.md](PERFORMANCE.md), [README.md](../README.md)):

- MacBook Air **M1, 8 GB**: 1080p60 screen + camera, 10 s record, ~95 MB peak RSS, 0 dropped frames.
- Export **1080p30** H.264 + AAC: ~**2× real time**, ~205 MB peak RSS.
- Preview compositor is **CPU-based** today; a dedicated GPU is not required for editing.

---

## Windows

| | Minimum | Recommended |
|---|---|---|
| **OS** | **Windows 10** version **1903+** or **Windows 11**, **64-bit only** | Windows 11 |
| **Architecture** | x64 | x64 |
| **Memory** | 8 GB RAM | 16 GB RAM |
| **GPU** | Optional | Discrete GPU can help hardware encoders (NVENC / QSV / AMF) when available |
| **Storage** | Free space for projects and exports | SSD |

Details and build steps: [WINDOWS.md](WINDOWS.md).

**Support note:** Native capture backends (Windows Graphics Capture, WASAPI, Media
Foundation) compile and are wired in the tree, but **device testing on Windows is
still limited**. Treat Windows as supported in principle; validate on your PC
before relying on it for production.

**Permissions:** Enable **Microphone** and **Camera** under
*Settings → Privacy & security* if recording fails.

---

## Linux

| | Status |
|---|---|
| **Desktop app** | Can be built in some configurations |
| **Screen / camera / mic capture** | **Not implemented** — synthetic sources only |
| **Daily use as recorder/editor** | **Not supported** until native backends land |

See [ARCHITECTURE.md §8](ARCHITECTURE.md) for the platform matrix.

---

## Encoding and performance expectations

- **Hardware encoding** is preferred when FFmpeg can open the platform encoder
  (VideoToolbox on macOS; NVENC/QSV/AMF/MF on Windows). **Software fallback**
  always exists; nothing assumes a particular GPU ([ARCHITECTURE.md §9](ARCHITECTURE.md)).
- **4K / 60 fps** capture and high-DPI workflows are partially implemented;
  1080p30–60 is the best-documented path.
- **Retimed clips** (speed ≠ 1×) may play without sound; speed UI is still
  evolving ([README.md](../README.md) known limitations).

---

## Developer requirements

### All platforms

- **CMake** 3.25+
- **C++23** compiler
- **FFmpeg** libraries (linked directly; no CLI spawn at runtime)
- **Qt 6** (Quick, Gui, Qml, Svg; Network for MCP-related targets)

### macOS development

```bash
brew install cmake ninja pkgconf ffmpeg qtbase qtdeclarative qtsvg googletest nlohmann-json
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

Development builds often link Homebrew libraries built for the **running** macOS
version. Release/distribution builds should use
`-DCMAKE_OSX_DEPLOYMENT_TARGET=14.0` with dependencies built for that target
([README.md](../README.md), [ARCHITECTURE.md §11](ARCHITECTURE.md)).

### Windows development

- Visual Studio **2022** with Desktop C++ and Windows 10/11 SDK
- **Qt 6.8+** (MSVC 2022 64-bit)
- **vcpkg** for native dependencies

See [WINDOWS.md](WINDOWS.md) and `scripts/build-windows.ps1`.

### Tests

- Default suite: `ctest --preset dev` (unit + integration, offscreen Qt).
- **Device tests** (`ctest -L device`): real screen/camera; need macOS permissions.
- Optional mic device test: `LECTERN_TEST_MICROPHONE=1 ctest -L device`.

---

## Not required today

- Dedicated GPU for the editor (CPU compositor for preview/export).
- Linux desktop capture.
- Phone-as-camera (designed in [PHONE_CAMERA_PROTOCOL.md](PHONE_CAMERA_PROTOCOL.md), not shipped).
- Internet connection for core record/edit/export (optional packs/MCP are separate).

---

## Licensing (distribution, not runtime)

Shipping a commercial build has **Qt** (LGPL or commercial), **FFmpeg** (LGPL vs
GPL build), and codec patent considerations. Development on macOS often uses
Homebrew **GPL** FFmpeg; release pipelines should follow
[ARCHITECTURE.md §11](ARCHITECTURE.md).
