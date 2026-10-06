#pragma once

#include <QQuickAsyncImageProvider>

#include <list>
#include <mutex>
#include <utility>

namespace lectern::ui {

/// image://thumbnail/<url-encoded absolute path>?t=<seconds>
///
/// Decodes one frame near time t on Qt's image thread pool (never on the UI
/// thread) and keeps a small LRU cache so timelines/previews that re-request
/// the same frame do not decode again.
class ThumbnailProvider final : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    /// Shared, bounded cache (64 entries).
    static QImage cached(const QString& key);
    static void store(const QString& key, const QImage& image);

private:
    static std::mutex mutex_;
    static std::list<std::pair<QString, QImage>> cache_;
};

}  // namespace lectern::ui
