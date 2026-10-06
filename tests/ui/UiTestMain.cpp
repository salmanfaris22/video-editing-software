// UI controller tests: the C++ view-models QML talks to, driven through the
// same Q_INVOKABLE API, on the offscreen platform.

#include "core/Log.h"
#include "media/FFmpeg.h"
#include "support/TestSupport.h"

#include <QGuiApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    // Settings, saved looks and logs go to a throwaway folder, never the user's.
    const lectern::test::TempDir appData("lectern-appdata");
    if (qEnvironmentVariableIsEmpty("LECTERN_APP_DATA_DIR")) qputenv("LECTERN_APP_DATA_DIR", appData.path().string().c_str());
    QGuiApplication app(argc, argv);
    lectern::media::initializeFFmpeg(lectern::LogLevel::Error);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
