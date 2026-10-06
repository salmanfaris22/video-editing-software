// Editor tests need a QGuiApplication (fonts, image formats); the offscreen
// platform keeps them headless.

#include "core/Log.h"
#include "media/FFmpeg.h"

#include <QGuiApplication>

#include <gtest/gtest.h>

int main(int argc, char** argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    lectern::media::initializeFFmpeg(lectern::LogLevel::Error);
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
