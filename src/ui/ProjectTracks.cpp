// ProjectController: tracks as layers — select, add, rename, reorder, delete,
// and moving free clips from one layer to another.

#include "ui/ProjectController.h"

#include "timeline/EditOps.h"

#include <algorithm>

namespace lectern::ui {

namespace {
QString qs(const std::string& s) { return QString::fromStdString(s); }
Time sec(double s) { return Time::fromSecondsF(s); }

/// The storage index a track swaps with to move one step up (+1) or down (−1)
/// in the timeline's display order, or -1. Visual tracks are shown top-most
/// last-stored first; audio tracks in stored order. Only tracks of the same
/// kind swap, since the picture stacks by kind (video, overlays, subtitles).
int swapPartner(const timeline::Timeline& tl, std::size_t index, int direction) {
    const auto kind = tl.tracks[index].kind;
    const int step = kind == timeline::TrackKind::Audio ? -direction : direction;
    for (int i = static_cast<int>(index) + step; i >= 0 && i < static_cast<int>(tl.tracks.size()); i += step) {
        if (tl.tracks[static_cast<std::size_t>(i)].kind == kind) return i;
    }
    return -1;
}
}  // namespace

void ProjectController::selectTrack(const QString& trackId) {
    if (selectedTrack_ == trackId) return;
    selectedTrack_ = trackId;
    emit selectionChanged();
}

bool ProjectController::canMoveTrack(const QString& trackId, int direction) const {
    const auto id = trackIdFrom(trackId);
    if (!project_ || !id) return false;
    const auto& tracks = project_->timeline.tracks;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (tracks[i].id == *id) return swapPartner(project_->timeline, i, direction) >= 0;
    }
    return false;
}

QString ProjectController::addTrack(const QString& kind) {
    if (!project_) return {};
    const timeline::TrackKind k = kind == QLatin1String("audio")      ? timeline::TrackKind::Audio
                                  : kind == QLatin1String("subtitle") ? timeline::TrackKind::Subtitle
                                                                      : timeline::TrackKind::Overlay;
    const std::string base = k == timeline::TrackKind::Audio      ? "Audio"
                             : k == timeline::TrackKind::Subtitle ? "Captions"
                                                                  : "Layer";
    const timeline::TrackId id = timeline::TrackId::generate();
    const bool added = mutate(QStringLiteral("Add layer"), [&](project::Project& p) -> Status {
        int n = 1;
        for (const auto& t : p.timeline.tracks) n += t.kind == k ? 1 : 0;
        p.timeline.tracks.push_back({id, k, base + " " + std::to_string(n)});
        return ok();
    });
    if (!added) return {};
    selectTrack(qs(id.toString()));
    return selectedTrack_;
}

void ProjectController::renameTrack(const QString& trackId, const QString& name) {
    const auto id = trackIdFrom(trackId);
    const QString clean = name.trimmed();
    if (!id || clean.isEmpty()) return;
    mutate(QStringLiteral("Rename layer"), [&](project::Project& p) -> Status {
        timeline::Track* t = p.timeline.findTrack(*id);
        if (!t) return fail(ErrorCode::NotFound, "track not found");
        t->name = clean.toStdString();
        return ok();
    });
}

void ProjectController::moveTrack(const QString& trackId, int direction) {
    const auto id = trackIdFrom(trackId);
    if (!id || direction == 0) return;
    mutate(direction > 0 ? QStringLiteral("Layer up") : QStringLiteral("Layer down"), [&](project::Project& p) -> Status {
        auto& tracks = p.timeline.tracks;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].id != *id) continue;
            const int other = swapPartner(p.timeline, i, direction > 0 ? 1 : -1);
            if (other < 0) return fail(ErrorCode::InvalidArgument, "the layer is already at the edge");
            std::swap(tracks[i], tracks[static_cast<std::size_t>(other)]);
            return ok();
        }
        return fail(ErrorCode::NotFound, "track not found");
    });
}

void ProjectController::deleteTrack(const QString& trackId) {
    const auto id = trackIdFrom(trackId);
    if (!id) return;
    const bool removed = mutate(QStringLiteral("Delete layer"), [&](project::Project& p) -> Status {
        auto& tracks = p.timeline.tracks;
        const auto it = std::find_if(tracks.begin(), tracks.end(), [&](const auto& t) { return t.id == *id; });
        if (it == tracks.end()) return fail(ErrorCode::NotFound, "track not found");
        if (it->locked) return fail(ErrorCode::InvalidState, "unlock the layer first");
        tracks.erase(it);
        return ok();
    });
    if (removed && selectedTrack_ == trackId) selectTrack({});
}

void ProjectController::moveClipToTrack(const QString& clipId, const QString& trackId, double seconds) {
    const auto clip = clipIdFrom(clipId);
    const auto track = trackIdFrom(trackId);
    if (!clip || !track) return;
    if (mutate(QStringLiteral("Move"), [&](project::Project& p) {
            return timeline::edit::moveClipToTrack(p.timeline, *clip, *track, sec(seconds));
        })) {
        selectTrack(trackId);
    }
}

void ProjectController::moveClipAlone(const QString& clipId, double seconds, const QString& trackId) {
    const auto clip = clipIdFrom(clipId);
    if (!clip) return;
    const auto track = trackId.isEmpty() ? std::optional<timeline::TrackId>(timeline::TrackId{}) : trackIdFrom(trackId);
    if (!track) return;
    if (mutate(QStringLiteral("Move clip alone"), [&](project::Project& p) {
            return timeline::edit::moveClipAlone(p.timeline, *clip, sec(seconds), *track);
        }) && !trackId.isEmpty()) {
        selectTrack(trackId);
    }
}

void ProjectController::unlinkClip(const QString& clipId) {
    const auto clip = clipIdFrom(clipId);
    if (!clip) return;
    mutate(QStringLiteral("Unlink from recording"), [&](project::Project& p) { return timeline::edit::unlinkClip(p.timeline, *clip); });
}

}  // namespace lectern::ui
