#include "editor/EditorFixture.h"

#include "editor/RenderPlan.h"
#include "timeline/EditOps.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

namespace lectern::test {

namespace {

project::MediaSource videoMedia(const std::string& name, project::MediaRole role, const std::string& path, int w, int h,
                                double seconds) {
    project::MediaSource m;
    m.id = project::MediaId::generate();
    m.kind = project::MediaKind::Video;
    m.role = role;
    m.name = name;
    m.path = path;
    m.info.container = "matroska,webm";
    m.info.duration = Time::fromSecondsF(seconds);
    m.info.video = project::VideoMetadata{"h264", w, h, FrameRate(30, 1), "nv12", "bt709", "tv", 0, false};
    return m;
}

}  // namespace

EditorFixture::EditorFixture() : EditorFixture(Options{}) {}

EditorFixture::EditorFixture(const Options& o) {
    project = project::Project::createEmpty("Editor test");
    project.canvas.width = 1280;
    project.canvas.height = 720;
    const timeline::LinkGroupId group = timeline::LinkGroupId::generate();
    const Time duration = Time::fromSecondsF(o.seconds);

    auto addClip = [&](const project::MediaSource& m, timeline::TrackKind kind) {
        timeline::Track t{timeline::TrackId::generate(), kind, m.name};
        timeline::Clip c;
        c.id = timeline::ClipId::generate();
        c.name = m.name;
        c.media = m.id;
        c.range = {Time::zero(), duration};
        c.linkGroup = group;
        EXPECT_TRUE(t.insertClip(c));
        project.timeline.tracks.push_back(t);
        project.media.push_back(m);
        return c.id;
    };

    EXPECT_TRUE(writeTestVideo(dir / "screen.mkv",
                               {.seconds = o.seconds, .width = 640, .height = 360, .u = kScreenU, .v = kScreenV}));
    const auto s = videoMedia("Screen", project::MediaRole::Screen, "screen.mkv", 640, 360, o.seconds);
    screen = s.id;
    screenClip = addClip(s, timeline::TrackKind::Video);
    if (o.camera) {
        EXPECT_TRUE(writeTestVideo(dir / "camera.mkv",
                                   {.seconds = o.seconds, .width = 320, .height = 180, .u = kCameraU, .v = kCameraV}));
        const auto c = videoMedia("Camera", project::MediaRole::Camera, "camera.mkv", 320, 180, o.seconds);
        camera = c.id;
        cameraClip = addClip(c, timeline::TrackKind::Video);
    }
    EXPECT_TRUE(writeTestAudio(dir / "mic.mkv", {.seconds = o.seconds, .channels = 1, .envelope = o.micEnvelope}));
    project::MediaSource m;
    m.id = mic = project::MediaId::generate();
    m.kind = project::MediaKind::Audio;
    m.role = project::MediaRole::Microphone;
    m.name = "Microphone";
    m.path = "mic.mkv";
    m.info.duration = duration;
    m.info.audio = project::AudioMetadata{"flac", 48'000, 1};
    micClip = addClip(m, timeline::TrackKind::Audio);
    timeline::edit::setLayoutAll(project.timeline, o.camera ? "pip.bottom-right.rounded" : "screen.only");
    EXPECT_TRUE(project.validate());
}

timeline::Track& EditorFixture::track(const std::string& name) {
    for (auto& t : project.timeline.tracks) {
        if (t.name == name) return t;
    }
    throw std::runtime_error("no track " + name);
}

timeline::ClipId EditorFixture::addAudioTrack(const std::string& name, const TestAudioSpec& spec) {
    const std::string file = name + ".mkv";
    EXPECT_TRUE(writeTestAudio(dir / file, spec));
    project::MediaSource m;
    m.id = project::MediaId::generate();
    m.kind = project::MediaKind::Audio;
    m.role = project::MediaRole::Imported;
    m.name = name;
    m.path = file;
    m.info.duration = Time::fromSecondsF(spec.seconds);
    m.info.audio = project::AudioMetadata{"flac", spec.sampleRate, spec.channels};
    timeline::Track t{timeline::TrackId::generate(), timeline::TrackKind::Audio, name};
    timeline::Clip c;
    c.id = timeline::ClipId::generate();
    c.name = name;
    c.media = m.id;
    c.range = {Time::zero(), m.info.duration};
    EXPECT_TRUE(t.insertClip(c));
    project.media.push_back(m);
    project.timeline.tracks.push_back(t);
    return c.id;
}

timeline::ClipId EditorFixture::addText(const std::string& text, double start, double duration, const std::string& preset) {
    auto it = std::find_if(project.timeline.tracks.begin(), project.timeline.tracks.end(),
                           [](const auto& t) { return t.kind == timeline::TrackKind::Overlay && t.name == "Text"; });
    if (it == project.timeline.tracks.end()) {
        project.timeline.tracks.push_back({timeline::TrackId::generate(), timeline::TrackKind::Overlay, "Text"});
        it = std::prev(project.timeline.tracks.end());
    }
    const auto defaults = editor::textPresetDefaults(preset);
    timeline::Clip c;
    c.id = timeline::ClipId::generate();
    c.kind = timeline::ClipKind::Text;
    c.name = "Text";
    c.range = {Time::fromSecondsF(start), Time::fromSecondsF(duration)};
    c.text = timeline::TextContent{text, preset, defaults.style};
    c.transform.position = timeline::Vec2{defaults.x, defaults.y};
    EXPECT_TRUE(it->insertClip(c));
    return c.id;
}

QColor yuv709(int y, int u, int v) {
    const double c = 1.164 * (y - 16);
    const double d = u - 128;
    const double e = v - 128;
    auto clamp = [](double x) { return std::clamp(static_cast<int>(std::lround(x)), 0, 255); };
    return QColor(clamp(c + 1.793 * e), clamp(c - 0.213 * d - 0.533 * e), clamp(c + 2.112 * d));
}

bool near(QColor a, QColor b, int tolerance) {
    return std::abs(a.red() - b.red()) <= tolerance && std::abs(a.green() - b.green()) <= tolerance &&
           std::abs(a.blue() - b.blue()) <= tolerance;
}

}  // namespace lectern::test
