#include "ui/LookPreviews.h"

#include <mutex>

namespace lectern::ui::lookpreviews {

namespace {
std::mutex mutex;
std::map<QString, QImage> images;
}  // namespace

void store(std::map<QString, QImage> next) {
    std::lock_guard lock(mutex);
    images = std::move(next);
}

QImage image(const QString& lookId) {
    std::lock_guard lock(mutex);
    const auto it = images.find(lookId);
    return it == images.end() ? QImage() : it->second;
}

}  // namespace lectern::ui::lookpreviews
