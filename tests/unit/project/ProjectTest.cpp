#include "core/FileSystem.h"
#include "project/ProjectJson.h"
#include "project/ProjectStore.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <fstream>

using namespace lectern;
using namespace lectern::project;

namespace {
Project sampleProject() {
    Project p = Project::createEmpty("Lesson 01");
    MediaSource m;
    m.id = MediaId::generate();
    m.kind = MediaKind::Video;
    m.role = MediaRole::Screen;
    m.name = "Screen";
    m.path = "media/screen/screen-abc.mkv";
    m.fingerprint = {123, "fnv1a64:0011223344556677"};
    m.info.container = "matroska,webm";
    m.info.duration = Time::fromSeconds(60);
    m.info.video = VideoMetadata{"h264", 1920, 1080, FrameRate(30, 1), "nv12", "bt709", "tv", 0, false};
    m.recording = RecordingRef{"session", "screen"};
    p.media.push_back(m);
    timeline::Track t;
    t.id = timeline::TrackId::generate();
    timeline::Clip c;
    c.id = timeline::ClipId::generate();
    c.media = m.id;
    c.range = {Time::zero(), Time::fromSeconds(60)};
    t.clips.push_back(c);
    p.timeline.tracks.push_back(t);
    p.recordings.push_back({"session", "recordings/session/session.json", "",
                            "2026-10-04T12:00:00Z", Time::fromSeconds(60), "completed"});
    p.exportSettings.frameRate = FrameRate(60, 1);
    return p;
}
}  // namespace

TEST(ProjectJson, RoundTrip) {
    const Project p = sampleProject();
    ASSERT_TRUE(p.validate());
    auto parsed = projectFromJson(json::parse(json::dump(toJson(p))).value());
    ASSERT_TRUE(parsed) << parsed.error().toString();
    EXPECT_EQ(*parsed, p);
}

TEST(ProjectJson, RoundTripsStyleAndTrackGain) {
    Project p = sampleProject();
    p.style.backgroundColor2 = "#3A1C71";
    p.style.screenPadding = 0.06;
    p.style.screenRadius = 0.02;
    p.style.screenShadow = 0.5;
    p.style.cameraShape = "circle";
    p.style.cameraBorder = 0.004;
    p.style.cameraMirror = true;
    p.style.subtitleSize = 0.05;
    p.timeline.tracks.front().gainDb = -6.5;
    auto parsed = projectFromJson(json::parse(json::dump(toJson(p))).value());
    ASSERT_TRUE(parsed) << parsed.error().toString();
    EXPECT_EQ(parsed->style, p.style);
    EXPECT_DOUBLE_EQ(parsed->timeline.tracks.front().gainDb, -6.5);
}

TEST(ProjectJson, MigratesV1CameraPlacementToLayoutRelativeTransform) {
    Project p = sampleProject();
    MediaSource cam;
    cam.id = MediaId::generate();
    cam.role = MediaRole::Camera;
    cam.name = "Camera";
    cam.path = "media/camera/camera.mkv";
    p.media.push_back(cam);
    timeline::Track t;
    t.id = timeline::TrackId::generate();
    timeline::Clip legacy;
    legacy.id = timeline::ClipId::generate();
    legacy.media = cam.id;
    legacy.range = {Time::zero(), Time::fromSeconds(10)};
    legacy.transform.position = timeline::Vec2{0.84, 0.80};  // the v1 importer's absolute placement
    legacy.transform.scale = timeline::Vec2{0.28, 0.28};
    t.clips.push_back(legacy);
    timeline::Clip moved = legacy;  // a user-chosen placement must survive
    moved.id = timeline::ClipId::generate();
    moved.range = {Time::fromSeconds(10), Time::fromSeconds(5)};
    moved.transform.position = timeline::Vec2{0.30, 0.40};
    t.clips.push_back(moved);
    p.timeline.tracks.push_back(t);

    auto j = toJson(p);
    j["formatVersion"] = 1;
    j.erase("style");
    auto parsed = projectFromJson(j);
    ASSERT_TRUE(parsed) << parsed.error().toString();
    const auto& clips = parsed->timeline.tracks.back().clips;
    EXPECT_EQ(clips[0].transform.position.value, (timeline::Vec2{0.5, 0.5}));
    EXPECT_EQ(clips[0].transform.scale.value, (timeline::Vec2{1.0, 1.0}));
    EXPECT_EQ(clips[1].transform.position.value, (timeline::Vec2{0.30, 0.40}));
    EXPECT_EQ(parsed->style, StyleSettings{});
    EXPECT_EQ(toJson(*parsed)["formatVersion"], Project::kFormatVersion);
}

TEST(ProjectJson, RefusesNewerFormatAndForeignFiles) {
    auto j = toJson(sampleProject());
    j["formatVersion"] = Project::kFormatVersion + 1;
    auto r = projectFromJson(j);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), ErrorCode::Unsupported);
    EXPECT_NE(r.error().message().find("newer version"), std::string::npos);
    EXPECT_FALSE(projectFromJson(json::Json{{"format", "something-else"}}));
}

TEST(ProjectJson, RejectsDanglingMediaReference) {
    Project p = sampleProject();
    p.media.clear();
    EXPECT_FALSE(p.validate());
    EXPECT_FALSE(projectFromJson(toJson(p)));
}

TEST(ProjectStore, SaveLoadBackupAndAutosave) {
    test::TempDir dir;
    auto folder = ProjectStore::createProjectFolder(dir.path(), "My: Lesson");
    ASSERT_TRUE(folder);
    EXPECT_EQ(folder->filename().string(), "My- Lesson.lectern");
    EXPECT_TRUE(std::filesystem::exists(*folder / "media"));

    Project p = sampleProject();
    ASSERT_TRUE(ProjectStore::save(*folder, p));
    p.title = "Renamed";
    ASSERT_TRUE(ProjectStore::save(*folder, p));
    auto loaded = ProjectStore::load(*folder);
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->project.title, "Renamed");
    EXPECT_FALSE(loaded->restoredFromBackup);

    // Corrupt project.json → previous version from .bak.
    std::ofstream(*folder / "project.json", std::ios::trunc) << "{ truncated";
    auto restored = ProjectStore::load(*folder);
    ASSERT_TRUE(restored);
    EXPECT_TRUE(restored->restoredFromBackup);
    EXPECT_EQ(restored->project.title, "Lesson 01");

    for (int i = 0; i < 3; ++i) ASSERT_TRUE(ProjectStore::writeAutosave(*folder, p, 2));
    int count = 0;
    for (const auto& de : std::filesystem::directory_iterator(*folder / "autosave")) {
        if (de.path().extension() == ".json") ++count;
    }
    EXPECT_LE(count, 2);
    EXPECT_TRUE(ProjectStore::newestAutosave(*folder).has_value());
}

TEST(Fingerprint, StableAndSizeSensitive) {
    test::TempDir dir;
    std::ofstream(dir / "a.bin", std::ios::binary) << std::string(3 << 20, 'a');
    std::ofstream(dir / "b.bin", std::ios::binary) << std::string((3 << 20) + 1, 'a');
    auto a1 = computeFingerprint(dir / "a.bin");
    auto a2 = computeFingerprint(dir / "a.bin");
    auto b = computeFingerprint(dir / "b.bin");
    ASSERT_TRUE(a1 && a2 && b);
    EXPECT_EQ(*a1, *a2);
    EXPECT_NE(a1->partialHash, b->partialHash);
    EXPECT_EQ(a1->size, 3u << 20);
}
