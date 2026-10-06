#include "ui/PlaybackController.h"

#include "core/Log.h"
#include "core/Uuid.h"

#include "platform/AudioOutput.h"
#include "ui/ProjectController.h"

#include <algorithm>
#include <cmath>

namespace lectern::ui {

PlaybackController::PlaybackController(ProjectController* project, bool silent, QObject* parent)
    : QObject(parent), project_(project), silent_(silent) {
    // The playhead follows the audio clock at display rate (≈60 Hz) so it glides.
    ticker_.setTimerType(Qt::PreciseTimer);
    ticker_.setInterval(16);
    connect(&ticker_, &QTimer::timeout, this, &PlaybackController::tick);
    connect(project_, &ProjectController::snapshotChanged, this, &PlaybackController::follow);
    follow();
}

PlaybackController::~PlaybackController() { engine_.reset(); }  // joins engine threads before members go

void PlaybackController::follow() {
    const auto snapshot = project_->snapshot();
    if (!snapshot) {
        engine_.reset();
        dir_.clear();
        {
            std::lock_guard lock(frameMutex_);
            frame_ = QImage();
        }
        if (playing_) {
            playing_ = false;
            emit playingChanged();
        }
        position_ = 0;
        emit positionChanged();
        emit frameReady();
        return;
    }
    if (!engine_ || dir_ != project_->directory()) {
        engine_.reset();
        dir_ = project_->directory();
        auto output = silent_ ? audio::makeNullAudioOutput() : platform::createAudioOutput();
        editor::PlaybackEngine::Listener& listener = *this;
        engine_ = std::make_unique<editor::PlaybackEngine>(dir_, std::move(output), listener);
        engine_->setPreviewSize(previewSize_);
        applyCompare();
        position_ = 0;
        emit positionChanged();
    }
    engine_->setProject(snapshot);
    const double end = snapshot->timeline.duration().toSecondsF();
    if (position_ > end) {
        position_ = end;
        emit positionChanged();
    }
}

void PlaybackController::setCompareMode(const QString& mode) {
    static const QStringList kModes{QStringLiteral("off"), QStringLiteral("bypass"), QStringLiteral("wipe"), QStringLiteral("side")};
    const QString next = kModes.contains(mode) ? mode : QStringLiteral("off");
    if (next == compareMode_) return;
    compareMode_ = next;
    LEC_INFO("playback", "compare view: {}", compareMode_.toStdString());
    applyCompare();
    emit compareChanged();
}

void PlaybackController::setCompareSplit(double split) {
    split = std::clamp(split, 0.0, 1.0);
    if (split == compareSplit_) return;
    compareSplit_ = split;
    applyCompare();
    emit compareChanged();
}

void PlaybackController::setHighlight(const QString& clipId, const QString& nodeId) {
    if (clipId == highlightClip_ && nodeId == highlightNode_) return;
    highlightClip_ = nodeId.isEmpty() ? QString() : clipId;
    highlightNode_ = nodeId;
    applyCompare();
    emit compareChanged();
}

void PlaybackController::applyCompare() {
    if (!engine_) return;
    const auto clip = Uuid::parse(highlightClip_.toStdString());
    engine_->setHighlight(clip ? timeline::ClipId(*clip) : timeline::ClipId(), highlightNode_.toStdString());
    using Compare = editor::PlaybackEngine::Compare;
    const Compare mode = compareMode_ == QLatin1String("bypass") ? Compare::Bypass
                         : compareMode_ == QLatin1String("wipe") ? Compare::Wipe
                         : compareMode_ == QLatin1String("side") ? Compare::SideBySide
                                                                  : Compare::Off;
    engine_->setCompare(mode, compareSplit_);
}

void PlaybackController::play() {
    if (!engine_) return;
    LEC_INFO("playback", "play from {:.2f} s", position_);
    engine_->play();
}

void PlaybackController::pause() {
    if (!engine_) return;
    LEC_INFO("playback", "pause at {:.2f} s", position_);
    engine_->pause();
}

void PlaybackController::toggle() {
    if (!engine_) return;
    if (engine_->playing()) {
        pause();
    } else {
        play();
    }
}

void PlaybackController::seek(double seconds) {
    if (!engine_) return;
    const double end = engine_->duration().toSecondsF();
    position_ = std::clamp(seconds, 0.0, end);
    engine_->seek(Time::fromSecondsF(position_));
    emit positionChanged();
}

void PlaybackController::step(int frames) {
    if (!engine_) return;
    pause();
    const double fps = project_->frameRate() > 0 ? project_->frameRate() : 30.0;
    // Land on frame boundaries so repeated steps never drift.
    const double current = std::floor(position_ * fps + 0.5);
    seek((current + frames) / fps);
}

QImage PlaybackController::frame() const {
    std::lock_guard lock(frameMutex_);
    return frame_;
}

void PlaybackController::setPreviewSize(QSize pixels) {
    if (pixels == previewSize_ || pixels.isEmpty()) return;
    previewSize_ = pixels;
    if (engine_) engine_->setPreviewSize(pixels);
}

void PlaybackController::onFrame(const QImage& frame, Time) {
    {
        std::lock_guard lock(frameMutex_);
        frame_ = frame;
    }
    QMetaObject::invokeMethod(this, &PlaybackController::frameReady, Qt::QueuedConnection);
}

void PlaybackController::onPlayingChanged(bool playing) {
    QMetaObject::invokeMethod(this, [this, playing] {
        if (playing_ == playing) return;
        playing_ = playing;
        if (playing) {
            ticker_.start();
        } else {
            ticker_.stop();
            tick();
            levels_[0] = levels_[1] = 0;
            emit levelsChanged();
        }
        if (engine_ && outputName_ != QString::fromStdString(engine_->outputName())) {
            outputName_ = QString::fromStdString(engine_->outputName());
            emit outputChanged();
        }
        emit playingChanged();
    }, Qt::QueuedConnection);
}

void PlaybackController::tick() {
    if (!engine_) return;
    position_ = engine_->position().toSecondsF();
    levels_[0] = engine_->level(0);
    levels_[1] = engine_->level(1);
    emit positionChanged();
    emit levelsChanged();
}

}  // namespace lectern::ui
