#include "mobile/PhoneCameraSource.h"

#include "core/Clock.h"
#include "core/Log.h"
#include "core/Uuid.h"
#include "mobile/PhonePage.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace lectern::mobile {
namespace {
constexpr std::string_view kCategory = "phone-camera";

std::string_view hexToken() {
    static const std::string token = [] {
        std::string t = Uuid::generateV4().toString();
        std::erase_if(t, [](unsigned char c) { return !std::isxdigit(c); });
        t.resize(32);
        return t;
    }();
    return token;
}
}  // namespace

std::string_view toString(PhoneCameraState state) noexcept {
    switch (state) {
        case PhoneCameraState::Idle: return "Idle";
        case PhoneCameraState::Waiting: return "Waiting for phone";
        case PhoneCameraState::Live: return "Streaming";
        case PhoneCameraState::Error: return "Error";
    }
    return "Unknown";
}

std::int64_t PhoneClockMapper::map(double phoneTimeMs, std::int64_t arrivalNs) {
    std::int64_t host = arrivalNs;
    if (phoneTimeMs >= 0) {
        const auto phoneNs = static_cast<std::int64_t>(phoneTimeMs * 1e6);
        const std::int64_t delta = arrivalNs - phoneNs;
        if (!deltas_.empty()) {
            const std::int64_t current = *std::ranges::min_element(deltas_);
            if (delta < current - kResetNs || delta > current + kResetNs) reset();
        }
        if (deltas_.size() < kWindow) {
            deltas_.push_back(delta);
        } else {
            deltas_[next_] = delta;
            next_ = (next_ + 1) % kWindow;
        }
        host = std::min(arrivalNs, phoneNs + *std::ranges::min_element(deltas_));
    }
    // The pacer and muxer need strictly increasing capture times.
    host = std::max(host, lastHostNs_ + 1);
    lastHostNs_ = host;
    return host;
}

void PhoneClockMapper::reset() {
    deltas_.clear();
    next_ = 0;
}

PhoneCameraSource::PhoneCameraSource(std::uint16_t port) : requestedPort_(port) { server_.setHandler(this); }

PhoneCameraSource::~PhoneCameraSource() {
    stop();
    unlisten();
}

void PhoneCameraSource::setState(PhoneCameraState state) {
    const auto previous = state_.exchange(state, std::memory_order_acq_rel);
    if (previous == state) return;
    LEC_INFO(kCategory, "state {} -> {}", toString(previous), toString(state));
}

Status PhoneCameraSource::listen() {
    if (server_.running()) return ok();
    auto started = server_.start(requestedPort_, hexToken());
    if (!started) return started;
    setState(PhoneCameraState::Waiting);
    return ok();
}

void PhoneCameraSource::unlisten() {
    if (!server_.running()) return;
    server_.stop();
    setState(PhoneCameraState::Idle);
}

std::string PhoneCameraSource::url() const { return server_.url(); }

std::string PhoneCameraSource::phoneStatus() const {
    std::lock_guard lock(mutex_);
    return phoneStatus_;
}

Status PhoneCameraSource::start(capture::IVideoFrameSink& sink) {
    if (running_.load(std::memory_order_acquire)) {
        return fail(ErrorCode::AlreadyExists, "phone camera source already running");
    }
    auto listening = listen();
    if (!listening) return listening;

    sink_ = &sink;
    running_.store(true, std::memory_order_release);
    captureThread_ = std::thread([this] { captureLoop(); });
    return ok();
}

void PhoneCameraSource::stop() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) return;
    frameAvailable_.notify_all();
    if (captureThread_.joinable()) captureThread_.join();
    sink_ = nullptr;
    unlisten();
}

capture::VideoSourceInfo PhoneCameraSource::info() const {
    capture::VideoSourceInfo out;
    out.name = "Phone camera";
    out.kind = capture::SourceKind::Phone;
    out.deviceId = std::string(kDeviceId);
    out.width = decoderWidth_.load(std::memory_order_relaxed);
    out.height = decoderHeight_.load(std::memory_order_relaxed);
    out.nominalFrameRate = FrameRate{targetFps_.load(std::memory_order_relaxed), 1};
    return out;
}

void PhoneCameraSource::captureLoop() {
    media::JpegDecoder decoder;
    std::uint64_t emitted = 0;

    while (running_.load(std::memory_order_acquire)) {
        std::vector<std::uint8_t> jpeg;
        std::int64_t hostTimeNs = 0;
        {
            std::unique_lock lock(mutex_);
            frameAvailable_.wait(lock, [this] { return haveFrame_ || !running_.load(std::memory_order_acquire); });
            if (!running_.load(std::memory_order_acquire)) return;
            jpeg = std::move(pending_);
            hostTimeNs = pendingHostTimeNs_;
            haveFrame_ = false;
        }
        if (jpeg.empty()) continue;

        auto decoded = decoder.decode(jpeg);
        if (!decoded) {
            // A malformed upload is the phone's problem, not a fatal one; log
            // and keep the source alive.
            LEC_WARN(kCategory, "dropping frame: {}", decoded.error().message());
            continue;
        }
        decoderWidth_.store(decoder.width(), std::memory_order_relaxed);
        decoderHeight_.store(decoder.height(), std::memory_order_relaxed);

        capture::CapturedVideoFrame frame;
        frame.frame = std::move(decoded).value();
        // Stamped on arrival by PhoneClockMapper, so decode time and upload
        // jitter do not shift the frame against the microphone.
        frame.hostTimeNs = hostTimeNs;
        frame.sequence = emitted++;
        if (sink_) sink_->onVideoFrame(std::move(frame));
    }
}

net::HttpResponse PhoneCameraSource::onGet(std::string_view path) {
    // Accept "/" and "/<token>" and "/<token>/"; the token is the secret.
    const auto slash = path.find('/', 1);
    const auto first = path.substr(1, slash == std::string_view::npos ? std::string_view::npos : slash - 1);
    if (first != hexToken()) return net::HttpResponse::notFound();
    if (slash == std::string_view::npos || path.substr(slash) == "/") {
        return net::HttpResponse::html(phonePageHtml());
    }
    return net::HttpResponse::notFound();
}

void PhoneCameraSource::onPost(std::string_view path, std::string_view query, std::span<const std::uint8_t> body) {
    const auto slash = path.find('/', 1);
    const auto first = path.substr(1, slash == std::string_view::npos ? std::string_view::npos : slash - 1);
    if (first != hexToken()) return;
    const auto route = slash == std::string_view::npos ? std::string_view() : path.substr(slash);

    if (route == "/frame") {
        // Fallback for browsers without WebSocket; `t=` carries capture time.
        double phoneTimeMs = -1;
        if (const auto at = query.find("t="); at != std::string_view::npos) {
            phoneTimeMs = std::strtod(std::string(query.substr(at + 2)).c_str(), nullptr);
        }
        acceptFrame(body, phoneTimeMs);
        return;
    }

    if (route == "/ctl") {
        std::lock_guard lock(mutex_);
        phoneStatus_.assign(query);
        LEC_INFO(kCategory, "phone status: {}", query);
        return;
    }

    if (route == "/hq") {
        LEC_INFO(kCategory, "ignoring high quality upload of {} bytes (relink is a later phase)", body.size());
        return;
    }
}

void PhoneCameraSource::acceptFrame(std::span<const std::uint8_t> jpeg, double phoneTimeMs) {
    if (jpeg.empty()) return;
    const std::int64_t arrivalNs = HostClock::now();
    {
        std::lock_guard lock(mutex_);
        pendingHostTimeNs_ = clock_.map(phoneTimeMs, arrivalNs);
        pending_.assign(jpeg.begin(), jpeg.end());
        haveFrame_ = true;
    }
    frameAvailable_.notify_one();
    framesReceived_.fetch_add(1, std::memory_order_relaxed);
    if (state_.load(std::memory_order_acquire) != PhoneCameraState::Live) {
        setState(PhoneCameraState::Live);
        if (sink_) sink_->onSourceEvent({capture::SourceEvent::Type::Started, "Phone camera connected"});
    }
}

bool PhoneCameraSource::acceptWebSocket(std::string_view path) {
    return path == "/" + std::string(hexToken()) + "/ws";
}

void PhoneCameraSource::onWebSocketMessage(std::string_view path, std::span<const std::uint8_t> data, bool binary) {
    if (!acceptWebSocket(path)) return;
    if (!binary) {
        // Text messages are heartbeats or status, in the /ctl query format.
        const std::string_view text(reinterpret_cast<const char*>(data.data()), data.size());
        if (text == "hb") return;
        std::lock_guard lock(mutex_);
        phoneStatus_.assign(text);
        return;
    }
    // Binary frame: 8-byte little-endian float64 capture time (ms), then JPEG.
    constexpr std::size_t kHeader = 8;
    if (data.size() <= kHeader) return;
    std::uint64_t bits = 0;
    for (std::size_t i = 0; i < kHeader; ++i) bits |= std::uint64_t{data[i]} << (8 * i);
    double phoneTimeMs = 0;
    std::memcpy(&phoneTimeMs, &bits, sizeof(phoneTimeMs));
    if (!std::isfinite(phoneTimeMs)) phoneTimeMs = -1;
    acceptFrame(data.subspan(kHeader), phoneTimeMs);
}

}  // namespace lectern::mobile