// Core Audio HAL microphone capture and device enumeration. System audio is
// delegated to ScreenCaptureKit (MacScreenCapture.mm).

#include "platform/macos/MacInternal.h"  // must be first (see header)

#import <AVFoundation/AVFoundation.h>
#import <CoreAudio/CoreAudio.h>

#include "core/Clock.h"
#include "core/Log.h"

#include <atomic>
#include <cstring>
#include <vector>

using namespace lectern;
using namespace lectern::capture;

namespace lectern::platform::mac {

namespace {

constexpr int kMaxChannels = 8;
constexpr int kMaxFrames = 16384;

AudioObjectPropertyAddress address(AudioObjectPropertySelector selector,
                                   AudioObjectPropertyScope scope = kAudioObjectPropertyScopeGlobal) {
    return AudioObjectPropertyAddress{selector, scope, kAudioObjectPropertyElementMain};
}

template <class T>
bool getProperty(AudioObjectID object, const AudioObjectPropertyAddress& addr, T& out) {
    UInt32 size = sizeof(T);
    return AudioObjectGetPropertyData(object, &addr, 0, nullptr, &size, &out) == noErr;
}

std::string cfString(AudioObjectID object, AudioObjectPropertySelector selector) {
    CFStringRef value = nullptr;
    const auto addr = address(selector);
    UInt32 size = sizeof(value);
    if (AudioObjectGetPropertyData(object, &addr, 0, nullptr, &size, &value) != noErr || !value) return {};
    NSString* s = (__bridge_transfer NSString*)value;
    return std::string((s.UTF8String ? s.UTF8String : ""));
}

std::vector<AudioObjectID> allDevices() {
    const auto addr = address(kAudioHardwarePropertyDevices);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &addr, 0, nullptr, &size) != noErr) return {};
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &addr, 0, nullptr, &size, ids.data()) != noErr) return {};
    return ids;
}

int inputChannels(AudioObjectID device) {
    const auto addr = address(kAudioDevicePropertyStreamConfiguration, kAudioObjectPropertyScopeInput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &addr, 0, nullptr, &size) != noErr || size == 0) return 0;
    std::vector<std::uint8_t> storage(size);
    auto* list = reinterpret_cast<AudioBufferList*>(storage.data());
    if (AudioObjectGetPropertyData(device, &addr, 0, nullptr, &size, list) != noErr) return 0;
    int channels = 0;
    for (UInt32 i = 0; i < list->mNumberBuffers; ++i) channels += static_cast<int>(list->mBuffers[i].mNumberChannels);
    return channels;
}

AudioObjectID defaultInputDevice() {
    AudioObjectID id = kAudioObjectUnknown;
    getProperty(kAudioObjectSystemObject, address(kAudioHardwarePropertyDefaultInputDevice), id);
    return id;
}

std::string transportName(AudioObjectID device) {
    UInt32 t = 0;
    if (!getProperty(device, address(kAudioDevicePropertyTransportType), t)) return "unknown";
    switch (t) {
        case kAudioDeviceTransportTypeBuiltIn: return "built-in";
        case kAudioDeviceTransportTypeUSB: return "usb";
        case kAudioDeviceTransportTypeBluetooth:
        case kAudioDeviceTransportTypeBluetoothLE: return "bluetooth";
        case kAudioDeviceTransportTypeVirtual: return "virtual";
        case kAudioDeviceTransportTypeAggregate: return "aggregate";
        case kAudioDeviceTransportTypeThunderbolt: return "thunderbolt";
        case kAudioDeviceTransportTypeContinuityCaptureWired:
        case kAudioDeviceTransportTypeContinuityCaptureWireless: return "continuity";
        default: return "other";
    }
}

AudioObjectID findDevice(const std::string& uid) {
    if (uid.empty() || uid == "default") return defaultInputDevice();
    for (AudioObjectID id : allDevices()) {
        if (cfString(id, kAudioDevicePropertyDeviceUID) == uid) return id;
    }
    return kAudioObjectUnknown;
}

/// Input latency in frames: device + first input stream (what Core Audio
/// reports between the ADC and the IOProc timestamp).
UInt32 inputLatencyFrames(AudioObjectID device) {
    UInt32 deviceLatency = 0;
    getProperty(device, address(kAudioDevicePropertyLatency, kAudioObjectPropertyScopeInput), deviceLatency);
    UInt32 streamLatency = 0;
    const auto streamsAddr = address(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &streamsAddr, 0, nullptr, &size) == noErr && size >= sizeof(AudioStreamID)) {
        std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
        if (AudioObjectGetPropertyData(device, &streamsAddr, 0, nullptr, &size, streams.data()) == noErr) {
            getProperty(streams[0], address(kAudioStreamPropertyLatency), streamLatency);
        }
    }
    return deviceLatency + streamLatency;
}

bool inputIsFloat32(AudioObjectID device) {
    const auto streamsAddr = address(kAudioDevicePropertyStreams, kAudioObjectPropertyScopeInput);
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(device, &streamsAddr, 0, nullptr, &size) != noErr || size < sizeof(AudioStreamID)) {
        return false;
    }
    std::vector<AudioStreamID> streams(size / sizeof(AudioStreamID));
    if (AudioObjectGetPropertyData(device, &streamsAddr, 0, nullptr, &size, streams.data()) != noErr) return false;
    for (AudioStreamID s : streams) {
        AudioStreamBasicDescription asbd{};
        if (!getProperty(s, address(kAudioStreamPropertyVirtualFormat), asbd)) return false;
        if (asbd.mFormatID != kAudioFormatLinearPCM || !(asbd.mFormatFlags & kAudioFormatFlagIsFloat) ||
            asbd.mBitsPerChannel != 32) {
            return false;
        }
    }
    return true;
}

// ===========================================================================

class MacMicrophoneSource final : public IAudioSource {
public:
    MacMicrophoneSource(AudioObjectID device, AudioSourceInfo info) : device_(device), info_(std::move(info)) {
        interleaved_.resize(static_cast<std::size_t>(kMaxFrames * kMaxChannels));
    }
    ~MacMicrophoneSource() override { stop(); }

    Status start(IAudioSink& sink) override {
        sink_ = &sink;
        OSStatus st = AudioDeviceCreateIOProcID(device_, &MacMicrophoneSource::ioProc, this, &procId_);
        if (st != noErr) return fail(ErrorCode::DeviceBusy, "create IOProc", st);
        aliveAddr_ = address(kAudioDevicePropertyDeviceIsAlive);
        auto* source = this;
        aliveListener_ = ^(UInt32, const AudioObjectPropertyAddress*) {
          UInt32 alive = 1;
          getProperty(source->device_, address(kAudioDevicePropertyDeviceIsAlive), alive);
          if (!alive && source->running_.load()) {
              source->sink_->onSourceEvent({SourceEvent::Type::Interrupted, "microphone disconnected"});
          }
        };
        listenerQueue_ = dispatch_queue_create("lectern.capture.mic.listener", DISPATCH_QUEUE_SERIAL);
        AudioObjectAddPropertyListenerBlock(device_, &aliveAddr_, listenerQueue_, aliveListener_);

        running_.store(true, std::memory_order_release);
        st = AudioDeviceStart(device_, procId_);
        if (st != noErr) {
            running_.store(false, std::memory_order_release);
            AudioObjectRemovePropertyListenerBlock(device_, &aliveAddr_, listenerQueue_, aliveListener_);
            AudioDeviceDestroyIOProcID(device_, procId_);
            procId_ = nullptr;
            return fail(ErrorCode::DeviceBusy, "start microphone", st);
        }
        sink.onSourceEvent({SourceEvent::Type::Started, info_.name});
        return ok();
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        // AudioDeviceStop is synchronous when called off the IO thread: the
        // IOProc is not running once it returns.
        AudioDeviceStop(device_, procId_);
        AudioDeviceDestroyIOProcID(device_, procId_);
        procId_ = nullptr;
        AudioObjectRemovePropertyListenerBlock(device_, &aliveAddr_, listenerQueue_, aliveListener_);
        if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, info_.name});
    }

    [[nodiscard]] AudioSourceInfo info() const override { return info_; }

private:
    // Real-time thread: copy/interleave into a preallocated buffer, hand off.
    static OSStatus ioProc(AudioObjectID, const AudioTimeStamp*, const AudioBufferList* input,
                           const AudioTimeStamp* inputTime, AudioBufferList*, const AudioTimeStamp*, void* client) {
        auto* self = static_cast<MacMicrophoneSource*>(client);
        if (!self->running_.load(std::memory_order_acquire) || !input || input->mNumberBuffers == 0) return noErr;

        int channels = 0;
        for (UInt32 b = 0; b < input->mNumberBuffers; ++b) channels += static_cast<int>(input->mBuffers[b].mNumberChannels);
        if (channels <= 0 || channels > kMaxChannels) return noErr;
        const UInt32 frames = input->mBuffers[0].mDataByteSize /
                              (sizeof(float) * std::max<UInt32>(1, input->mBuffers[0].mNumberChannels));
        if (frames == 0 || frames > static_cast<UInt32>(kMaxFrames)) return noErr;

        float* dst = self->interleaved_.data();
        if (input->mNumberBuffers == 1) {
            std::memcpy(dst, input->mBuffers[0].mData, frames * static_cast<UInt32>(channels) * sizeof(float));
        } else {
            int channelOffset = 0;
            for (UInt32 b = 0; b < input->mNumberBuffers; ++b) {
                const AudioBuffer& buf = input->mBuffers[b];
                const auto* src = static_cast<const float*>(buf.mData);
                const int bc = static_cast<int>(buf.mNumberChannels);
                for (UInt32 i = 0; i < frames; ++i) {
                    for (int c = 0; c < bc; ++c) dst[i * static_cast<UInt32>(channels) + static_cast<UInt32>(channelOffset + c)] = src[i * static_cast<UInt32>(bc) + static_cast<UInt32>(c)];
                }
                channelOffset += bc;
            }
        }
        std::int64_t host = (inputTime && (inputTime->mFlags & kAudioTimeStampHostTimeValid))
                                ? HostClock::nativeTicksToNs(inputTime->mHostTime)
                                : HostClock::now();
        host -= self->info_.latencyCompensationNs;
        self->sink_->onAudio(AudioChunk{dst, static_cast<int>(frames), channels, self->info_.sampleRate, host, 0});
        return noErr;
    }

    AudioObjectID device_;
    AudioSourceInfo info_;
    IAudioSink* sink_ = nullptr;
    AudioDeviceIOProcID procId_ = nullptr;
    std::atomic<bool> running_{false};
    std::vector<float> interleaved_;
    AudioObjectPropertyAddress aliveAddr_{};
    AudioObjectPropertyListenerBlock aliveListener_ = nil;
    dispatch_queue_t listenerQueue_ = nil;
};

class MacAudioBackend final : public IAudioCaptureBackend {
public:
    [[nodiscard]] std::string name() const override { return "CoreAudio"; }

    Result<std::vector<AudioDeviceInfo>> enumerateInputs() override {
        std::vector<AudioDeviceInfo> out;
        const AudioObjectID def = defaultInputDevice();
        for (AudioObjectID id : allDevices()) {
            const int channels = inputChannels(id);
            if (channels <= 0) continue;
            AudioDeviceInfo info;
            info.id = cfString(id, kAudioDevicePropertyDeviceUID);
            info.name = cfString(id, kAudioObjectPropertyName);
            info.channels = channels;
            Float64 rate = 0;
            getProperty(id, address(kAudioDevicePropertyNominalSampleRate), rate);
            info.sampleRate = static_cast<int>(rate);
            info.isDefault = id == def;
            info.transport = transportName(id);
            if (info.transport == "aggregate" && info.name.find("CADefaultDeviceAggregate") != std::string::npos) continue;
            out.push_back(std::move(info));
        }
        return out;
    }

    Result<std::unique_ptr<IAudioSource>> createMicrophoneSource(const AudioCaptureConfig& config) override {
        if ([AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] != AVAuthorizationStatusAuthorized) {
            // Without permission Core Audio silently delivers zeros; fail loudly instead.
            return fail(ErrorCode::PermissionDenied, "microphone access has not been granted");
        }
        const AudioObjectID device = findDevice(config.deviceId);
        if (device == kAudioObjectUnknown) return fail(ErrorCode::NotFound, "microphone '" + config.deviceId + "' not found");
        if (!inputIsFloat32(device)) return fail(ErrorCode::Unsupported, "microphone stream format is not 32-bit float");
        Float64 rate = 48'000;
        getProperty(device, address(kAudioDevicePropertyNominalSampleRate), rate);
        const int channels = std::min(inputChannels(device), kMaxChannels);
        const UInt32 latency = inputLatencyFrames(device);
        AudioSourceInfo info;
        info.name = cfString(device, kAudioObjectPropertyName);
        info.kind = SourceKind::Microphone;
        info.deviceId = cfString(device, kAudioDevicePropertyDeviceUID);
        info.sampleRate = static_cast<int>(rate);
        info.channels = channels;
        info.latencyCompensationNs = rate > 0 ? static_cast<std::int64_t>(latency * 1e9 / rate) : 0;
        LEC_INFO("platform", "microphone '{}' ({}) {} Hz x{}, input latency {} frames", info.name,
                 transportName(device), info.sampleRate, channels, latency);
        return std::unique_ptr<IAudioSource>(std::make_unique<MacMicrophoneSource>(device, std::move(info)));
    }

    [[nodiscard]] bool supportsSystemAudio() const override { return true; }

    Result<std::unique_ptr<IAudioSource>> createSystemAudioSource(const AudioCaptureConfig& config) override {
        return mac::createSystemAudioSource(config);
    }
};

}  // namespace

std::unique_ptr<IAudioCaptureBackend> createAudioBackend() { return std::make_unique<MacAudioBackend>(); }

}  // namespace lectern::platform::mac
