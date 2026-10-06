#pragma once

#include <QImage>
#include <QString>

namespace lectern::ui {

/// One decoded frame of a video (or an image) near `seconds`, at most
/// `maxHeight` pixels high, as RGBA8888. Blocking (FFmpeg decode): call it
/// off the UI thread. Recent frames are kept in a small shared cache.
[[nodiscard]] QImage grabFrame(const QString& path, double seconds, int maxHeight);

}  // namespace lectern::ui
