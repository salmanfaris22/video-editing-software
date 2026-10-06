#include "audio/AudioOutput.h"

#include "core/Thread.h"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

namespace lectern::audio {

namespace {

class NullAudioOutput final : public IAudioOutput {
public:
    ~NullAudioOutput() override { stop(); }

    Status start(RenderCallback render) override {
        stop();
        render_ = std::move(render);
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this] { run(); });
        return ok();
    }

    void stop() override {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        if (thread_.joinable()) thread_.join();
    }

    [[nodiscard]] Time latency() const override { return Time::zero(); }
    [[nodiscard]] std::string deviceName() const override { return "No audio device"; }

private:
    void run() {
        setCurrentThreadName("lectern.audio.null");
        constexpr int kBlock = kSampleRate / 100;  // 10 ms
        std::vector<float> buffer(static_cast<std::size_t>(kBlock) * kChannels);
        auto next = std::chrono::steady_clock::now();
        while (running_.load(std::memory_order_acquire)) {
            render_(buffer.data(), kBlock);
            next += std::chrono::milliseconds(10);
            std::this_thread::sleep_until(next);
        }
    }

    RenderCallback render_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}  // namespace

std::unique_ptr<IAudioOutput> makeNullAudioOutput() { return std::make_unique<NullAudioOutput>(); }

}  // namespace lectern::audio
