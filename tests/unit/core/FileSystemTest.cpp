#include "core/FileSystem.h"
#include "core/Settings.h"
#include "support/TestSupport.h"

#include <gtest/gtest.h>

#include <fstream>

#if !defined(_WIN32)
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace lectern;

TEST(FileSystem, AtomicWriteReplacesAndKeepsBackup) {
    test::TempDir dir;
    const auto file = dir / "project.json";
    ASSERT_TRUE(fs::writeFileAtomic(file, "v1", {.fsync = true, .keepBackup = true}));
    ASSERT_TRUE(fs::writeFileAtomic(file, "v2", {.fsync = true, .keepBackup = true}));
    EXPECT_EQ(fs::readFile(file).value(), "v2");
    EXPECT_EQ(fs::readFile(file.string() + ".bak").value(), "v1");
    EXPECT_FALSE(std::filesystem::exists(file.string() + ".tmp"));
}

TEST(FileSystem, ReadFileRejectsOversizedFiles) {
    test::TempDir dir;
    ASSERT_TRUE(fs::writeFileAtomic(dir / "big", std::string(1000, 'x')));
    auto r = fs::readFile(dir / "big", 100);
    ASSERT_FALSE(r);
    EXPECT_EQ(r.error().code(), ErrorCode::InvalidArgument);
    EXPECT_EQ(fs::readFile(dir / "missing").error().code(), ErrorCode::NotFound);
}

TEST(FileSystem, DiskSpaceQueryWorksForNonExistentSubpath) {
    test::TempDir dir;
    auto space = fs::queryDiskSpace(dir / "not" / "yet" / "created");
    ASSERT_TRUE(space) << space.error().toString();
    EXPECT_GT(space->capacity, 0u);
}

TEST(FileSystem, SanitizeAndUniquePath) {
    EXPECT_EQ(fs::sanitizeFileName("Lesson: 1/2?"), "Lesson- 1-2-");
    EXPECT_EQ(fs::sanitizeFileName("   "), "Untitled");
    test::TempDir dir;
    std::ofstream(dir / "a.txt") << "x";
    EXPECT_EQ(fs::uniquePath(dir / "a.txt").filename().string(), "a 2.txt");
}

#if !defined(_WIN32)
TEST(FileLock, ExclusiveAcrossProcessesAndReleasedOnProcessDeath) {
    test::TempDir dir;
    const auto lockPath = dir / "session.lock";
    const pid_t child = fork();
    if (child == 0) {
        auto lock = fs::FileLock::tryAcquire(lockPath);
        if (!lock) _exit(2);
        ::sleep(30);  // killed by the parent
        _exit(0);
    }
    // Wait for the child to take the lock.
    bool busy = false;
    for (int i = 0; i < 200 && !busy; ++i) {
        auto attempt = fs::FileLock::tryAcquire(lockPath);
        busy = !attempt && attempt.error().code() == ErrorCode::DeviceBusy;
        if (!busy) {
            attempt->release();
            usleep(10'000);
        }
    }
    EXPECT_TRUE(busy) << "child never acquired the lock";
    ::kill(child, SIGKILL);
    int status = 0;
    waitpid(child, &status, 0);
    // The OS released the lock when the process died.
    auto after = fs::FileLock::tryAcquire(lockPath);
    EXPECT_TRUE(after) << (after ? "" : after.error().toString());
}
#endif

TEST(Settings, RoundTripAndLenientParsing) {
    test::TempDir dir;
    SettingsStore store(dir / "settings.json");
    auto defaults = store.load();
    ASSERT_TRUE(defaults);
    EXPECT_EQ(defaults->recording.frameRate, 30);

    AppSettings s;
    s.recording.frameRate = 60;
    s.recording.resolution = "1440p";
    s.logLevel = LogLevel::Debug;
    ASSERT_TRUE(store.save(s));
    auto loaded = store.load();
    ASSERT_TRUE(loaded);
    EXPECT_EQ(loaded->recording.frameRate, 60);
    EXPECT_EQ(loaded->recording.resolution, "1440p");
    EXPECT_EQ(loaded->logLevel, LogLevel::Debug);

    // Invalid values fall back to defaults instead of failing startup.
    ASSERT_TRUE(fs::writeFileAtomic(dir / "settings.json", R"({"recording":{"frameRate":17,"showCursor":"yes"}})"));
    auto lenient = store.load();
    ASSERT_TRUE(lenient);
    EXPECT_EQ(lenient->recording.frameRate, 30);
    EXPECT_TRUE(lenient->recording.showCursor);

    ASSERT_TRUE(fs::writeFileAtomic(dir / "settings.json", "{not json"));
    EXPECT_FALSE(store.load());
}
