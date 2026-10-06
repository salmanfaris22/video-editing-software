// ProjectController: looks (cinematic presets and the user's saved looks),
// look thumbnails and hover previews, and copying grades between clips —
// the Color page's grade management (docs/v2/COLOR_GRADING_COMPARISON.md §2.12).

#include "ui/ProjectController.h"

#include "core/FileSystem.h"
#include "core/Json.h"
#include "core/Log.h"
#include "core/Uuid.h"
#include "editor/ColorGrading.h"
#include "editor/Looks.h"
#include "editor/RenderPlan.h"
#include "timeline/TimelineJson.h"
#include "ui/FrameGrab.h"
#include "ui/LookPreviews.h"

#include <QCoreApplication>
#include <QPointer>
#include <QThreadPool>

#include <algorithm>
#include <map>

namespace lectern::ui {

namespace {

using Look = timeline::ColorAdjustments::Look;

QString qs(const std::string& s) { return QString::fromStdString(s); }

timeline::Clip* clipById(project::Project& p, const timeline::ClipId& id) {
    for (auto& t : p.timeline.tracks) {
        for (auto& c : t.clips) {
            if (c.id == id) return &c;
        }
    }
    return nullptr;
}

/// Video and images on picture tracks: what the Color page grades.
bool isPicture(const project::Project& p, const timeline::Track& t, const timeline::Clip& c) {
    if (t.kind != timeline::TrackKind::Video && t.kind != timeline::TrackKind::Overlay) return false;
    if (c.kind != timeline::ClipKind::Media) return false;
    const project::MediaSource* m = p.findMedia(c.media);
    return m && (m->kind == project::MediaKind::Video || m->kind == project::MediaKind::Image);
}

/// Picture clips in timeline order (start time, then lower track first).
std::vector<const timeline::Clip*> pictureClips(const project::Project& p) {
    std::vector<std::pair<const timeline::Clip*, std::size_t>> found;
    for (std::size_t ti = 0; ti < p.timeline.tracks.size(); ++ti) {
        const auto& t = p.timeline.tracks[ti];
        for (const auto& c : t.clips) {
            if (isPicture(p, t, c)) found.emplace_back(&c, ti);
        }
    }
    std::stable_sort(found.begin(), found.end(), [](const auto& a, const auto& b) {
        if (a.first->range.start != b.first->range.start) return a.first->range.start < b.first->range.start;
        return a.second < b.second;
    });
    std::vector<const timeline::Clip*> out;
    out.reserve(found.size());
    for (const auto& f : found) out.push_back(f.first);
    return out;
}

/// The grade travels; how the target's source is read (its input color space) stays.
void takeGrade(timeline::ColorAdjustments& target, const timeline::ColorAdjustments& source) {
    const std::string input = target.inputColorSpace;
    target = source;
    target.inputColorSpace = input;
}

std::filesystem::path savedLooksPath() { return fs::appDataDirectory() / "looks.json"; }

}  // namespace

// ---- Looks ---------------------------------------------------------------------

void ProjectController::loadSavedLooks() const {
    if (savedLooksLoaded_) return;
    savedLooksLoaded_ = true;
    savedLooks_.clear();
    const auto text = fs::readFile(savedLooksPath(), 4u << 20);
    if (!text) return;  // none saved yet
    const auto doc = json::parse(*text, "looks.json");
    if (!doc || !doc->is_object() || !doc->contains("looks") || !(*doc)["looks"].is_array()) {
        LEC_WARN("looks", "ignoring unreadable {}", savedLooksPath().string());
        return;
    }
    for (const auto& j : (*doc)["looks"]) {
        Look l = timeline::lookFromJson(j);
        if (l.id.starts_with("custom-") && !l.name.empty()) {
            l.amount = 1.0;
            savedLooks_.push_back(std::move(l));
        }
    }
}

const timeline::ColorAdjustments::Look* ProjectController::findLook(const QString& id) const {
    const std::string key = id.toStdString();
    if (const editor::LookPreset* p = editor::findBuiltinLook(key)) return &p->settings;
    loadSavedLooks();
    for (const auto& l : savedLooks_) {
        if (l.id == key) return &l;
    }
    return nullptr;
}

QVariantList ProjectController::looks() const {
    QVariantList out;
    for (const auto& p : editor::builtinLooks()) {
        out.append(QVariantMap{{"id", qs(p.settings.id)},
                               {"name", qs(p.settings.name)},
                               {"category", qs(p.category)},
                               {"description", qs(p.description)},
                               {"custom", false}});
    }
    loadSavedLooks();
    for (const auto& l : savedLooks_) {
        out.append(QVariantMap{{"id", qs(l.id)},
                               {"name", qs(l.name)},
                               {"category", QStringLiteral("My Looks")},
                               {"description", QStringLiteral("Your saved grade")},
                               {"custom", true}});
    }
    return out;
}

QVariantList ProjectController::filmStocks() const {
    QVariantList out;
    int index = 0;
    for (const auto& s : editor::filmStocks()) {
        out.append(QVariantMap{{"index", index++}, {"id", qs(s.id)}, {"name", qs(s.name)}});
    }
    return out;
}

void ProjectController::applyLook(const QString& clipId, const QString& lookId, double amount) {
    if (lookId.isEmpty() || lookId == QLatin1String("none")) {
        removeLook(clipId);
        return;
    }
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return;
    const Look* look = findLook(lookId);
    if (!look) {
        showMessage(QStringLiteral("Unknown look “%1”").arg(lookId));
        return;
    }
    const Look chosen = *look;
    mutate(QStringLiteral("Look: ") + qs(chosen.name), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color.look = chosen;
        c->color.look.amount = std::clamp(amount, 0.0, 1.0);
        return ok();
    });
}

void ProjectController::setLookAmount(const QString& clipId, double amount) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Look amount"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        if (c->color.look.isNone()) return fail(ErrorCode::InvalidArgument, "the clip has no look");
        c->color.look.amount = std::clamp(amount, 0.0, 1.0);
        return ok();
    }, QStringLiteral("lookAmount:") + clipId);
}

void ProjectController::removeLook(const QString& clipId) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Remove look"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color.look = {};
        return ok();
    });
}

void ProjectController::applyLookToAll(const QString& clipId) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Apply look to all clips"), [&](project::Project& p) -> Status {
        const timeline::Clip* source = clipById(p, *id);
        if (!source) return fail(ErrorCode::NotFound, "clip not found");
        const Look look = source->color.look;
        for (auto& t : p.timeline.tracks) {
            for (auto& c : t.clips) {
                if (isPicture(p, t, c)) c.color.look = look;
            }
        }
        return ok();
    });
}

QString ProjectController::saveLook(const QString& clipId, const QString& name) {
    const auto id = clipIdFrom(clipId);
    const QString title = name.trimmed().left(60);
    if (!id || !project_ || title.isEmpty()) return {};
    const timeline::Clip* clip = clipById(*project_, *id);
    if (!clip) return {};
    Look look = editor::lookFromGrade(editor::gradeAt(clip->color, Time::zero()));
    look.id = "custom-" + Uuid::generateV4().toString().substr(0, 8);
    look.name = title.toStdString();
    look.amount = 1.0;
    loadSavedLooks();
    savedLooks_.push_back(look);
    json::Json list = json::Json::array();
    for (const auto& l : savedLooks_) list.push_back(timeline::toJson(l));
    const Status saved = fs::writeFileAtomic(savedLooksPath(), json::dump(json::Json{{"version", 1}, {"looks", list}}));
    if (!saved) {
        savedLooks_.pop_back();
        showMessage(QStringLiteral("Could not save the look: ") + qs(saved.error().message()));
        return {};
    }
    LEC_INFO("looks", "saved look '{}' ({})", look.name, look.id);
    emit looksChanged();
    showInfo(QStringLiteral("Saved “%1” to My Looks").arg(title));
    return qs(look.id);
}

void ProjectController::deleteLook(const QString& lookId) {
    loadSavedLooks();
    const std::string key = lookId.toStdString();
    const auto it = std::find_if(savedLooks_.begin(), savedLooks_.end(), [&](const Look& l) { return l.id == key; });
    if (it == savedLooks_.end()) return;  // built-in or unknown
    const Look removed = *it;
    savedLooks_.erase(it);
    json::Json list = json::Json::array();
    for (const auto& l : savedLooks_) list.push_back(timeline::toJson(l));
    if (const Status saved = fs::writeFileAtomic(savedLooksPath(), json::dump(json::Json{{"version", 1}, {"looks", list}})); !saved) {
        savedLooks_.push_back(removed);
        showMessage(QStringLiteral("Could not delete the look: ") + qs(saved.error().message()));
        return;
    }
    LEC_INFO("looks", "deleted look '{}' ({})", removed.name, removed.id);
    emit looksChanged();
}

void ProjectController::previewLook(const QString& clipId, const QString& lookId) {
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return;
    auto work = std::make_shared<project::Project>(*project_);
    timeline::Clip* c = clipById(*work, *id);
    if (!c) return;
    if (lookId.isEmpty() || lookId == QLatin1String("none")) {
        c->color.look = {};
    } else {
        const Look* look = findLook(lookId);
        if (!look) return;
        const double amount = c->color.look.isNone() ? 1.0 : c->color.look.amount;
        c->color.look = *look;
        c->color.look.amount = amount;
    }
    snapshot_ = std::move(work);
    emit snapshotChanged();
}

void ProjectController::refreshLookPreviews(const QString& clipId, double seconds) {
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return;
    const project::Project& p = *project_;
    const timeline::Clip* clip = nullptr;
    for (const auto& t : p.timeline.tracks) {
        for (const auto& c : t.clips) {
            if (c.id == *id && isPicture(p, t, c)) clip = &c;
        }
    }
    if (!clip) return;
    const project::MediaSource* m = p.findMedia(clip->media);
    const double start = clip->range.start.toSecondsF();
    const double duration = clip->range.duration.toSecondsF();
    const double localSeconds = std::clamp(seconds - start, 0.0, std::max(0.0, duration - 0.05));
    const Time local = Time::fromSecondsF(localSeconds);
    const auto* video = m->info.video ? &*m->info.video : nullptr;

    struct Job {
        QString path;
        double sourceSeconds = 0;
        editor::ColorParams base;
        editor::InputColor input;
        std::filesystem::path dir;
        std::vector<std::pair<QString, Look>> looks;
    };
    Job job;
    job.path = qs((dir_ / m->path).string());
    job.sourceSeconds = m->kind == project::MediaKind::Image ? 0.0 : clip->sourceTimeAt(clip->range.start + local).toSecondsF();
    job.base = editor::gradeAt(clip->color, local, false);
    job.input = editor::resolveInputColor(clip->color.inputColorSpace, video ? video->colorTransfer : std::string_view{},
                                          video ? video->colorPrimaries : std::string_view{});
    job.dir = dir_;
    for (const auto& preset : editor::builtinLooks()) job.looks.emplace_back(qs(preset.settings.id), preset.settings);
    loadSavedLooks();
    for (const auto& l : savedLooks_) job.looks.emplace_back(qs(l.id), l);

    const int jobId = ++lookPreviewJob_;
    QPointer<ProjectController> self(this);
    QThreadPool::globalInstance()->start([self, jobId, job = std::move(job)]() {
        std::map<QString, QImage> out;
        QImage frame = grabFrame(job.path, job.sourceSeconds, 135);
        if (!frame.isNull()) {
            frame = frame.convertToFormat(QImage::Format_RGB32);
            editor::applyInputColor(frame, job.input);
            static editor::LutCache luts;
            const auto lut = job.base.lut.empty() ? nullptr : luts.get(job.base.lut, job.dir);
            QImage none = frame.copy();
            editor::applyColor(none, job.base, lut.get());
            out.emplace(QStringLiteral("none"), std::move(none));
            for (const auto& [lookId, look] : job.looks) {
                QImage img = frame.copy();
                editor::applyColor(img, editor::applyLook(job.base, look), lut.get());
                out.emplace(lookId, std::move(img));
            }
        }
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            [self, jobId, out = std::move(out)]() mutable {
                if (!self || jobId != self->lookPreviewJob_) return;  // closed, or a newer request is running
                lookpreviews::store(std::move(out));
                ++self->lookPreviewRevision_;
                emit self->lookPreviewsChanged();
            },
            Qt::QueuedConnection);
    });
}

// ---- Copying grades --------------------------------------------------------------

QStringList ProjectController::gradableClips() const {
    QStringList out;
    if (!project_) return out;
    for (const timeline::Clip* c : pictureClips(*project_)) out.append(qs(c->id.toString()));
    return out;
}

void ProjectController::copyGrade(const QString& clipId) {
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return;
    const timeline::Clip* clip = clipById(*project_, *id);
    if (!clip) return;
    copiedGrade_ = clip->color;
    LEC_INFO("color", "copied the grade of clip {}", clipId.toStdString());
    emit copiedGradeChanged();
    showInfo(QStringLiteral("Grade copied — paste it on other clips (⌘V on the Color page)"));
}

void ProjectController::pasteGrade(const QStringList& clipIds) {
    if (!copiedGrade_) {
        showMessage(QStringLiteral("Copy a grade first"));
        return;
    }
    std::vector<timeline::ClipId> ids;
    for (const QString& s : clipIds) {
        if (const auto id = clipIdFrom(s)) ids.push_back(*id);
    }
    if (ids.empty()) return;
    const timeline::ColorAdjustments grade = *copiedGrade_;
    mutate(ids.size() == 1 ? QStringLiteral("Paste grade") : QStringLiteral("Paste grade to %1 clips").arg(ids.size()),
           [&](project::Project& p) -> Status {
               for (const auto& id : ids) {
                   timeline::Clip* c = clipById(p, id);
                   if (!c) return fail(ErrorCode::NotFound, "clip not found");
                   takeGrade(c->color, grade);
               }
               return ok();
           });
}

void ProjectController::copyGradeTo(const QString& fromClipId, const QStringList& toClipIds) {
    const auto from = clipIdFrom(fromClipId);
    std::vector<timeline::ClipId> ids;
    for (const QString& s : toClipIds) {
        if (const auto id = clipIdFrom(s); id && id != from) ids.push_back(*id);
    }
    if (!from || ids.empty()) return;
    mutate(ids.size() == 1 ? QStringLiteral("Copy grade") : QStringLiteral("Copy grade to %1 clips").arg(ids.size()),
           [&](project::Project& p) -> Status {
               const timeline::Clip* source = clipById(p, *from);
               if (!source) return fail(ErrorCode::NotFound, "clip not found");
               const timeline::ColorAdjustments grade = source->color;
               for (const auto& id : ids) {
                   timeline::Clip* c = clipById(p, id);
                   if (!c) return fail(ErrorCode::NotFound, "clip not found");
                   takeGrade(c->color, grade);
               }
               return ok();
           });
}

void ProjectController::applyPreviousGrade(const QString& clipId) {
    if (!project_) return;
    const QStringList order = gradableClips();
    const qsizetype at = order.indexOf(clipId);
    if (at <= 0) {
        showMessage(at == 0 ? QStringLiteral("This is the first clip — there is no grade before it") : QStringLiteral("Select a clip to grade"));
        return;
    }
    const auto from = clipIdFrom(order[at - 1]);
    const auto to = clipIdFrom(clipId);
    mutate(QStringLiteral("Grade from previous clip"), [&](project::Project& p) -> Status {
        const timeline::Clip* source = clipById(p, *from);
        timeline::Clip* target = clipById(p, *to);
        if (!source || !target) return fail(ErrorCode::NotFound, "clip not found");
        takeGrade(target->color, source->color);
        return ok();
    });
}

void ProjectController::applyGradeToNext(const QString& clipId) {
    if (!project_) return;
    const QStringList order = gradableClips();
    const qsizetype at = order.indexOf(clipId);
    if (at < 0 || at + 1 >= order.size()) {
        showMessage(at < 0 ? QStringLiteral("Select a clip to grade") : QStringLiteral("This is the last clip — there is no next clip"));
        return;
    }
    const auto from = clipIdFrom(clipId);
    const auto to = clipIdFrom(order[at + 1]);
    mutate(QStringLiteral("Grade to next clip"), [&](project::Project& p) -> Status {
        const timeline::Clip* source = clipById(p, *from);
        timeline::Clip* target = clipById(p, *to);
        if (!source || !target) return fail(ErrorCode::NotFound, "clip not found");
        takeGrade(target->color, source->color);
        return ok();
    });
}

void ProjectController::applyGradeToAll(const QString& clipId) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Grade to all clips"), [&](project::Project& p) -> Status {
        const timeline::Clip* source = clipById(p, *id);
        if (!source) return fail(ErrorCode::NotFound, "clip not found");
        const timeline::ColorAdjustments grade = source->color;
        for (auto& t : p.timeline.tracks) {
            for (auto& c : t.clips) {
                if (c.id != *id && isPicture(p, t, c)) takeGrade(c.color, grade);
            }
        }
        return ok();
    });
}

}  // namespace lectern::ui
