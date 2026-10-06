// ProjectController: document lifecycle, undo/redo, saving and the view
// models QML binds to. Edits live in ProjectEdits.cpp and ProjectLooks.cpp.

#include "ui/ProjectController.h"

#include "core/Log.h"
#include "editor/AudioMixer.h"
#include "editor/ColorGrading.h"
#include "editor/LayoutPresets.h"
#include "editor/RenderPlan.h"
#include "project/ProjectStore.h"
#include "timeline/EditOps.h"
#include "timeline/Subtitles.h"

#include <QDateTime>

#include <algorithm>
#include <cmath>

namespace lectern::ui {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

constexpr std::size_t kUndoLimit = 200;
constexpr qint64 kMergeWindowMs = 1500;

QString roleColor(project::MediaRole role) {
    switch (role) {
        case project::MediaRole::Screen: return QStringLiteral("#5B7CFA");
        case project::MediaRole::Camera:
        case project::MediaRole::Phone: return QStringLiteral("#3BB98C");
        case project::MediaRole::Microphone: return QStringLiteral("#E0A43A");
        case project::MediaRole::SystemAudio: return QStringLiteral("#B07CF0");
        case project::MediaRole::Imported: return QStringLiteral("#8D94A3");
    }
    return QStringLiteral("#8D94A3");
}

/// What a clip is to the user: "screen", "camera", "microphone", "system",
/// "music", "overlay", "text" or "subtitle".
QString clipRole(const project::Project& p, const timeline::Track& track, const timeline::Clip& c) {
    if (c.kind == timeline::ClipKind::Text) return QStringLiteral("text");
    if (c.kind == timeline::ClipKind::Subtitle) return QStringLiteral("subtitle");
    const project::MediaSource* m = p.findMedia(c.media);
    if (!m) return QStringLiteral("media");
    switch (m->role) {
        case project::MediaRole::Screen: return QStringLiteral("screen");
        case project::MediaRole::Camera:
        case project::MediaRole::Phone: return QStringLiteral("camera");
        case project::MediaRole::Microphone: return QStringLiteral("microphone");
        case project::MediaRole::SystemAudio: return QStringLiteral("system");
        case project::MediaRole::Imported: break;
    }
    if (track.kind == timeline::TrackKind::Audio || m->kind == project::MediaKind::Audio) return QStringLiteral("music");
    return QStringLiteral("overlay");
}

double effectValue(const timeline::Clip& c, const char* type, const char* param, double fallback) {
    for (const auto& e : c.effects) {
        if (e.type != type || !e.enabled) continue;
        const auto it = e.params.find(param);
        return it == e.params.end() ? fallback : it->second.value;
    }
    return 0.0;
}

QVariantList curveList(const std::vector<timeline::Vec2>& pts) {
    QVariantList out;
    for (const auto& p : pts) out.append(QVariantMap{{"x", p.x}, {"y", p.y}});
    return out;
}

QVariantMap wheelMap(const timeline::ColorAdjustments::Wheel& w) {
    return {{"x", w.x}, {"y", w.y}, {"master", w.master}};
}

/// What the Adjust panel shows for a LUT reference.
QString lutName(const std::string& ref) {
    if (ref.empty()) return {};
    for (const auto& lut : editor::builtinLuts()) {
        if (lut.id == ref) return qs(lut.name);
    }
    return qs(std::filesystem::path(ref).stem().string());
}

bool hasEffect(const timeline::Clip& c, const char* type) {
    return std::any_of(c.effects.begin(), c.effects.end(), [type](const auto& e) { return e.type == type && e.enabled; });
}

}  // namespace

ProjectController::ProjectController(QObject* parent)
    : QObject(parent), worker_(std::make_unique<SerialExecutor>("lectern.ui.project")) {
    saveTimer_.setSingleShot(true);
    saveTimer_.setInterval(400);
    connect(&saveTimer_, &QTimer::timeout, this, &ProjectController::save);
}

ProjectController::~ProjectController() {
    if (jobCancel_) jobCancel_->store(true);
    if (saveTimer_.isActive()) save();
    worker_.reset();  // drains pending saves
}

QString ProjectController::title() const { return project_ ? qs(project_->title) : QString(); }

QString ProjectController::frameRateText() const {
    if (!project_) return QStringLiteral("30");
    const double fps = project_->canvas.frameRate.toDouble();
    return std::fabs(fps - std::round(fps)) < 0.001 ? QString::number(static_cast<int>(std::round(fps)))
                                                     : QString::number(fps, 'f', 2);
}

QString ProjectController::aspect() const {
    return project_ ? qs(project_->canvas.aspect) : QStringLiteral("16:9");
}

QString ProjectController::backgroundColor() const {
    return project_ ? qs(project_->canvas.backgroundColor) : QStringLiteral("#0E0F13");
}

QString ProjectController::backgroundColor2() const { return project_ ? qs(project_->style.backgroundColor2) : QString(); }

double ProjectController::duration() const { return project_ ? project_->timeline.duration().toSecondsF() : 0.0; }

QString ProjectController::formatTime(double seconds) const {
    return qs(lectern::formatDuration(Time::fromSecondsF(std::max(0.0, seconds))));
}

QString ProjectController::formatTimecode(double seconds) const {
    const double fps = project_ ? project_->canvas.frameRate.toDouble() : 30.0;
    const auto total = static_cast<std::int64_t>(std::floor(std::max(0.0, seconds) * fps + 1e-6));
    const auto frames = static_cast<int>(total % static_cast<std::int64_t>(std::lround(fps)));
    const auto wholeSeconds = static_cast<std::int64_t>(std::floor(std::max(0.0, seconds) + 1e-9));
    const auto minutes = wholeSeconds / 60;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(wholeSeconds % 60, 2, 10, QLatin1Char('0'))
        .arg(frames, 2, 10, QLatin1Char('0'));
}

void ProjectController::showInfo(const QString& text) {
    message_ = text;  // a result, not a failure: assistant edits still succeed
    emit messageChanged();
}

void ProjectController::showMessage(const QString& text) {
    if (assistantEditing_ && !text.isEmpty()) assistantError_ = text;
    message_ = text;
    emit messageChanged();
}

void ProjectController::setBusy(const QString& text) {
    if (busyText_ == text) return;
    busyText_ = text;
    emit busyChanged();
}

std::optional<timeline::ClipId> ProjectController::clipIdFrom(const QString& id) {
    auto uuid = Uuid::parse(id.toStdString());
    if (!uuid) return std::nullopt;
    return timeline::ClipId(*uuid);
}

std::optional<timeline::TrackId> ProjectController::trackIdFrom(const QString& id) {
    auto uuid = Uuid::parse(id.toStdString());
    if (!uuid) return std::nullopt;
    return timeline::TrackId(*uuid);
}

timeline::edit::MediaBounds ProjectController::mediaBounds(const project::Project& p) const {
    return [&p](const timeline::MediaId& id) -> std::optional<TimeRange> {
        const project::MediaSource* m = p.findMedia(id);
        if (!m || m->info.duration <= Time::zero()) return std::nullopt;
        return TimeRange{m->info.start, m->info.duration};
    };
}

// ---- Document lifecycle ---------------------------------------------------

void ProjectController::open(const QString& dir) {
    if (loading_) return;
    loading_ = true;
    emit loadingChanged();
    const std::filesystem::path path = dir.toStdString();
    worker_->post([this, path] {
        auto result = project::ProjectStore::load(path);
        std::shared_ptr<project::Project> loadedProject;
        QString error;
        QString note;
        if (result) {
            loadedProject = std::make_shared<project::Project>(std::move(result->project));
            note = qs(result->note);
        } else {
            error = qs(result.error().message());
        }
        QMetaObject::invokeMethod(this, [this, path, loadedProject, error, note] {
            loading_ = false;
            emit loadingChanged();
            if (!loadedProject) {
                emit failed(error);
                return;
            }
            if (saveTimer_.isActive()) save();  // the previous project's pending changes
            project_ = loadedProject;
            dir_ = path;
            notice_ = note;
            undo_.clear();
            redo_.clear();
            dirty_ = false;
            selectedClip_.clear();
            selectedClips_.clear();
            silences_.clear();
            message_.clear();
            documentChanged();
            emit editStateChanged();
            emit silencesChanged();
            emit opened(QString::fromStdString(path.string()));
        }, Qt::QueuedConnection);
    });
}

void ProjectController::close() {
    if (jobCancel_) jobCancel_->store(true);
    if (saveTimer_.isActive()) save();
    project_.reset();
    snapshot_.reset();
    dir_.clear();
    undo_.clear();
    redo_.clear();
    dirty_ = false;
    notice_.clear();
    selectedClip_.clear();
    selectedClips_.clear();
    silences_.clear();
    rebuildViews();
    rebuildSelection();
    emit projectChanged();
    emit selectionChanged();
    emit editStateChanged();
    emit snapshotChanged();
    emit silencesChanged();
}

void ProjectController::save() {
    saveTimer_.stop();
    if (!project_ || dir_.empty()) return;
    project::Project copy = *project_;
    copy.modifiedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate).toStdString();
    worker_->post([copy = std::move(copy), path = dir_] {
        if (auto st = project::ProjectStore::save(path, copy); !st) {
            LEC_ERROR("ui", "saving the project failed: {}", st.error().toString());
        }
    });
    dirty_ = false;
    emit editStateChanged();
}

void ProjectController::flushSaves() {
    save();
    worker_->wait();
}

void ProjectController::scheduleSave() {
    dirty_ = true;
    saveTimer_.start();
    emit editStateChanged();
}

bool ProjectController::mutate(const QString& label, const Mutation& edit, const QString& mergeKey) {
    if (!project_) return false;
    if (assistantEditing_ && !assistantError_.isEmpty()) return false;
    project::Project work = *project_;
    if (auto st = edit(work); !st) {
        if (st.error().code() != ErrorCode::Cancelled) LEC_WARN("edit", "{} refused: {}", label.toStdString(), st.error().message());
        if (assistantEditing_) assistantError_ = qs(st.error().message());
        if (st.error().code() != ErrorCode::Cancelled) showMessage(qs(st.error().message()));
        return false;
    }
    if (work == *project_) return true;  // no-op edit: no undo step
    if (auto valid = work.validate(); !valid) {
        LEC_ERROR("ui", "edit '{}' produced an invalid project: {}", label.toStdString(), valid.error().toString());
        showMessage(QStringLiteral("That edit could not be applied."));
        return false;
    }
    if (assistantEditing_) {
        *project_ = std::move(work);
        return true;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool merge = !mergeKey.isEmpty() && !undo_.empty() && undo_.back().mergeKey == mergeKey &&
                       now - undo_.back().atMs < kMergeWindowMs;
    if (merge) {
        undo_.back().atMs = now;  // keep the original "before" of the drag
    } else {
        LEC_INFO("edit", "{}", label.toStdString());  // one line per undo step (slider drags merge)
        undo_.push_back({std::move(*project_), label, mergeKey, now});
        if (undo_.size() > kUndoLimit) undo_.erase(undo_.begin());
    }
    redo_.clear();
    *project_ = std::move(work);
    if (!message_.isEmpty()) showMessage({});
    documentChanged();
    scheduleSave();
    return true;
}

void ProjectController::undo() {
    if (!project_ || undo_.empty()) return;
    UndoStep step = std::move(undo_.back());
    undo_.pop_back();
    LEC_INFO("edit", "undo: {}", step.label.toStdString());
    redo_.push_back({*project_, step.label, {}, 0, step.assistantId});
    *project_ = std::move(step.before);
    documentChanged();
    scheduleSave();
}

void ProjectController::redo() {
    if (!project_ || redo_.empty()) return;
    UndoStep step = std::move(redo_.back());
    redo_.pop_back();
    LEC_INFO("edit", "redo: {}", step.label.toStdString());
    undo_.push_back({*project_, step.label, {}, 0, step.assistantId});
    *project_ = std::move(step.before);
    documentChanged();
    scheduleSave();
}

void ProjectController::documentChanged() {
    snapshot_ = project_ ? std::make_shared<const project::Project>(*project_) : nullptr;
    rebuildViews();
    rebuildSelection();
    emit projectChanged();
    emit selectionChanged();
    emit snapshotChanged();
}

// ---- Selection ------------------------------------------------------------

void ProjectController::setLinkedEditMode(const QString& mode) {
    const QString next = mode == QLatin1String("allTracks") ? QStringLiteral("allTracks") : QStringLiteral("track");
    if (linkedEditMode_ == next) return;
    linkedEditMode_ = next;
    emit linkedEditModeChanged();
}

void ProjectController::selectClip(const QString& clipId, bool additive) {
    if (!additive && !clipId.isEmpty() && linkedEditMode_ == QLatin1String("allTracks") && project_) {
        if (const auto id = clipIdFrom(clipId)) {
            const auto linked = timeline::edit::linkedClips(project_->timeline, *id);
            if (linked.size() > 1) {
                QStringList ids;
                ids.reserve(static_cast<int>(linked.size()));
                for (const timeline::Clip* c : linked) ids.append(qs(c->id.toString()));
                if (ids == selectedClips_ && selectedClip_ == clipId) return followClipTrack();
                selectedClips_ = std::move(ids);
                selectedClip_ = clipId;
                rebuildSelection();
                followClipTrack(false);
                emit selectionChanged();
                return;
            }
        }
    }
    if (!additive) {
        if (clipId.isEmpty()) {
            if (selectedClips_.isEmpty() && selectedClip_.isEmpty()) return;
            selectedClips_.clear();
            selectedClip_.clear();
        } else {
            if (selectedClips_.size() == 1 && selectedClips_.first() == clipId) return followClipTrack();
            selectedClips_ = QStringList{clipId};
            selectedClip_ = clipId;
        }
    } else if (clipId.isEmpty()) {
        return;
    } else if (selectedClips_.contains(clipId)) {
        selectedClips_.removeAll(clipId);
        selectedClip_ = selectedClips_.isEmpty() ? QString() : selectedClips_.last();
    } else {
        selectedClips_.append(clipId);
        selectedClip_ = clipId;
    }
    rebuildSelection();
    followClipTrack(false);
    emit selectionChanged();
}

void ProjectController::followClipTrack(bool notify) {
    const QString track = selection_.value(QStringLiteral("trackId")).toString();
    if (track.isEmpty() || track == selectedTrack_) return;
    selectedTrack_ = track;
    if (notify) emit selectionChanged();
}

void ProjectController::selectClips(const QStringList& clipIds) {
    QStringList unique;
    unique.reserve(clipIds.size());
    for (const QString& id : clipIds) {
        if (!id.isEmpty() && !unique.contains(id)) unique.append(id);
    }
    if (unique == selectedClips_) return;
    selectedClips_ = std::move(unique);
    selectedClip_ = selectedClips_.isEmpty() ? QString() : selectedClips_.last();
    rebuildSelection();
    followClipTrack(false);
    emit selectionChanged();
}

void ProjectController::selectAllClips() {
    if (!project_) return;
    QStringList ids;
    for (const auto& track : project_->timeline.tracks) {
        for (const auto& clip : track.clips) ids.append(qs(clip.id.toString()));
    }
    selectClips(ids);
}

bool ProjectController::isClipSelected(const QString& clipId) const { return selectedClips_.contains(clipId); }

void ProjectController::clearSelection() { selectClip({}, false); }

void ProjectController::selectAt(double seconds, const QString& trackId) {
    if (!project_) return;
    const Time t = Time::fromSecondsF(seconds);
    const auto wanted = trackIdFrom(trackId);
    // Topmost first: overlays and text above video, video above audio.
    for (auto it = project_->timeline.tracks.rbegin(); it != project_->timeline.tracks.rend(); ++it) {
        if (wanted && it->id != *wanted) continue;
        if (const timeline::Clip* c = it->clipAt(t)) {
            selectClip(qs(c->id.toString()));
            return;
        }
    }
    clearSelection();
}

namespace {
/// What "auto" reads a source as, for the Adjust panel ("HDR · HLG · BT.2020").
QString detectedColorSpace(const project::MediaSource* m) {
    const auto* v = m && m->info.video ? &*m->info.video : nullptr;
    const editor::InputColor c = editor::resolveInputColor("auto", v ? v->colorTransfer : std::string_view{},
                                                           v ? v->colorPrimaries : std::string_view{});
    const QString gamut = c.primaries == editor::Primaries::Bt2020      ? QStringLiteral("BT.2020")
                          : c.primaries == editor::Primaries::DisplayP3 ? QStringLiteral("Display P3")
                                                                        : QStringLiteral("Rec.709");
    if (c.transfer == editor::Transfer::Hlg) return QStringLiteral("HDR · HLG · ") + gamut;
    if (c.transfer == editor::Transfer::Pq) return QStringLiteral("HDR · PQ · ") + gamut;
    return gamut;
}
}  // namespace

void ProjectController::rebuildSelection() {
    if (project_) {
        QStringList alive;
        alive.reserve(selectedClips_.size());
        for (const QString& id : selectedClips_) {
            if (clipIdFrom(id)) alive.append(id);
        }
        selectedClips_ = std::move(alive);
        if (!selectedClip_.isEmpty() && !selectedClips_.contains(selectedClip_)) {
            selectedClip_ = selectedClips_.isEmpty() ? QString() : selectedClips_.last();
        }
    }

    if (project_ && !selectedTrack_.isEmpty()) {
        const auto track = trackIdFrom(selectedTrack_);
        if (!track || !project_->timeline.findTrack(*track)) selectedTrack_.clear();
    }
    if (!project_) selectedTrack_.clear();

    selection_.clear();
    if (!project_ || selectedClip_.isEmpty()) return;
    const auto id = clipIdFrom(selectedClip_);
    const timeline::Track* track = nullptr;
    const timeline::Clip* clip = nullptr;
    for (const auto& t : project_->timeline.tracks) {
        for (const auto& c : t.clips) {
            if (id && c.id == *id) {
                track = &t;
                clip = &c;
            }
        }
    }
    if (!clip) {  // the clip is gone (deleted, undone): drop the selection
        selectedClip_.clear();
        return;
    }
    const project::MediaSource* media = project_->findMedia(clip->media);
    const QString role = clipRole(*project_, *track, *clip);
    const bool visual = role == QLatin1String("screen") || role == QLatin1String("camera") ||
                        role == QLatin1String("overlay") || role == QLatin1String("text");
    const auto pos = clip->transform.position.value;
    selection_ = QVariantMap{
        {"id", selectedClip_},
        {"role", role},
        {"kind", qs(std::string(timeline::toString(clip->kind)))},
        {"name", qs(clip->name)},
        {"trackId", qs(track->id.toString())},
        {"trackName", qs(track->name)},
        {"start", clip->range.start.toSecondsF()},
        {"duration", clip->range.duration.toSecondsF()},
        {"enabled", clip->enabled},
        {"linked", timeline::edit::isLinkedSegment(project_->timeline, clip->id)},
        {"visual", visual},
        {"hasAudio", media && editor::hasAudio(*media)},
        {"x", pos.x},
        {"y", pos.y},
        {"scale", clip->transform.scale.value.x},
        {"opacity", clip->opacity.value},
        {"gainDb", clip->audio.gainDb},
        {"muted", clip->audio.muted},
        {"fadeIn", clip->audio.fadeIn.toSecondsF()},
        {"fadeOut", clip->audio.fadeOut.toSecondsF()},
        {"blur", hasEffect(*clip, editor::kEffectBlur) ? effectValue(*clip, editor::kEffectBlur, "amount", 0.5) : 0.0},
        {"blurOn", hasEffect(*clip, editor::kEffectBlur)},
        {"vignette", hasEffect(*clip, editor::kEffectVignette) ? effectValue(*clip, editor::kEffectVignette, "amount", 0.5) : 0.0},
        {"vignetteOn", hasEffect(*clip, editor::kEffectVignette)},
        {"zoomOn", hasEffect(*clip, editor::kEffectZoom)},
        {"zoom", hasEffect(*clip, editor::kEffectZoom) ? effectValue(*clip, editor::kEffectZoom, "scale", 1.5) : 1.0},
        {"zoomX", hasEffect(*clip, editor::kEffectZoom) ? effectValue(*clip, editor::kEffectZoom, "x", 0.5) : 0.5},
        {"zoomY", hasEffect(*clip, editor::kEffectZoom) ? effectValue(*clip, editor::kEffectZoom, "y", 0.5) : 0.5},
        {"exposure", clip->color.exposure.value},
        {"brightness", clip->color.brightness.value},
        {"contrast", clip->color.contrast.value},
        {"saturation", clip->color.saturation.value},
        {"temperature", clip->color.temperature.value},
        {"tint", clip->color.tint.value},
        {"lift", wheelMap(clip->color.lift)},
        {"gammaWheel", wheelMap(clip->color.gammaWheel)},
        {"offset", wheelMap(clip->color.offset)},
        {"pivot", clip->color.pivot},
        {"shadows", clip->color.shadows.value},
        {"highlights", clip->color.highlights.value},
        {"colorBoost", clip->color.colorBoost.value},
        {"hue", clip->color.hue.value},
        {"curveY", curveList(clip->color.curves[0])},
        {"curveR", curveList(clip->color.curves[1])},
        {"curveG", curveList(clip->color.curves[2])},
        {"curveB", curveList(clip->color.curves[3])},
        {"gain", wheelMap(clip->color.gain)},
        {"lut", qs(clip->color.lut)},
        {"lutName", lutName(clip->color.lut)},
        {"lutAmount", clip->color.lutAmount},
        {"inputColorSpace", qs(clip->color.inputColorSpace)},
        {"detectedColorSpace", detectedColorSpace(media)},
        {"look", qs(clip->color.look.id)},
        {"lookName", qs(clip->color.look.name)},
        {"lookAmount", clip->color.look.isNone() ? 1.0 : clip->color.look.amount},
        {"nodes", nodesView(clip->color)},
        {"blurOn", hasEffect(*clip, editor::kEffectBlur)},
        {"grainOn", hasEffect(*clip, editor::kEffectFilmGrain)},
        {"grain", effectValue(*clip, editor::kEffectFilmGrain, "amount", 0.35)},
        {"grainSize", effectValue(*clip, editor::kEffectFilmGrain, "size", 1.0)},
        {"glowOn", hasEffect(*clip, editor::kEffectGlow)},
        {"glow", effectValue(*clip, editor::kEffectGlow, "amount", 0.4)},
        {"glowThreshold", effectValue(*clip, editor::kEffectGlow, "threshold", 0.75)},
        {"glowRadius", effectValue(*clip, editor::kEffectGlow, "radius", 0.5)},
        {"halationOn", hasEffect(*clip, editor::kEffectHalation)},
        {"halation", effectValue(*clip, editor::kEffectHalation, "amount", 0.5)},
        {"halationThreshold", effectValue(*clip, editor::kEffectHalation, "threshold", 0.8)},
        {"halationRadius", effectValue(*clip, editor::kEffectHalation, "radius", 0.4)},
        {"filmOn", hasEffect(*clip, editor::kEffectFilmEmulation)},
        {"filmAmount", effectValue(*clip, editor::kEffectFilmEmulation, "amount", 1.0)},
        {"filmStock", effectValue(*clip, editor::kEffectFilmEmulation, "stock", 0.0)},
        {"backgroundBlurOn", hasEffect(*clip, editor::kEffectBackgroundBlur)},
        {"backgroundBlur", hasEffect(*clip, editor::kEffectBackgroundBlur)
                               ? effectValue(*clip, editor::kEffectBackgroundBlur, "amount", 0.6) : 0.0},
    };
    if (role == QLatin1String("screen") || role == QLatin1String("camera")) {
        int segments = 0;
        for (const auto& t : project_->timeline.tracks) {
            for (const auto& c : t.clips) segments += clipRole(*project_, t, c) == role ? 1 : 0;
        }
        selection_["segments"] = segments;  // clips of the same recording source
    }
    if (clip->text) {
        selection_["text"] = qs(clip->text->text);
        selection_["preset"] = qs(clip->text->preset);
        selection_["textSize"] = clip->text->style.size;
        selection_["textColor"] = qs(clip->text->style.color);
        selection_["textBackground"] = qs(clip->text->style.background);
        selection_["textWeight"] = clip->text->style.weight;
        selection_["textAlignment"] = qs(clip->text->style.alignment);
        selection_["animationIn"] = qs(clip->text->animation.in);
        selection_["animationOut"] = qs(clip->text->animation.out);
        selection_["inDuration"] = clip->text->animation.inDuration.toSecondsF();
        selection_["outDuration"] = clip->text->animation.outDuration.toSecondsF();
    }
    if (clip->subtitle) selection_["text"] = qs(clip->subtitle->text);
}

// ---- Views ------------------------------------------------------------------

QVariantList ProjectController::silences() const {
    QVariantList out;
    for (const auto& r : silences_) out.append(QVariantMap{{"start", r.start.toSecondsF()}, {"duration", r.duration.toSecondsF()}});
    return out;
}

double ProjectController::silenceTotal() const {
    Time total;
    for (const auto& r : silences_) total += r.duration;
    return total.toSecondsF();
}

void ProjectController::rebuildViews() {
    tracks_.clear();
    markers_.clear();
    layoutRegions_.clear();
    layoutPresets_.clear();
    subtitles_.clear();
    style_.clear();
    roleTransforms_.clear();
    textOverlays_.clear();
    screenMedia_.clear();
    cameraMedia_.clear();
    if (!project_) return;
    const project::Project& p = *project_;

    // Display order: top-most visual track first, audio below
    // (docs/TIMELINE_ENGINE.md §2: tracks are stored bottom → top).
    QVariantList visual;
    QVariantList audio;
    int videoIndex = 0;
    int overlayIndex = 0;
    int audioIndex = 0;
    for (const auto& t : p.timeline.tracks) {
        const bool isAudio = t.kind == timeline::TrackKind::Audio;
        bool hasAudio = isAudio;
        QVariantList clips;
        for (const auto& c : t.clips) {
            const project::MediaSource* m = p.findMedia(c.media);
            const QString abs = m ? qs((dir_ / m->path).string()) : QString();
            const QString role = clipRole(p, t, c);
            if (m && editor::hasAudio(*m)) hasAudio = true;
            if (m && m->role == project::MediaRole::Screen && screenMedia_.isEmpty()) screenMedia_ = abs;
            if (role == QLatin1String("camera") && cameraMedia_.isEmpty()) cameraMedia_ = abs;
            QString name = qs(c.name);
            if (c.text) name = qs(c.text->text).section(QLatin1Char('\n'), 0, 0);
            if (c.subtitle) name = qs(c.subtitle->text).section(QLatin1Char('\n'), 0, 0);
            QString color = m ? roleColor(m->role) : QStringLiteral("#8D94A3");
            if (c.kind == timeline::ClipKind::Text) color = QStringLiteral("#F08A5D");
            if (c.kind == timeline::ClipKind::Subtitle) color = QStringLiteral("#E8C547");
            clips.append(QVariantMap{{"id", qs(c.id.toString())},
                                     {"name", name},
                                     {"kind", qs(std::string(timeline::toString(c.kind)))},
                                     {"role", role},
                                     {"start", c.range.start.toSecondsF()},
                                     {"duration", c.range.duration.toSecondsF()},
                                     {"sourceIn", c.sourceIn.toSecondsF()},
                                     {"media", m && m->kind == project::MediaKind::Video ? abs : QString()},
                                     {"audioPath", m && editor::hasAudio(*m) ? abs : QString()},
                                     {"color", color},
                                     {"enabled", c.enabled},
                                     {"muted", c.audio.muted},
                                     {"linked", c.linkGroup.has_value() && timeline::edit::isLinkedSegment(p.timeline, c.id)},
                                     {"linkGroup", c.linkGroup ? qs(c.linkGroup->toString()) : QString()},
                                     {"fadeIn", c.audio.fadeIn.toSecondsF()},
                                     {"fadeOut", c.audio.fadeOut.toSecondsF()}});
            if (c.kind == timeline::ClipKind::Text && c.text) {
                textOverlays_.append(QVariantMap{{"text", qs(c.text->text)},
                                                 {"start", c.range.start.toSecondsF()},
                                                 {"duration", c.range.duration.toSecondsF()}});
            }
            if (role == QLatin1String("screen") || role == QLatin1String("camera")) {
                roleTransforms_[role] = QVariantMap{{"x", c.transform.position.value.x},
                                                    {"y", c.transform.position.value.y},
                                                    {"scale", c.transform.scale.value.x}};
            }
        }
        const QString label = isAudio ? QStringLiteral("A%1").arg(++audioIndex)
                              : t.kind == timeline::TrackKind::Subtitle ? QStringLiteral("CC")
                              : t.kind == timeline::TrackKind::Overlay  ? QStringLiteral("T%1").arg(++overlayIndex)
                                                                        : QStringLiteral("V%1").arg(++videoIndex);
        QVariantMap track{{"id", qs(t.id.toString())},
                          {"label", label},
                          {"name", qs(t.name)},
                          {"kind", qs(std::string(timeline::toString(t.kind)))},
                          {"locked", t.locked},
                          {"hidden", t.hidden},
                          {"muted", t.muted},
                          {"solo", t.solo},
                          {"gainDb", t.gainDb},
                          {"hasAudio", hasAudio},
                          {"canMoveUp", canMoveTrack(qs(t.id.toString()), 1)},
                          {"canMoveDown", canMoveTrack(qs(t.id.toString()), -1)},
                          {"clips", clips}};
        (isAudio ? audio : visual).prepend(track);
    }
    std::reverse(audio.begin(), audio.end());  // audio tracks in creation order
    tracks_ = visual + audio;

    for (const auto& m : p.timeline.markers) {
        markers_.append(QVariantMap{{"id", qs(m.id.toString())},
                                    {"time", m.time.toSecondsF()},
                                    {"label", qs(m.label)},
                                    {"color", qs(m.color)},
                                    {"kind", qs(std::string(timeline::toString(m.kind)))}});
    }
    QMap<QString, QString> presetNames;
    for (const auto& preset : editor::layoutPresets()) presetNames[qs(preset.id)] = qs(preset.name);
    for (const auto& r : p.timeline.layout) {
        layoutRegions_.append(QVariantMap{{"start", r.range.start.toSecondsF()},
                                          {"duration", r.range.duration.toSecondsF()},
                                          {"preset", qs(r.preset)},
                                          {"name", presetNames.value(qs(r.preset), qs(r.preset))}});
    }
    auto rect = [](const std::optional<editor::NormRect>& r) -> QVariant {
        if (!r) return {};
        return QVariantMap{{"x", r->x}, {"y", r->y}, {"w", r->w}, {"h", r->h}};
    };
    for (const auto& preset : editor::layoutPresets()) {
        const auto slots_ = editor::customizedSlots(p, preset.id);
        layoutPresets_.append(QVariantMap{{"id", qs(preset.id)},
                                          {"name", qs(preset.name)},
                                          {"screen", rect(slots_.screen)},
                                          {"camera", rect(slots_.camera)},
                                          {"circle", slots_.circleCamera || p.style.cameraShape == "circle"},
                                          {"customized", p.style.layouts.contains(project::layoutKey(
                                                             preset.id, p.canvas.width, p.canvas.height))}});
    }
    layoutPreset_ = qs(timeline::edit::layoutAt(p.timeline, Time::zero()));

    for (const auto& t : p.timeline.tracks) {
        if (t.kind != timeline::TrackKind::Subtitle) continue;
        for (const auto& c : t.clips) {
            if (!c.subtitle) continue;
            subtitles_.append(QVariantMap{{"id", qs(c.id.toString())},
                                          {"start", c.range.start.toSecondsF()},
                                          {"duration", c.range.duration.toSecondsF()},
                                          {"text", qs(c.subtitle->text)}});
        }
    }
    const project::StyleSettings& s = p.style;
    style_ = QVariantMap{{"backgroundColor", qs(p.canvas.backgroundColor)},
                         {"backgroundColor2", qs(s.backgroundColor2)},
                         {"screenPadding", s.screenPadding},
                         {"screenRadius", s.screenRadius},
                         {"screenShadow", s.screenShadow},
                         {"cameraShape", qs(s.cameraShape)},
                         {"cameraBorder", s.cameraBorder},
                         {"cameraBorderColor", qs(s.cameraBorderColor)},
                         {"cameraMirror", s.cameraMirror},
                         {"subtitleSize", s.subtitleSize},
                         {"subtitleColor", qs(s.subtitleColor)},
                         {"subtitleBackground", qs(s.subtitleBackground)},
                         {"subtitlePosition", s.subtitlePosition}};
}

}  // namespace lectern::ui
