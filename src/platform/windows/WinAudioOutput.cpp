// WASAPI playback for the editor preview: shared mode on the default render
// endpoint; Windows converts our 48 kHz stereo float to the mix format.
//
// Verification status: compile-checked with MinGW-w64; not yet run on Windows.

#include "platform/windows/WinCommon.h"

#include <audioclient.h>
#include <avrt.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include "core/Log.h"
#include "core/Thread.h"
#include "platform/AudioOutput.h"

#include <atomic>
#include <future>
#include <thread>

namespace lectern::platform {

namespace win {
namespace {

// KSDATAFORMAT_SUBTYPE_IEEE_FLOAT, declared here to avoid ksmedia's GUID macros.
constexpr GUID kSubtypeFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

class WasapiOutput final : public audio::IAudioOutput {
public:
    ~WasapiOutput() override { stop(); }

    Status start(RenderCallback render) override {
        stop();
        render_ = std::move(render);
        running_.store(true, std::memory_order_release);
        std::promise<Status> ready;
        auto result = ready.get_future();
        thread_ = std::thread([this, p = std::move(ready)]() mutable { run(std::move(p)); });
        Status st = result.get();
        if (!st) {
            running_.store(false, std::memory_order_release);
            thread_.join();
        }
        return st;
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (thread_.joinable()) thread_.join();  // the loop wakes at least every buffer period
    }

    [[nodiscard]] Time latency() const override { return latency_; }
    [[nodiscard]] std::string deviceName() const override { return name_; }

private:
    void run(std::promise<Status> ready) {
        setCurrentThreadName("lectern.wasapi.render");
        ComApartment com;
        DWORD taskIndex = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
        HANDLE event = nullptr;
        auto finish = [&](Status st, bool started) {
            if (!started) ready.set_value(std::move(st));
            if (event) CloseHandle(event);
            if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        };

        ComPtr<IMMDeviceEnumerator> enumerator;
        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        ComPtr<IMMDevice> device;
        if (SUCCEEDED(hr)) hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        if (FAILED(hr)) return finish(fail(hrError(hr, "no audio output device", ErrorCode::NotFound)), false);
        ComPtr<IAudioClient> client;
        hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()));
        if (FAILED(hr)) return finish(fail(hrError(hr, "activate audio output")), false);

        WAVEFORMATEXTENSIBLE format{};
        format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
        format.Format.nChannels = kChannels;
        format.Format.nSamplesPerSec = kSampleRate;
        format.Format.wBitsPerSample = 32;
        format.Format.nBlockAlign = kChannels * sizeof(float);
        format.Format.nAvgBytesPerSec = kSampleRate * format.Format.nBlockAlign;
        format.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX);
        format.Samples.wValidBitsPerSample = 32;
        format.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
        format.SubFormat = kSubtypeFloat;
        const DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
        hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 400'000 /* 40 ms */, 0, &format.Format, nullptr);
        if (FAILED(hr)) return finish(fail(hrError(hr, "initialize audio output")), false);
        event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        if (!event || FAILED(hr = client->SetEventHandle(event))) return finish(fail(hrError(hr, "audio output event")), false);
        UINT32 bufferFrames = 0;
        client->GetBufferSize(&bufferFrames);
        REFERENCE_TIME streamLatency = 0;
        client->GetStreamLatency(&streamLatency);
        latency_ = Time::fromNanoseconds(static_cast<std::int64_t>(streamLatency) * 100) +
                   Time::fromSamples(bufferFrames, kSampleRate);
        ComPtr<IAudioRenderClient> renderClient;
        if (FAILED(hr = client->GetService(IID_PPV_ARGS(&renderClient)))) {
            return finish(fail(hrError(hr, "audio render client")), false);
        }
        BYTE* data = nullptr;
        if (SUCCEEDED(renderClient->GetBuffer(bufferFrames, &data))) {
            renderClient->ReleaseBuffer(bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);  // start without a click
        }
        if (FAILED(hr = client->Start())) return finish(fail(hrError(hr, "start audio output")), false);
        ready.set_value(ok());

        while (running_.load(std::memory_order_acquire)) {
            WaitForSingleObject(event, 100);
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding))) break;  // device removed
            const UINT32 available = bufferFrames - padding;
            if (available == 0) continue;
            if (SUCCEEDED(renderClient->GetBuffer(available, &data))) {
                render_(reinterpret_cast<float*>(data), static_cast<int>(available));
                renderClient->ReleaseBuffer(available, 0);
            }
        }
        client->Stop();
        finish(ok(), true);
    }

    RenderCallback render_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    Time latency_ = Time::fromMilliseconds(40);
    std::string name_ = "Default output";
};

}  // namespace
}  // namespace win

std::unique_ptr<audio::IAudioOutput> createAudioOutput() { return std::make_unique<win::WasapiOutput>(); }

}  // namespace lectern::platform
