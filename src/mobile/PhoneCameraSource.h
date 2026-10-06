#pragma once

#include "capture/CaptureInterfaces.h"
#include "media/JpegDecoder.h"
#include "network/HttpServer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace lectern::mobile {

/// What the pair sheet shows next to the link.
enum class PhoneCameraState {
    Idle,      ///< server not started
    Waiting,   ///< listening, no phone has opened the page yet
    Live,      ///< the page is open and sending frames
    Error,
};

[[nodiscard]] std::string_view toString(PhoneCameraState state) noexcept;

/// Maps the phone's capture timestamps onto the host clock.
///
/// Every frame gives `arrival - capture`, which is the clock offset plus that
/// frame's encode and network delay. The minimum over a sliding window is the
/// offset plus the *least* delay seen, so `capture + minimum` places each frame
/// at its true capture instant (give or take the best-case delay, a few ms on
/// a LAN) instead of whenever Wi-Fi happened to deliver it. That keeps the
/// phone video in sync with the microphone even when uploads jitter.
class PhoneClockMapper {
public:
    static constexpr std::size_t kWindow = 120;
    /// A jump this large means the page reloaded (performance.now restarted).
    static constexpr std::int64_t kResetNs = 1'000'000'000;

    /// Returns the host capture time for a frame; never later than
    /// `arrivalNs` and strictly increasing across calls.
    [[nodiscard]] std::int64_t map(double phoneTimeMs, std::int64_t arrivalNs);
    void reset();

private:
    std::vector<std::int64_t> deltas_;  ///< ring buffer of arrival - capture
    std::size_t next_ = 0;
    std::int64_t lastHostNs_ = 0;
};

/// A phone used as a wireless camera (docs/PHONE_CAMERA_PROTOCOL.md).
///
/// The transport is a phone browser: the desktop hosts one page, the phone
/// grants camera access with `getUserMedia` and uploads JPEG frames. That
/// avoids the libwebrtc dependency the protocol rejected in §2, because the
/// browser already owns the camera, the codec and the pacing.
///
/// The class is both the capture source and its own HTTP handler, since the
/// server exists only to feed this source.
class PhoneCameraSource final : public capture::IVideoSource, private net::IHttpHandler {
public:
    /// `port` 0 picks an ephemeral port.
    explicit PhoneCameraSource(std::uint16_t port = 0);
    ~PhoneCameraSource() override;
    PhoneCameraSource(const PhoneCameraSource&) = delete;
    PhoneCameraSource& operator=(const PhoneCameraSource&) = delete;

    // --- IVideoSource -------------------------------------------------------
    Status start(capture::IVideoFrameSink& sink) override;
    void stop() override;
    [[nodiscard]] capture::VideoSourceInfo info() const override;
    /// Frames the phone's encoder/upload pipeline dropped, as far as the page
    /// can report them.
    [[nodiscard]] std::uint64_t droppedBySource() const override { return 0; }

    // --- Pairing sheet ------------------------------------------------------
    /// Starts the page host and returns the link to send to the phone.
    [[nodiscard]] Status listen();
    /// Stops the page host and drops the link.
    void unlisten();
    [[nodiscard]] bool listening() const noexcept { return server_.running(); }
    /// TCP port the page is served on (0 until listen()).
    [[nodiscard]] std::uint16_t port() const noexcept { return server_.port(); }
    /// Token-bearing URL to copy or show as a QR code.
    [[nodiscard]] std::string url() const;
    [[nodiscard]] PhoneCameraState state() const noexcept { return state_.load(std::memory_order_acquire); }
    [[nodiscard]] std::size_t clients() const noexcept { return server_.clientCount(); }
    /// Frames received since listen(), for the "streaming" indicator.
    [[nodiscard]] std::uint64_t framesReceived() const noexcept { return framesReceived_.load(std::memory_order_relaxed); }
    /// Free-form status the phone reported (model, battery, thermal state).
    [[nodiscard]] std::string phoneStatus() const;

    /// True when `deviceId` refers to this source, for the camera dropdown.
    static constexpr std::string_view kDeviceId = "phone";

private:
    // IHttpHandler
    net::HttpResponse onGet(std::string_view path) override;
    void onPost(std::string_view path, std::string_view query, std::span<const std::uint8_t> body) override;
    bool acceptWebSocket(std::string_view path) override;
    void onWebSocketMessage(std::string_view path, std::span<const std::uint8_t> data, bool binary) override;

    /// Takes one JPEG from the phone. `phoneTimeMs` is its capture time on
    /// the phone's monotonic clock (performance.now), or < 0 when unknown.
    void acceptFrame(std::span<const std::uint8_t> jpeg, double phoneTimeMs);
    void captureLoop();
    void setState(PhoneCameraState state);

    /// Preferred port; 0 asks the OS for an ephemeral one.
    std::uint16_t requestedPort_ = 0;
    net::HttpServer server_;
    mutable std::mutex mutex_;
    /// Newest uploaded frame; older ones are dropped, which is what a live
    /// preview wants.
    std::vector<std::uint8_t> pending_;
    std::int64_t pendingHostTimeNs_ = 0;
    bool haveFrame_ = false;
    /// Maps phone capture times onto the host clock (guarded by mutex_).
    PhoneClockMapper clock_;
    std::condition_variable frameAvailable_;
    std::atomic<PhoneCameraState> state_{PhoneCameraState::Idle};
    std::atomic<std::uint64_t> framesReceived_{0};
    std::string phoneStatus_;

    capture::IVideoFrameSink* sink_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread captureThread_;
    /// Written by the capture thread, read by info() from the UI thread.
    std::atomic<int> decoderWidth_{0};
    std::atomic<int> decoderHeight_{0};
    std::atomic<int> targetFps_{15};

    /// Tests drive the HTTP surface directly instead of over a socket.
    friend struct PhoneCameraTestAccess;
};

/// Reaches the private IHttpHandler methods for testing.
struct PhoneCameraTestAccess {
    static net::HttpResponse get(PhoneCameraSource& source, std::string_view path) { return source.onGet(path); }
    static void post(PhoneCameraSource& source, std::string_view path, std::string_view query,
                     std::span<const std::uint8_t> body) {
        source.onPost(path, query, body);
    }
};

}  // namespace lectern::mobile