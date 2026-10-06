#pragma once

#include "media/Waveform.h"

#include <QQuickAsyncImageProvider>

#include <future>
#include <map>
#include <memory>
#include <mutex>

namespace lectern::ui {

/// image://waveform/<url-encoded absolute path>?from=<s>&to=<s>&color=<RRGGBB>
///
/// Draws the waveform of media time [from, to) at the requested size. Peaks
/// are computed once per file on Qt's image thread pool (concurrent requests
/// share the work) and kept for the session.
class WaveformProvider final : public QQuickAsyncImageProvider {
public:
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

    /// Peaks for a file (computed on first use; null when it has no audio).
    static std::shared_ptr<const media::WaveformPeaks> peaks(const QString& path);
    /// Renders peaks for [from, to) into an image (tests and the response).
    static QImage render(const media::WaveformPeaks& peaks, Time from, Time to, QSize size, QColor color);

private:
    using Shared = std::shared_future<std::shared_ptr<const media::WaveformPeaks>>;
    static std::mutex mutex_;
    static std::map<QString, Shared> cache_;
};

}  // namespace lectern::ui
