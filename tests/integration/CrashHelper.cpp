// Records synthetic sources in real time until killed. Used by RecoveryTest
// to produce a genuinely crashed (SIGKILLed) recording.
//
//   lectern_crash_helper <projectDir> <registryDir>

#include "capture/RecordingSession.h"
#include "capture/synthetic/SyntheticSources.h"
#include "media/FFmpeg.h"

#include <cstdio>
#include <thread>

using namespace lectern;
using namespace lectern::capture;

int main(int argc, char** argv) {
    if (argc < 3) {
        std::fprintf(stderr, "usage: %s <projectDir> <registryDir>\n", argv[0]);
        return 2;
    }
    Logger::instance().addSink(makeStderrSink());
    Logger::instance().setLevel(LogLevel::Warn);
    media::initializeFFmpeg(LogLevel::Error);

    SyntheticVideoOptions vo;
    vo.width = 320;
    vo.height = 180;
    vo.frameRate = FrameRate(30, 1);
    auto video = LiveVideoSource::start(std::make_unique<SyntheticVideoSource>(vo));
    SyntheticAudioOptions ao;
    auto audio = LiveAudioSource::start(std::make_unique<SyntheticAudioSource>(ao));
    if (!video || !audio) return 3;

    SessionConfig cfg;
    cfg.title = "Crash test";
    cfg.projectDir = argv[1];
    cfg.registryDir = argv[2];
    cfg.checkpointInterval = std::chrono::milliseconds(500);
    TrackPlan screen;
    screen.trackId = "screen";
    screen.role = TrackRole::Screen;
    screen.videoSource = *video;
    screen.video.encoder = media::EncoderPreference::SoftwareOnly;
    screen.video.bitRate = 1'000'000;
    TrackPlan mic;
    mic.trackId = "microphone";
    mic.role = TrackRole::Microphone;
    mic.audioSource = *audio;
    cfg.tracks = {screen, mic};

    auto session = RecordingSession::create(std::move(cfg));
    if (!session || !(*session)->start()) return 4;
    std::printf("RECORDING %s\n", (*session)->sessionId().c_str());
    std::fflush(stdout);
    std::this_thread::sleep_for(std::chrono::seconds(60));  // the test kills us long before this
    (void)(*session)->stop();
    (void)(*session)->waitForCompletion(std::chrono::seconds(10));
    return 0;
}
