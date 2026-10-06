#include "media/HardwareCapabilities.h"

#include <cstring>

namespace lectern::media {

namespace {

Status testEncode(const std::string& name, VideoCodec codec) {
    VideoEncoderConfig cfg;
    cfg.codec = codec;
    cfg.width = 640;
    cfg.height = 360;
    cfg.frameRate = FrameRate(30, 1);
    cfg.bitRate = 1'000'000;
    cfg.gopFrames = 30;
    cfg.inputFormat = AV_PIX_FMT_NV12;
    cfg.encoderNames = {name};
    auto enc = VideoEncoder::create(cfg);
    if (!enc) return fail(std::move(enc).error());
    auto frame = Frame::allocVideo(640, 360, AV_PIX_FMT_NV12);
    if (!frame) return fail(std::move(frame).error());
    // Mid-grey test frame.
    for (int y = 0; y < 360; ++y) std::memset((*frame)->data[0] + y * (*frame)->linesize[0], 128, 640);
    for (int y = 0; y < 180; ++y) std::memset((*frame)->data[1] + y * (*frame)->linesize[1], 128, 640);
    (*frame)->pts = 0;
    int packets = 0;
    auto sink = [&](Packet&&) -> Status {
        ++packets;
        return ok();
    };
    LEC_TRY((*enc)->encode(std::move(*frame), true, sink));
    LEC_TRY((*enc)->flush(sink));
    if (packets == 0) return fail(ErrorCode::EncoderError, "encoder produced no packets");
    return ok();
}

}  // namespace

HardwareCapabilities HardwareCapabilities::probe(bool verify) {
    HardwareCapabilities caps;
    for (VideoCodec codec : {VideoCodec::H264, VideoCodec::HEVC}) {
        for (const std::string& name : videoEncoderCandidates(codec, EncoderPreference::Auto)) {
            EncoderCapability cap;
            cap.name = name;
            cap.codec = codec;
            const AVCodec* c = avcodec_find_encoder_by_name(name.c_str());
            cap.compiledIn = c != nullptr;
            cap.hardware = c ? isHardwareEncoder(*c) : false;
            if (c && verify) {
                if (auto st = testEncode(name, codec); st) {
                    cap.verified = true;
                } else {
                    cap.error = st.error().message();
                }
            }
            caps.encoders.push_back(std::move(cap));
        }
    }
    AVHWDeviceType type = AV_HWDEVICE_TYPE_NONE;
    while ((type = av_hwdevice_iterate_types(type)) != AV_HWDEVICE_TYPE_NONE) {
        AVBufferRef* device = nullptr;
        if (av_hwdevice_ctx_create(&device, type, nullptr, nullptr, 0) >= 0) {
            caps.hwDeviceTypes.emplace_back(av_hwdevice_get_type_name(type));
            av_buffer_unref(&device);
        }
    }
    return caps;
}

bool HardwareCapabilities::hasHardwareEncoder(VideoCodec codec) const {
    for (const auto& e : encoders) {
        if (e.codec == codec && e.hardware && e.verified) return true;
    }
    return false;
}

std::string HardwareCapabilities::describe() const {
    std::string out;
    for (const auto& e : encoders) {
        out += e.name;
        out += e.hardware ? " [hw]" : " [sw]";
        out += !e.compiledIn ? " not built" : (e.verified ? " ok" : " unavailable");
        if (!e.error.empty()) out += " (" + e.error + ")";
        out += '\n';
    }
    out += "hwaccel devices:";
    for (const auto& d : hwDeviceTypes) out += " " + d;
    return out;
}

}  // namespace lectern::media
