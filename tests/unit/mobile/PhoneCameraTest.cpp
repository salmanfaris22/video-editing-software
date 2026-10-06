// Phone camera transport: the LAN page host, the JPEG decoder and the
// IVideoSource the recording engine sees.
#include "mobile/PhoneCameraSource.h"

#include <gtest/gtest.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/ssl.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <thread>
#include <vector>

namespace lectern::mobile {
namespace {

/// A sink that records what the source delivered.
class RecordingSink final : public capture::IVideoFrameSink {
public:
    void onVideoFrame(capture::CapturedVideoFrame&& frame) noexcept override {
        std::lock_guard lock(mutex_);
        ++frames;
        if (firstWidth == 0) {
            firstWidth = frame.frame->width;
            firstHeight = frame.frame->height;
            firstFormat = static_cast<AVPixelFormat>(frame.frame->format);
        }
        if (lastHostTimeNs != 0 && frame.hostTimeNs <= lastHostTimeNs) ++outOfOrder;
        lastHostTimeNs = frame.hostTimeNs;
        lastSequence = frame.sequence;
    }
    void onSourceEvent(const capture::SourceEvent& event) noexcept override {
        if (event.type == capture::SourceEvent::Type::Started) ++startedEvents;
    }

    [[nodiscard]] int count() const {
        std::lock_guard lock(mutex_);
        return frames;
    }
    [[nodiscard]] int width() const {
        std::lock_guard lock(mutex_);
        return firstWidth;
    }
    [[nodiscard]] int height() const {
        std::lock_guard lock(mutex_);
        return firstHeight;
    }
    [[nodiscard]] AVPixelFormat format() const {
        std::lock_guard lock(mutex_);
        return firstFormat;
    }

    std::atomic<int> startedEvents{0};
    std::atomic<int> outOfOrder{0};

private:
    mutable std::mutex mutex_;
    int frames = 0;
    int firstWidth = 0;
    int firstHeight = 0;
    AVPixelFormat firstFormat = AV_PIX_FMT_NONE;
    std::uint64_t lastSequence = 0;
    std::int64_t lastHostTimeNs = 0;
};

/// Polls until `predicate` holds. Frame delivery is asynchronous by design, so
/// tests wait for the condition instead of sleeping a fixed amount.
template <class Predicate>
bool waitFor(Predicate predicate, std::chrono::milliseconds budget = std::chrono::milliseconds(3000)) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return predicate();
}

/// Encodes one frame as JPEG with libavcodec, so the test needs no fixture file
/// and no encoder on the machine beyond FFmpeg itself.
std::vector<std::uint8_t> encodeJpeg(int width, int height, int luma = 96) {
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) return {};
    media::AVCodecContextPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx) return {};
    ctx->width = width;
    ctx->height = height;
    ctx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    ctx->time_base = AVRational{1, 25};
    if (avcodec_open2(ctx.get(), codec, nullptr) < 0) return {};

    AVFrame* raw = av_frame_alloc();
    raw->format = ctx->pix_fmt;
    raw->width = width;
    raw->height = height;
    if (av_frame_get_buffer(raw, 32) < 0 || av_frame_make_writable(raw) < 0) {
        av_frame_free(&raw);
        return {};
    }
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) raw->data[0][y * raw->linesize[0] + x] = static_cast<std::uint8_t>(luma);
    }
    for (int y = 0; y < height / 2; ++y) {
        for (int x = 0; x < width / 2; ++x) {
            raw->data[1][y * raw->linesize[1] + x] = 128;
            raw->data[2][y * raw->linesize[2] + x] = 128;
        }
    }

    std::vector<std::uint8_t> out;
    if (avcodec_send_frame(ctx.get(), raw) == 0) {
        AVPacket* packet = av_packet_alloc();
        if (avcodec_receive_packet(ctx.get(), packet) == 0) {
            out.assign(packet->data, packet->data + packet->size);
        }
        av_packet_free(&packet);
    }
    av_frame_free(&raw);
    return out;  // ctx frees itself
}

/// The token path segment of a url, e.g. "abcd…" from "https://ip:port/abcd…".
std::string tokenOf(const std::string& url) {
    const auto scheme = url.find("://");
    const auto hostStart = scheme == std::string::npos ? 0 : scheme + 3;
    const auto slash = url.find('/', hostStart);
    return url.substr(slash + 1);
}

TEST(PhoneCamera, ListenAdvertisesATokenUrlOnTheLan) {
    PhoneCameraSource source;
    EXPECT_EQ(source.state(), PhoneCameraState::Idle);

    auto started = source.listen();
    ASSERT_TRUE(started.has_value()) << started.error().toString();
    EXPECT_TRUE(source.listening());
    EXPECT_EQ(source.state(), PhoneCameraState::Waiting);

    // The link is the whole feature: http, the port actually bound, and a
    // secret path segment so a LAN stranger cannot just browse to it.
    const std::string url = source.url();
    EXPECT_EQ(url.rfind("https://", 0), 0u) << url;
    EXPECT_NE(url.find(":" + std::to_string(source.port()) + "/"), std::string::npos) << url;
    EXPECT_EQ(tokenOf(url).size(), 32u) << url;
    // Hex only, so the token needs no percent-decoding anywhere.
    EXPECT_EQ(tokenOf(url).find_first_not_of("0123456789abcdef"), std::string::npos) << url;

    source.unlisten();
    EXPECT_FALSE(source.listening());
    EXPECT_EQ(source.state(), PhoneCameraState::Idle);
}

TEST(PhoneCamera, ServesThePageOnlyWithTheToken) {
    PhoneCameraSource source;
    ASSERT_TRUE(source.listen().has_value());
    const std::string token = tokenOf(source.url());

    net::HttpResponse good = PhoneCameraTestAccess::get(source, "/" + token);
    EXPECT_EQ(good.status, "200 OK");
    EXPECT_EQ(good.contentType, "text/html; charset=utf-8");
    // The page is useless without the camera API and the frame route.
    EXPECT_NE(good.body.find("getUserMedia"), std::string::npos);
    EXPECT_NE(good.body.find("/frame"), std::string::npos);

    // The trailing-slash form the phone actually navigates to.
    EXPECT_EQ(PhoneCameraTestAccess::get(source, "/" + token + "/").status, "200 OK");

    EXPECT_EQ(PhoneCameraTestAccess::get(source, "/").status, "404 Not Found");
    EXPECT_EQ(PhoneCameraTestAccess::get(source, "/deadbeef").status, "404 Not Found");
    EXPECT_EQ(PhoneCameraTestAccess::get(source, "/" + token + "/nope").status, "404 Not Found");
    source.unlisten();
}

TEST(PhoneCamera, UploadedFramesReachTheSinkAsYuv420p) {
    const auto jpeg = encodeJpeg(160, 120);
    ASSERT_FALSE(jpeg.empty()) << "mjpeg encoder unavailable";

    PhoneCameraSource source;
    RecordingSink sink;
    ASSERT_TRUE(source.start(sink).has_value());

    const std::string token = tokenOf(source.url());
    // At a realistic 15 fps cadence nothing should be dropped; see
    // OnlyTheNewestFrameIsKept for the burst case.
    for (int i = 0; i < 3; ++i) {
        PhoneCameraTestAccess::post(source, "/" + token + "/frame", "", jpeg);
        std::this_thread::sleep_for(std::chrono::milliseconds(66));
    }

    EXPECT_TRUE(waitFor([&] { return sink.count() >= 3; })) << "only " << sink.count() << " frames arrived";
    EXPECT_EQ(sink.width(), 160);
    EXPECT_EQ(sink.height(), 120);
    EXPECT_EQ(sink.format(), AV_PIX_FMT_YUV420P);
    EXPECT_EQ(sink.outOfOrder.load(), 0) << "host timestamps must not go backwards";
    EXPECT_EQ(sink.startedEvents.load(), 1) << "the phone connecting is one event, not one per frame";
    EXPECT_EQ(source.state(), PhoneCameraState::Live);
    EXPECT_GE(source.framesReceived(), 3u);
    source.stop();
}

TEST(PhoneCamera, MalformedUploadsAreDroppedAndTheSourceStaysLive) {
    const auto jpeg = encodeJpeg(64, 64, 200);
    ASSERT_FALSE(jpeg.empty());

    PhoneCameraSource source;
    RecordingSink sink;
    ASSERT_TRUE(source.start(sink).has_value());
    const std::string framePath = "/" + tokenOf(source.url()) + "/frame";

    const std::uint8_t garbage[] = {'n', 'o', 't', ' ', 'a', ' ', 'j', 'p', 'e', 'g'};
    PhoneCameraTestAccess::post(source, framePath, "", garbage);
    // A real frame straight after proves the bad one did not wedge the loop.
    PhoneCameraTestAccess::post(source, framePath, "", jpeg);

    EXPECT_TRUE(waitFor([&] { return sink.count() >= 1; })) << "no frame delivered after a malformed upload";
    EXPECT_EQ(sink.outOfOrder.load(), 0);
    source.stop();
}

TEST(PhoneCamera, UploadsWithTheWrongTokenAreIgnored) {
    const auto jpeg = encodeJpeg(64, 64);
    ASSERT_FALSE(jpeg.empty());

    PhoneCameraSource source;
    RecordingSink sink;
    ASSERT_TRUE(source.start(sink).has_value());

    PhoneCameraTestAccess::post(source, "/00000000000000000000000000000000/frame", "", jpeg);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    EXPECT_EQ(sink.count(), 0) << "a guessed token must not open the camera";
    EXPECT_EQ(source.state(), PhoneCameraState::Waiting);
    source.stop();
}

TEST(PhoneCamera, OnlyTheNewestFrameIsKept) {
    const auto jpeg = encodeJpeg(64, 64);
    ASSERT_FALSE(jpeg.empty());

    PhoneCameraSource source;
    RecordingSink sink;
    ASSERT_TRUE(source.start(sink).has_value());
    const std::string framePath = "/" + tokenOf(source.url()) + "/frame";

    // A burst faster than the capture thread drains must not queue up: latency
    // matters more than completeness for a live preview.
    for (int i = 0; i < 40; ++i) PhoneCameraTestAccess::post(source, framePath, "", jpeg);
    EXPECT_TRUE(waitFor([&] { return sink.count() >= 1; }));
    std::this_thread::sleep_for(std::chrono::milliseconds(250));

    EXPECT_LT(sink.count(), 40) << "preview frames should be dropped, not queued";
    EXPECT_GT(sink.count(), 0);
    source.stop();
}

TEST(PhoneCamera, ReportsPhoneKindWithoutClaimingAFrameRateItHasNotSeen) {
    PhoneCameraSource source;
    const auto info = source.info();
    EXPECT_EQ(info.kind, capture::SourceKind::Phone);
    EXPECT_EQ(info.deviceId, std::string(PhoneCameraSource::kDeviceId));
    EXPECT_TRUE(info.nominalFrameRate.isValid());
    // Nothing is known about the phone's geometry before one connects.
    EXPECT_EQ(info.width, 0);
    EXPECT_EQ(info.height, 0);
    EXPECT_EQ(source.droppedBySource(), 0u);
    EXPECT_EQ(source.phoneStatus(), "");
}

TEST(PhoneCamera, StopIsIdempotentAndReleasesThePort) {
    PhoneCameraSource source;
    RecordingSink sink;
    ASSERT_TRUE(source.start(sink).has_value());
    const std::uint16_t port = source.port();
    ASSERT_NE(port, 0);

    source.stop();
    source.stop();
    EXPECT_FALSE(source.listening());

    // The OS released the port, so the next pair attempt binds cleanly.
    PhoneCameraSource again;
    ASSERT_TRUE(again.listen().has_value());
    EXPECT_EQ(again.state(), PhoneCameraState::Waiting);
    again.unlisten();
}

// --- Over a real socket ------------------------------------------------------

/// A minimal TLS client for the loopback server (the phone, in tests).
class TlsClient {
public:
    explicit TlsClient(std::uint16_t port) {
        ctx_ = SSL_CTX_new(TLS_client_method());
        fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
        int one = 1;
        ::setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::connect(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) return;
        ssl_ = SSL_new(ctx_);  // self-signed: no verification, as a phone after "Continue"
        SSL_set_fd(ssl_, fd_);
        connected_ = SSL_connect(ssl_) == 1;
    }
    ~TlsClient() {
        if (ssl_) SSL_free(ssl_);
        if (fd_ >= 0) ::close(fd_);
        SSL_CTX_free(ctx_);
    }
    [[nodiscard]] bool connected() const { return connected_; }
    bool write(std::string_view data) {
        return SSL_write(ssl_, data.data(), static_cast<int>(data.size())) == static_cast<int>(data.size());
    }
    /// Reads until the head of one HTTP response (and its short body) arrived.
    std::string readResponse() {
        std::string out;
        char buf[4096];
        while (out.find("\r\n\r\n") == std::string::npos) {
            const int n = SSL_read(ssl_, buf, sizeof(buf));
            if (n <= 0) break;
            out.append(buf, static_cast<std::size_t>(n));
        }
        return out;
    }
    /// Sends one masked client frame (RFC 6455 §5.2).
    bool sendWs(std::uint8_t opcode, std::span<const std::uint8_t> payload) {
        std::string frame;
        frame.push_back(static_cast<char>(0x80 | opcode));
        const std::size_t n = payload.size();
        if (n < 126) {
            frame.push_back(static_cast<char>(0x80 | n));
        } else if (n <= 0xFFFF) {
            frame.push_back(static_cast<char>(0x80 | 126));
            frame.push_back(static_cast<char>(n >> 8));
            frame.push_back(static_cast<char>(n & 0xFF));
        } else {
            frame.push_back(static_cast<char>(0x80 | 127));
            for (int i = 7; i >= 0; --i) frame.push_back(static_cast<char>((n >> (8 * i)) & 0xFF));
        }
        const std::uint8_t mask[4] = {0x12, 0x34, 0x56, 0x78};
        frame.append(reinterpret_cast<const char*>(mask), 4);
        for (std::size_t i = 0; i < n; ++i) frame.push_back(static_cast<char>(payload[i] ^ mask[i & 3]));
        return write(frame);
    }

private:
    SSL_CTX* ctx_ = nullptr;
    SSL* ssl_ = nullptr;
    int fd_ = -1;
    bool connected_ = false;
};

std::vector<std::uint8_t> wsFramePayload(double captureMs, const std::vector<std::uint8_t>& jpeg) {
    std::vector<std::uint8_t> out(8);
    std::uint64_t bits = 0;
    std::memcpy(&bits, &captureMs, sizeof(bits));
    for (int i = 0; i < 8; ++i) out[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(bits >> (8 * i));
    out.insert(out.end(), jpeg.begin(), jpeg.end());
    return out;
}

TEST(PhoneCamera, StreamsFramesOverOneWebSocket) {
    const auto jpeg = encodeJpeg(320, 240);
    ASSERT_FALSE(jpeg.empty());

    PhoneCameraSource source;
    RecordingSink sink;
    ASSERT_TRUE(source.start(sink).has_value());
    const std::string token = tokenOf(source.url());

    TlsClient phone(source.port());
    ASSERT_TRUE(phone.connected());
    ASSERT_TRUE(phone.write("GET /" + token + "/ws HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                            "Sec-WebSocket-Version: 13\r\n\r\n"));
    const std::string reply = phone.readResponse();
    ASSERT_EQ(reply.rfind("HTTP/1.1 101", 0), 0u) << reply;
    // The RFC 6455 §1.3 worked example.
    EXPECT_NE(reply.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), std::string::npos) << reply;

    // 30 frames at 30 fps on one connection, plus a heartbeat in between.
    const std::uint8_t hb[] = {'h', 'b'};
    for (int i = 0; i < 30; ++i) {
        ASSERT_TRUE(phone.sendWs(0x2, wsFramePayload(1000.0 + i * 33.3, jpeg)));
        if (i == 10) ASSERT_TRUE(phone.sendWs(0x1, hb));
        std::this_thread::sleep_for(std::chrono::milliseconds(33));
    }
    EXPECT_TRUE(waitFor([&] { return sink.count() >= 25; })) << "only " << sink.count() << " frames arrived";
    EXPECT_EQ(sink.width(), 320);
    EXPECT_EQ(sink.outOfOrder.load(), 0);
    EXPECT_EQ(sink.startedEvents.load(), 1);
    EXPECT_EQ(source.framesReceived(), 30u);
    EXPECT_EQ(source.clients(), 1u) << "every frame must reuse the one connection";

    // stop() must wake the thread blocked reading the open socket.
    const auto t0 = std::chrono::steady_clock::now();
    source.stop();
    EXPECT_LT(std::chrono::steady_clock::now() - t0, std::chrono::seconds(2));
    EXPECT_EQ(source.clients(), 0u);
}

TEST(PhoneCamera, WebSocketNeedsTheToken) {
    PhoneCameraSource source;
    ASSERT_TRUE(source.listen().has_value());
    TlsClient phone(source.port());
    ASSERT_TRUE(phone.connected());
    ASSERT_TRUE(phone.write("GET /00000000000000000000000000000000/ws HTTP/1.1\r\nUpgrade: websocket\r\n"
                            "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n"));
    EXPECT_EQ(phone.readResponse().rfind("HTTP/1.1 404", 0), 0u);
    source.unlisten();
}

TEST(PhoneCamera, PlainRequestsReuseAKeepAliveConnection) {
    PhoneCameraSource source;
    ASSERT_TRUE(source.listen().has_value());
    const std::string token = tokenOf(source.url());
    TlsClient phone(source.port());
    ASSERT_TRUE(phone.connected());
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(phone.write("POST /" + token + "/ctl?op=hello HTTP/1.1\r\nContent-Length: 0\r\n\r\n"));
        const std::string reply = phone.readResponse();
        ASSERT_EQ(reply.rfind("HTTP/1.1 200", 0), 0u) << "request " << i << ": " << reply;
        EXPECT_NE(reply.find("keep-alive"), std::string::npos);
    }
    EXPECT_EQ(source.phoneStatus(), "op=hello");
    source.unlisten();
}

// --- Clock mapping -----------------------------------------------------------

TEST(PhoneClockMapper, StampsFramesAtCaptureTimeDespiteJitter) {
    PhoneClockMapper mapper;
    // Phone clock is 5 s behind the host; delivery delay alternates 10/120 ms.
    constexpr std::int64_t kOffset = 5'000'000'000;
    std::int64_t previous = 0;
    for (int i = 0; i < 60; ++i) {
        const double captureMs = 100.0 + i * 33.0;
        const std::int64_t captureHost = static_cast<std::int64_t>(captureMs * 1e6) + kOffset;
        const std::int64_t delay = (i % 2 == 0 ? 10 : 120) * 1'000'000LL;
        const std::int64_t host = mapper.map(captureMs, captureHost + delay);
        EXPECT_GT(host, previous);
        previous = host;
        if (i >= 1) {
            // Once the fast path is known, every frame lands 10 ms after its
            // capture, however late it actually arrived.
            EXPECT_EQ(host, captureHost + 10'000'000) << "frame " << i;
        }
    }
}

TEST(PhoneClockMapper, RecoversWhenThePageReloads) {
    PhoneClockMapper mapper;
    for (int i = 0; i < 10; ++i) (void)mapper.map(50'000.0 + i * 33, 1'000'000'000LL + i * 33'000'000LL);
    // performance.now restarted near zero: delta jumps by ~50 s.
    const std::int64_t arrival = 2'000'000'000;
    EXPECT_EQ(mapper.map(10.0, arrival), arrival);
    EXPECT_EQ(mapper.map(43.0, arrival + 33'000'000), arrival + 33'000'000);
}

TEST(PhoneClockMapper, FallsBackToArrivalWithoutACaptureTime) {
    PhoneClockMapper mapper;
    EXPECT_EQ(mapper.map(-1, 777), 777);
    EXPECT_EQ(mapper.map(-1, 700), 778) << "must still increase";
}

}  // namespace
}  // namespace lectern::mobile