// ProjectController: timeline edits (Cut panel, timeline gestures, markers,
// silence removal). Each is a thin call into timeline::edit inside mutate().

#include "ui/ProjectController.h"

#include "core/Log.h"
#include "editor/SilenceDetector.h"
#include "timeline/EditOps.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace lectern::ui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
Time sec(double s) { return Time::fromSecondsF(s); }
}  // namespace

void ProjectController::splitAt(double seconds) {
    const auto selected = clipIdFrom(selectedClip_);
    int split = 0;
    mutate(QStringLiteral("Split"), [&](project::Project& p) -> Status {
        const timeline::Clip* clip = selected ? p.timeline.findClip(*selected) : nullptr;
        // A selection only applies when the playhead is over that clip.
        const bool useSelection = clip && clip->range.start < sec(seconds) && sec(seconds) < clip->range.end();
        auto n = timeline::edit::splitAt(p.timeline, sec(seconds), useSelection ? selected : std::nullopt);
        if (!n) return fail(std::move(n).error());
        split = *n;
        return ok();
    });
    if (split == 0 && message_.isEmpty()) showMessage(QStringLiteral("Nothing to split at the playhead."));
}

void ProjectController::deleteSelected() {
    const QStringList victims =
        selectedClips_.isEmpty() && !selectedClip_.isEmpty() ? QStringList{selectedClip_} : selectedClips_;
    if (victims.isEmpty()) return;

    const bool trackOnly = linkedEditMode_ == QLatin1String("track");
    const QString label = trackOnly ? QStringLiteral("Delete clip") : QStringLiteral("Delete segment");
    if (!mutate(label, [&](project::Project& p) -> Status {
            std::unordered_set<std::string> rippleGroups;
            for (const QString& qid : victims) {
                const auto id = clipIdFrom(qid);
                // Already gone with an earlier victim's linked segment (all-tracks ripple).
                if (!id || !p.timeline.findClip(*id)) continue;
                if (trackOnly) {
                    LEC_TRY(timeline::edit::deleteClipLocal(p.timeline, *id));
                    continue;
                }
                if (timeline::edit::isLinkedSegment(p.timeline, *id)) {
                    const timeline::Clip* clip = p.timeline.findClip(*id);
                    if (!clip || !clip->linkGroup) continue;
                    const std::string group = clip->linkGroup->toString();
                    if (rippleGroups.contains(group)) continue;
                    rippleGroups.insert(group);
                }
                LEC_TRY(timeline::edit::deleteClip(p.timeline, *id));
            }
            return ok();
        })) {
        return;
    }

    for (const QString& id : victims) selectedClips_.removeAll(id);
    if (victims.contains(selectedClip_) || !selectedClips_.isEmpty()) {
        selectedClip_ = selectedClips_.isEmpty() ? QString() : selectedClips_.last();
        if (selectedClips_.isEmpty()) {
            clearSelection();
        } else {
            rebuildSelection();
            emit selectionChanged();
        }
    }
}

void ProjectController::deleteClip(const QString& clipId) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    const bool trackOnly = linkedEditMode_ == QLatin1String("track");
    const bool linked = !trackOnly && project_ && timeline::edit::isLinkedSegment(project_->timeline, *id);
    const auto edit = [&](project::Project& p) {
        return trackOnly ? timeline::edit::deleteClipLocal(p.timeline, *id) : timeline::edit::deleteClip(p.timeline, *id);
    };
    if (mutate(linked ? QStringLiteral("Delete segment") : QStringLiteral("Delete clip"), edit)) {
        selectedClips_.removeAll(clipId);
        if (selectedClip_ == clipId) {
            selectedClip_ = selectedClips_.isEmpty() ? QString() : selectedClips_.last();
            if (selectedClips_.isEmpty()) clearSelection();
            else rebuildSelection();
        }
    }
}

void ProjectController::removeRange(double start, double end) {
    if (end - start < 0.04) return;
    mutate(QStringLiteral("Remove section"), [&](project::Project& p) {
        return timeline::edit::removeRange(p.timeline, TimeRange::fromStartEnd(sec(std::max(0.0, start)), sec(end)));
    });
}

void ProjectController::trimClip(const QString& clipId, const QString& edge, double seconds) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Trim"), [&](project::Project& p) {
        const auto bounds = mediaBounds(p);
        return edge == QLatin1String("start") ? timeline::edit::trimStart(p.timeline, *id, sec(seconds), bounds)
                                              : timeline::edit::trimEnd(p.timeline, *id, sec(seconds), bounds);
    });
}

void ProjectController::moveClip(const QString& clipId, double seconds) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Move"), [&](project::Project& p) { return timeline::edit::moveClip(p.timeline, *id, sec(seconds)); });
}

void ProjectController::addMarker(double seconds, const QString& label) {
    mutate(QStringLiteral("Add marker"), [&](project::Project& p) -> Status {
        const QString text = label.isEmpty() ? QStringLiteral("Marker %1").arg(p.timeline.markers.size() + 1) : label;
        p.timeline.markers.push_back({timeline::MarkerId::generate(), sec(std::max(0.0, seconds)), text.toStdString(),
                                      "#7C8CFF", timeline::MarkerKind::User});
        return ok();
    });
}

void ProjectController::removeMarker(const QString& markerId) {
    mutate(QStringLiteral("Remove marker"), [&](project::Project& p) -> Status {
        const auto uuid = Uuid::parse(markerId.toStdString());
        if (!uuid) return fail(ErrorCode::InvalidArgument, "invalid marker id");
        const timeline::MarkerId id(*uuid);
        const std::size_t before = p.timeline.markers.size();
        std::erase_if(p.timeline.markers, [&](const timeline::Marker& m) { return m.id == id; });
        if (p.timeline.markers.size() == before) return fail(ErrorCode::NotFound, "marker not found");
        return ok();
    });
}

void ProjectController::findSilences(double thresholdDb, double minPauseSeconds, double paddingSeconds) {
    if (!project_ || busy() || dir_.empty()) return;
    silences_.clear();
    emit silencesChanged();
    setBusy(QStringLiteral("Finding pauses…"));
    jobCancel_ = std::make_shared<std::atomic<bool>>(false);
    const std::shared_ptr<std::atomic<bool>> cancel = jobCancel_;
    const project::Project doc = *project_;
    const std::filesystem::path dir = dir_;
    editor::SilenceOptions opt;
    opt.thresholdDb = thresholdDb;
    opt.minSilence = sec(minPauseSeconds);
    opt.padding = sec(paddingSeconds);
    worker_->post([this, doc, dir, opt, cancel] {
        auto found = editor::detectSilences(doc, dir, opt, {}, cancel.get());
        QMetaObject::invokeMethod(
            this,
            [this, found = std::move(found)]() mutable {
                setBusy({});
                if (!found) {
                    if (found.error().code() != ErrorCode::Cancelled) showMessage(qs(found.error().message()));
                    return;
                }
                silences_ = std::move(*found);
                emit silencesChanged();
                if (silences_.empty()) showMessage(QStringLiteral("No pauses found at those settings."));
            },
            Qt::QueuedConnection);
    });
}

void ProjectController::removeSilences() {
    if (silences_.empty()) return;
    mutate(QStringLiteral("Remove pauses"), [&](project::Project& p) -> Status {
        std::vector<TimeRange> ranges = silences_;
        auto removed = timeline::edit::removeRanges(p.timeline, std::move(ranges));
        if (!removed) return fail(std::move(removed).error());
        (void)*removed;
        return ok();
    });
    silences_.clear();
    emit silencesChanged();
}

void ProjectController::clearSilences() {
    if (silences_.empty()) return;
    silences_.clear();
    emit silencesChanged();
}

}  // namespace lectern::ui
