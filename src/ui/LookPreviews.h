#pragma once

#include <QImage>
#include <QString>

#include <map>

namespace lectern::ui {

/// The look thumbnails ProjectController::refreshLookPreviews renders on a
/// worker thread ("none" = the clip without a look). Thread-safe.
namespace lookpreviews {
void store(std::map<QString, QImage> images);
[[nodiscard]] QImage image(const QString& lookId);
}  // namespace lookpreviews

}  // namespace lectern::ui
