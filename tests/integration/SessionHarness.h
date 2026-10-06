#pragma once

// Deterministic end-to-end harness: synthetic sources driven by a ManualClock
// feed a real RecordingSession (encoders, muxers, files, manifest).

#include "capture/RecordingSession.h"
#include "capture/synthetic/SyntheticSources.h"
#include "media/FFmpeg.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <algorithm>

namespace lectern::test::session {

using namespace lectern::capture;

constexpr std::int64_t kSec = 1'000'000'000;
constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t kOrigin = 1000 * kSec;  // sources' timelines start on a whole second

struct Harness {
    ManualClock clock{kOrigin};
    test::TempDir dir{"lectern-session"};
    SyntheticVideoSource* video = nullptr;
    SyntheticAudioSource* audio = nullptr;
    std::shared_ptr<LiveVideoSource> liveVideo;
    std::shared_ptr<LiveAudioSource> liveAudio;
    std::unique_ptr<RecordingSession> session;
    std::int64_t audioStartNs = kOrigin;  // audio device starts delivering at this host time

    void setup(SyntheticVideoOptions vo, SyntheticAudioOptions ao, DiskSpaceMonitor::Query diskQuery = {}) {
        media::initializeFFmpeg(LogLevel::Error);
        vo.realtime = false;
        vo.clock = &clock;
        ao.realtime = false;
        ao.clock = &clock;
        auto v = std::make_unique<SyntheticVideoSource>(vo);
        video = v.get();
        auto a = std::make_unique<SyntheticAudioSource>(ao);
        audio = a.get();
        liveVideo = LiveVideoSource::start(std::move(v)).value();
        LiveAudioSource::Options lao;
        lao.blockWhenFull = true;
        liveAudio = LiveAudioSource::start(std::move(a), lao).value();

        SessionConfig cfg;
        cfg.title = "Synthetic lesson";
        cfg.projectDir = dir / "project.lectern";
        cfg.registryDir = dir / "registry";
        cfg.clock = &clock;
        cfg.diskQuery = diskQuery ? diskQuery : [](const std::filesystem::path&) -> Result<fs::DiskSpace> {
            return fs::DiskSpace{1ull << 40, 1ull << 40, 1ull << 40};
        };
        TrackPlan screen;
        screen.trackId = "screen";
        screen.role = TrackRole::Screen;
        screen.videoSource = liveVideo;
        screen.video.frameRate = vo.frameRate;
        screen.video.encoder = media::EncoderPreference::SoftwareOnly;
        screen.video.bitRate = 1'000'000;
        screen.video.blockWhenFull = true;
        TrackPlan mic;
        mic.trackId = "microphone";
        mic.role = TrackRole::Microphone;
        mic.audioSource = liveAudio;
        cfg.tracks = {screen, mic};
        session = RecordingSession::create(std::move(cfg)).value();
    }

    void produce() {
        const std::int64_t now = clock.nowNs();
        video->produceUntil(now, kOrigin);
        if (now >= audioStartNs) audio->produceUntil(now, audioStartNs);
        liveAudio->waitUntilDrained(std::chrono::seconds(5));
    }

    void advanceTo(std::int64_t hostNs, std::int64_t step = 10 * kMs) {
        while (clock.nowNs() < hostNs) {
            clock.set(std::min(hostNs, clock.nowNs() + step));
            produce();
        }
    }

    SessionResult finish() {
        auto r = session->waitForCompletion(std::chrono::seconds(60));
        EXPECT_TRUE(r) << (r ? "" : r.error().toString());
        return r ? *r : SessionResult{};
    }

    std::filesystem::path file(const SessionResult& r, const std::string& id) const {
        for (const auto& t : r.manifest.tracks) {
            if (t.id == id) return r.projectDir / t.file;
        }
        return {};
    }
};

}  // namespace lectern::test::session
