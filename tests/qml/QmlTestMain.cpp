// QML interaction tests: the real Lectern.UI module driven with mouse and
// keyboard events in an offscreen window.

#include "core/Log.h"
#include "media/FFmpeg.h"

#include <QGuiApplication>
#include <QQmlEngineExtensionPlugin>

#include <gtest/gtest.h>

Q_IMPORT_QML_PLUGIN(Lectern_UIPlugin)

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    lectern::media::initializeFFmpeg(lectern::LogLevel::Error);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
