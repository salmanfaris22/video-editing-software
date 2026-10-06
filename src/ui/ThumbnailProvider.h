#pragma once

#include <QQuickAsyncImageProvider>

namespace lectern::ui {

/// image://thumbnail/<url-encoded absolute path>?t=<seconds>
///
/// Decodes one frame near time t on Qt's image thread pool (never on the UI
/// thread) with grabFrame, whose small LRU cache spares timelines/previews
/// that re-request the same frame a second decode.
class ThumbnailProvider final : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
};

}  // namespace lectern::ui
