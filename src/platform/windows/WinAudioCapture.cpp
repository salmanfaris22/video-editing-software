// WASAPI microphone capture and system audio (loopback of the default render
// device). Event-driven shared-mode capture on a dedicated MMCSS "Pro Audio"
// thread; timestamps from u64QPCPosition (QPC, same domain as HostClock).
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinCommon.h"

#include <audioclient.h>
#include <avrt.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>

#include "capture/CaptureInterfaces.h"
#include "core/Clock.h"
#include "core/Log.h"
#include "core/Thread.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <future>
#include <thread>
#include <vector>

namespace lectern::platform::win {

using namespace capture;

namespace {

constexpr REFERENCE_TIME kBufferDuration = 200'000;  // 20 ms in 100 ns units

enum class SampleKind { Float32, Int16, Int24, Int32, Unsupported };

SampleKind sampleKind(const WAVEFORMATEX* f) {
    WORD tag = f->wFormatTag;
    if (tag == WAVE_FORMAT_EXTENSIBLE) {
        const auto* x = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(f);
        if (IsEqualGUID(x->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) tag = WAVE_FORMAT_IEEE_FLOAT;
        else if (IsEqualGUID(x->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) tag = WAVE_FORMAT_PCM;
    }
    if (tag == WAVE_FORMAT_IEEE_FLOAT && f->wBitsPerSample == 32) return SampleKind::Float32;
    if (tag == WAVE_FORMAT_PCM) {
        switch (f->wBitsPerSample) {
            case 16: return SampleKind::Int16;
            case 24: return SampleKind::Int24;
            case 32: return SampleKind::Int32;
            default: break;
        }
    }
    return SampleKind::Unsupported;
}

Result<ComPtr<IMMDeviceEnumerator>> enumerator() {
    ComPtr<IMMDeviceEnumerator> e;
    const HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e));
    if (FAILED(hr)) return fail(hrError(hr, "create MMDeviceEnumerator"));
    return e;
}

std::string deviceId(IMMDevice* d) {
    LPWSTR id = nullptr;
    std::string out;
    if (SUCCEEDED(d->GetId(&id)) && id) {
        out = toUtf8(id);
        CoTaskMemFree(id);
    }
    return out;
}

std::string deviceName(IMMDevice* d) {
    ComPtr<IPropertyStore> props;
    if (FAILED(d->OpenPropertyStore(STGM_READ, &props))) return {};
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string name;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = toUtf8(v.pwszVal);
    PropVariantClear(&v);
    return name;
}

Result<ComPtr<IMMDevice>> openDevice(const std::wstring& id, bool loopback) {
    auto e = enumerator();
    if (!e) return fail(std::move(e).error());
    ComPtr<IMMDevice> device;
    const HRESULT hr = id.empty() ? (*e)->GetDefaultAudioEndpoint(loopback ? eRender : eCapture, eConsole, &device)
                                  : (*e)->GetDevice(id.c_str(), &device);
    if (FAILED(hr)) return fail(hrError(hr, "open audio device", ErrorCode::NotFound));
    return device;
}

/// Mix format of a device (rate, channels) without starting a stream.
Status queryFormat(IMMDevice* device, int& rate, int& channels) {
    ComPtr<IAudioClient> client;
    HRESULT hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
    if (FAILED(hr)) return fail(hrError(hr, "activate audio client"));
    WAVEFORMATEX* mix = nullptr;
    hr = client->GetMixFormat(&mix);
    if (FAILED(hr) || !mix) return fail(hrError(hr, "query mix format"));
    rate = static_cast<int>(mix->nSamplesPerSec);
    channels = static_cast<int>(mix->nChannels);
    CoTaskMemFree(mix);
    return ok();
}

// ===========================================================================

class WasapiSource final : public IAudioSource {
public:
    WasapiSource(std::wstring deviceId, bool loopback, AudioSourceInfo info)
        : deviceId_(std::move(deviceId)), loopback_(loopback), info_(std::move(info)) {}
    ~WasapiSource() override { stop(); }

    Status start(IAudioSink& sink) override {
        sink_ = &sink;
        running_.store(true);
        std::promise<Status> ready;
        auto result = ready.get_future();
        thread_ = std::thread([this, p = std::move(ready)]() mutable { run(std::move(p)); });
        Status st = result.get();
        if (!st) {
            running_.store(false);
            thread_.join();
            return st;
        }
        sink.onSourceEvent({SourceEvent::Type::Started, info_.name});
        return ok();
    }

    void stop() override {
        if (!running_.exchange(false)) return;
        if (event_) SetEvent(event_);
        if (thread_.joinable()) thread_.join();
        if (sink_) sink_->onSourceEvent({SourceEvent::Type::Stopped, info_.name});
    }

    [[nodiscard]] AudioSourceInfo info() const override { return info_; }

private:
    void convert(const BYTE* data, UINT32 frames) {
        const std::size_t n = static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels_);
        if (buffer_.size() < n) buffer_.resize(n);
        switch (kind_) {
            case SampleKind::Float32:
                std::memcpy(buffer_.data(), data, n * sizeof(float));
                break;
            case SampleKind::Int16: {
                const auto* s = reinterpret_cast<const std::int16_t*>(data);
                for (std::size_t i = 0; i < n; ++i) buffer_[i] = static_cast<float>(s[i]) / 32768.0f;
                break;
            }
            case SampleKind::Int24:
                for (std::size_t i = 0; i < n; ++i) {
                    const BYTE* p = data + i * 3;
                    std::int32_t v = (p[0] << 8) | (p[1] << 16) | (p[2] << 24);  // sign-extend via top byte
                    buffer_[i] = static_cast<float>(v) / 2147483648.0f;
                }
                break;
            case SampleKind::Int32: {
                const auto* s = reinterpret_cast<const std::int32_t*>(data);
                for (std::size_t i = 0; i < n; ++i) buffer_[i] = static_cast<float>(static_cast<double>(s[i]) / 2147483648.0);
                break;
            }
            case SampleKind::Unsupported:
                std::fill_n(buffer_.begin(), n, 0.0f);
                break;
        }
    }

    void run(std::promise<Status> ready) {
        setCurrentThreadName(loopback_ ? "lectern.wasapi.loopback" : "lectern.wasapi.mic");
        ComApartment com;
        DWORD taskIndex = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);

        auto fail_ = [&](Error e) {
            ready.set_value(std::unexpected(std::move(e)));
            if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        };
        auto device = openDevice(deviceId_, loopback_);
        if (!device) return fail_(std::move(device).error());
        ComPtr<IAudioClient> client;
        HRESULT hr = (*device)->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                         reinterpret_cast<void**>(client.GetAddressOf()));
        if (FAILED(hr)) return fail_(hrError(hr, "activate audio client"));
        WAVEFORMATEX* mix = nullptr;
        if (FAILED(hr = client->GetMixFormat(&mix)) || !mix) return fail_(hrError(hr, "query mix format"));
        kind_ = sampleKind(mix);
        channels_ = mix->nChannels;
        rate_ = static_cast<int>(mix->nSamplesPerSec);
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | (loopback_ ? AUDCLNT_STREAMFLAGS_LOOPBACK : 0);
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, kBufferDuration, 0, mix, nullptr);
        CoTaskMemFree(mix);
        if (FAILED(hr)) return fail_(hrError(hr, "initialize audio stream"));
        if (kind_ == SampleKind::Unsupported) return fail_(Error(ErrorCode::Unsupported, "unsupported audio sample format"));

        event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event_ || FAILED(hr = client->SetEventHandle(event_))) return fail_(hrError(hr, "audio event handle"));
        ComPtr<IAudioCaptureClient> capture;
        if (FAILED(hr = client->GetService(IID_PPV_ARGS(&capture)))) return fail_(hrError(hr, "capture client"));
        if (FAILED(hr = client->Start())) return fail_(hrError(hr, "start audio stream"));
        buffer_.resize(static_cast<std::size_t>(rate_ / 10) * static_cast<std::size_t>(channels_));  // 100 ms headroom
        ready.set_value(ok());

        while (running_.load(std::memory_order_acquire)) {
            // The timeout keeps loopback working where its event is not signaled
            // (older Windows 10 builds signal it only while audio plays).
            WaitForSingleObject(event_, 10);
            UINT32 packet = 0;
            while (running_.load(std::memory_order_acquire) && SUCCEEDED(hr = capture->GetNextPacketSize(&packet)) &&
                   packet > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD bufferFlags = 0;
                UINT64 devicePosition = 0;
                UINT64 qpcPosition = 0;
                hr = capture->GetBuffer(&data, &frames, &bufferFlags, &devicePosition, &qpcPosition);
                if (FAILED(hr)) break;
                if (bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT) {
                    const std::size_t n = static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels_);
                    if (buffer_.size() < n) buffer_.resize(n);
                    std::fill_n(buffer_.begin(), n, 0.0f);
                } else {
                    convert(data, frames);
                }
                const std::int64_t host = (bufferFlags & AUDCLNT_BUFFERFLAGS_TIMESTAMP_ERROR)
                                              ? HostClock::now() - static_cast<std::int64_t>(frames) * 1'000'000'000 / rate_
                                              : qpc100nsToNs(qpcPosition);
                const std::uint32_t chunkFlags =
                    (bufferFlags & AUDCLNT_BUFFERFLAGS_DATA_DISCONTINUITY) ? AudioChunk::kDiscontinuity : 0;
                sink_->onAudio(AudioChunk{buffer_.data(), static_cast<int>(frames), channels_, rate_, host, chunkFlags});
                capture->ReleaseBuffer(frames);
            }
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED) {
                sink_->onSourceEvent({SourceEvent::Type::Interrupted, "audio device was removed"});
                break;
            }
        }
        client->Stop();
        CloseHandle(event_);
        event_ = nullptr;
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    }

    std::wstring deviceId_;
    bool loopback_;
    AudioSourceInfo info_;
    IAudioSink* sink_ = nullptr;
    std::atomic<bool> running_{false};
    std::thread thread_;
    HANDLE event_ = nullptr;
    SampleKind kind_ = SampleKind::Float32;
    int channels_ = 2;
    int rate_ = 48'000;
    std::vector<float> buffer_;
};

class WasapiBackend final : public IAudioCaptureBackend {
public:
    [[nodiscard]] std::string name() const override { return "WASAPI"; }

    Result<std::vector<AudioDeviceInfo>> enumerateInputs() override {
        ComApartment com;
        auto e = enumerator();
        if (!e) return fail(std::move(e).error());
        std::string defaultId;
        ComPtr<IMMDevice> def;
        if (SUCCEEDED((*e)->GetDefaultAudioEndpoint(eCapture, eConsole, &def))) defaultId = deviceId(def.Get());
        ComPtr<IMMDeviceCollection> collection;
        HRESULT hr = (*e)->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &collection);
        if (FAILED(hr)) return fail(hrError(hr, "enumerate audio inputs"));
        UINT count = 0;
        collection->GetCount(&count);
        std::vector<AudioDeviceInfo> out;
        for (UINT i = 0; i < count; ++i) {
            ComPtr<IMMDevice> d;
            if (FAILED(collection->Item(i, &d))) continue;
            AudioDeviceInfo info;
            info.id = deviceId(d.Get());
            info.name = deviceName(d.Get());
            (void)queryFormat(d.Get(), info.sampleRate, info.channels);
            info.isDefault = info.id == defaultId;
            info.transport = "wasapi";
            out.push_back(std::move(info));
        }
        return out;
    }

    Result<std::unique_ptr<IAudioSource>> createMicrophoneSource(const AudioCaptureConfig& config) override {
        return create(config.deviceId, false);
    }

    [[nodiscard]] bool supportsSystemAudio() const override { return true; }

    Result<std::unique_ptr<IAudioSource>> createSystemAudioSource(const AudioCaptureConfig&) override {
        // Note: loopback includes this app's own output; per-process exclusion
        // needs the process-loopback API (Windows 10 2004+) — planned.
        return create({}, true);
    }

private:
    Result<std::unique_ptr<IAudioSource>> create(const std::string& id, bool loopback) {
        ComApartment com;
        auto device = openDevice(toWide(id), loopback);
        if (!device) return fail(std::move(device).error());
        AudioSourceInfo info;
        info.kind = loopback ? SourceKind::SystemAudio : SourceKind::Microphone;
        info.name = loopback ? std::string("System Audio") : deviceName(device->Get());
        info.deviceId = loopback ? std::string("system-audio") : deviceId(device->Get());
        LEC_TRY(queryFormat(device->Get(), info.sampleRate, info.channels));
        return std::unique_ptr<IAudioSource>(
            std::make_unique<WasapiSource>(loopback ? std::wstring() : toWide(info.deviceId), loopback, info));
    }
};

}  // namespace

std::unique_ptr<IAudioCaptureBackend> createAudioBackend() { return std::make_unique<WasapiBackend>(); }

}  // namespace lectern::platform::win
