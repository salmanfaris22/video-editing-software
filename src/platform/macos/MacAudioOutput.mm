// CoreAudio playback for the editor preview: the default output unit (it
// follows the system's default device), fed 48 kHz stereo float.

#include "platform/macos/MacInternal.h"  // must be first (see header)

#import <AudioToolbox/AudioToolbox.h>
#import <CoreAudio/CoreAudio.h>

#include "core/Log.h"
#include "platform/AudioOutput.h"

#include <algorithm>

namespace lectern::platform::mac {

namespace {

class MacAudioOutput final : public audio::IAudioOutput {
public:
    ~MacAudioOutput() override { stop(); }

    Status start(RenderCallback render) override {
        stop();
        render_ = std::move(render);
        AudioComponentDescription desc{};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        AudioComponent component = AudioComponentFindNext(nullptr, &desc);
        if (!component) return fail(ErrorCode::NotFound, "no audio output device");
        if (OSStatus st = AudioComponentInstanceNew(component, &unit_); st != noErr) {
            unit_ = nullptr;
            return fail(ErrorCode::DeviceBusy, "open audio output (" + std::to_string(st) + ")");
        }
        AudioStreamBasicDescription format{};
        format.mSampleRate = kSampleRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
        format.mBytesPerPacket = sizeof(float) * kChannels;
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = sizeof(float) * kChannels;
        format.mChannelsPerFrame = kChannels;
        format.mBitsPerChannel = 32;
        AURenderCallbackStruct callback{&MacAudioOutput::renderProc, this};
        OSStatus st = AudioUnitSetProperty(unit_, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0, &format,
                                           sizeof format);
        if (st == noErr) {
            st = AudioUnitSetProperty(unit_, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input, 0, &callback,
                                      sizeof callback);
        }
        if (st == noErr) st = AudioUnitInitialize(unit_);
        if (st == noErr) {
            latency_ = queryLatency();
            name_ = queryName();
            st = AudioOutputUnitStart(unit_);
        }
        if (st != noErr) {
            AudioComponentInstanceDispose(unit_);
            unit_ = nullptr;
            return fail(ErrorCode::DeviceBusy, "start audio output (" + std::to_string(st) + ")");
        }
        return ok();
    }

    void stop() override {
        if (!unit_) return;
        AudioOutputUnitStop(unit_);  // synchronous: no render callback runs after this
        AudioUnitUninitialize(unit_);
        AudioComponentInstanceDispose(unit_);
        unit_ = nullptr;
    }

    [[nodiscard]] Time latency() const override { return latency_; }

    [[nodiscard]] std::string deviceName() const override { return name_; }

private:
    [[nodiscard]] std::string queryName() const {
        const AudioDeviceID device = currentDevice();
        if (!device) return "Default output";
        AudioObjectPropertyAddress address{kAudioObjectPropertyName, kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMain};
        CFStringRef name = nullptr;
        UInt32 size = sizeof name;
        if (AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &name) != noErr || !name) {
            return "Default output";
        }
        char buffer[256] = {};
        CFStringGetCString(name, buffer, sizeof buffer, kCFStringEncodingUTF8);
        CFRelease(name);
        return buffer;
    }

    static OSStatus renderProc(void* ref, AudioUnitRenderActionFlags*, const AudioTimeStamp*, UInt32, UInt32 frames,
                               AudioBufferList* io) {
        auto* self = static_cast<MacAudioOutput*>(ref);
        if (io && io->mNumberBuffers > 0 && io->mBuffers[0].mData) {
            self->render_(static_cast<float*>(io->mBuffers[0].mData), static_cast<int>(frames));
        }
        return noErr;
    }

    [[nodiscard]] AudioDeviceID currentDevice() const {
        AudioDeviceID device = 0;
        UInt32 size = sizeof device;
        if (!unit_ || AudioUnitGetProperty(unit_, kAudioOutputUnitProperty_CurrentDevice, kAudioUnitScope_Global, 0,
                                           &device, &size) != noErr) {
            return 0;
        }
        return device;
    }

    /// Device latency + safety offset + one I/O buffer, at the device rate.
    [[nodiscard]] Time queryLatency() const {
        const AudioDeviceID device = currentDevice();
        if (!device) return Time::fromMilliseconds(20);
        auto get = [device](AudioObjectPropertySelector selector, AudioObjectPropertyScope scope) -> UInt32 {
            AudioObjectPropertyAddress address{selector, scope, kAudioObjectPropertyElementMain};
            UInt32 value = 0;
            UInt32 size = sizeof value;
            return AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &value) == noErr ? value : 0;
        };
        const UInt32 frames = get(kAudioDevicePropertyLatency, kAudioObjectPropertyScopeOutput) +
                              get(kAudioDevicePropertySafetyOffset, kAudioObjectPropertyScopeOutput) +
                              get(kAudioDevicePropertyBufferFrameSize, kAudioObjectPropertyScopeGlobal);
        Float64 rate = kSampleRate;
        AudioObjectPropertyAddress address{kAudioDevicePropertyNominalSampleRate, kAudioObjectPropertyScopeGlobal,
                                           kAudioObjectPropertyElementMain};
        UInt32 size = sizeof rate;
        AudioObjectGetPropertyData(device, &address, 0, nullptr, &size, &rate);
        return Time::fromSecondsF(frames / std::max(1.0, static_cast<double>(rate)));
    }

    AudioComponentInstance unit_ = nullptr;
    RenderCallback render_;
    Time latency_ = Time::fromMilliseconds(20);
    std::string name_ = "Default output";
};

}  // namespace

}  // namespace lectern::platform::mac

namespace lectern::platform {
std::unique_ptr<audio::IAudioOutput> createAudioOutput() { return std::make_unique<mac::MacAudioOutput>(); }
}  // namespace lectern::platform
