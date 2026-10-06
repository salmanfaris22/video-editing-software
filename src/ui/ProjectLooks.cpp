// ProjectController: everything about how the video looks and sounds —
// setup, layout, style, text, overlays, effects, color, audio, subtitles.

#include "ui/ProjectController.h"

#include "core/Log.h"
#include "editor/LayoutPresets.h"
#include "editor/RenderPlan.h"
#include "media/MediaProbe.h"
#include "timeline/Subtitles.h"

#include <QColor>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <map>
#include <sstream>

namespace lectern::ui {

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }
Time sec(double s) { return Time::fromSecondsF(s); }

const char* effectType(const QString& type) {
    if (type == QLatin1String("blur")) return editor::kEffectBlur;
    if (type == QLatin1String("vignette")) return editor::kEffectVignette;
    if (type == QLatin1String("zoom")) return editor::kEffectZoom;
    if (type == QLatin1String("background-blur")) return editor::kEffectBackgroundBlur;
    if (type == QLatin1String("film-grain")) return editor::kEffectFilmGrain;
    if (type == QLatin1String("glow")) return editor::kEffectGlow;
    if (type == QLatin1String("halation")) return editor::kEffectHalation;
    if (type == QLatin1String("film-emulation")) return editor::kEffectFilmEmulation;
    return nullptr;
}

bool isTextAnimation(const std::string& id) {
    const auto& all = editor::textAnimations();
    return std::any_of(all.begin(), all.end(), [&](const auto& a) { return a.first == id; });
}

timeline::Clip* findClip(project::Project& p, const timeline::ClipId& id, timeline::Track** track = nullptr) {
    for (auto& t : p.timeline.tracks) {
        for (auto& c : t.clips) {
            if (c.id == id) {
                if (track) *track = &t;
                return &c;
            }
        }
    }
    return nullptr;
}

/// A track of `kind` named like `name` where `range` fits, created when none has room.
timeline::Track& trackWithRoom(project::Project& p, timeline::TrackKind kind, const std::string& name, TimeRange range) {
    for (auto& t : p.timeline.tracks) {
        if (t.kind != kind || t.locked || !t.name.starts_with(name)) continue;
        const bool free = std::none_of(t.clips.begin(), t.clips.end(), [&](const auto& c) { return c.range.intersects(range); });
        if (free) return t;
    }
    int n = 1;
    for (const auto& t : p.timeline.tracks) n += t.kind == kind && t.name.starts_with(name) ? 1 : 0;
    p.timeline.tracks.push_back({timeline::TrackId::generate(), kind, n == 1 ? name : name + " " + std::to_string(n)});
    return p.timeline.tracks.back();
}

std::filesystem::path uniqueTarget(const std::filesystem::path& dir, const std::filesystem::path& source) {
    std::filesystem::path target = dir / source.filename();
    for (int i = 2; std::filesystem::exists(target); ++i) {
        target = dir / (source.stem().string() + " " + std::to_string(i) + source.extension().string());
    }
    return target;
}

}  // namespace

// ---- Setup ---------------------------------------------------------------

void ProjectController::setTitle(const QString& title) {
    const QString clean = title.trimmed();
    if (clean.isEmpty()) return;
    mutate(QStringLiteral("Rename"), [&](project::Project& p) -> Status {
        p.title = clean.toStdString();
        return ok();
    }, QStringLiteral("title"));
}

void ProjectController::setCanvasAspect(const QString& aspect) {
    static const std::map<QString, std::pair<int, int>> sizes{
        {"16:9", {1920, 1080}}, {"9:16", {1080, 1920}}, {"1:1", {1080, 1080}}, {"4:5", {1080, 1350}}};
    const auto it = sizes.find(aspect);
    if (it == sizes.end()) return;
    mutate(QStringLiteral("Canvas"), [&](project::Project& p) -> Status {
        // Keep the project's resolution class (e.g. a 4K recording stays 4K wide).
        const double scale = std::max(1.0, std::max(p.canvas.width, p.canvas.height) / 1920.0);
        p.canvas.aspect = aspect.toStdString();
        p.canvas.width = static_cast<int>(std::lround(it->second.first * scale / 2.0)) * 2;
        p.canvas.height = static_cast<int>(std::lround(it->second.second * scale / 2.0)) * 2;
        p.exportSettings.width = p.canvas.width;
        p.exportSettings.height = p.canvas.height;
        return ok();
    });
}

void ProjectController::setFrameRate(int fps) {
    if (fps != 24 && fps != 25 && fps != 30 && fps != 50 && fps != 60) return;
    mutate(QStringLiteral("Frame rate"), [&](project::Project& p) -> Status {
        p.canvas.frameRate = FrameRate(fps, 1);
        return ok();
    });
}

void ProjectController::setBackground(const QString& color, const QString& color2) {
    if (!QColor::isValidColorName(color) || (!color2.isEmpty() && !QColor::isValidColorName(color2))) return;
    mutate(QStringLiteral("Background"), [&](project::Project& p) -> Status {
        p.canvas.backgroundColor = color.toStdString();
        p.style.backgroundColor2 = color2.toStdString();
        return ok();
    });
}

// ---- Layout --------------------------------------------------------------

void ProjectController::setLayoutPreset(const QString& preset) {
    if (!editor::isKnownLayout(preset.toStdString())) return;
    mutate(QStringLiteral("Layout"), [&](project::Project& p) -> Status {
        timeline::edit::setLayoutAll(p.timeline, preset.toStdString());
        return ok();
    });
}

void ProjectController::setLayoutFrom(const QString& preset, double seconds) {
    if (!editor::isKnownLayout(preset.toStdString())) return;
    mutate(QStringLiteral("Layout from here"),
           [&](project::Project& p) { return timeline::edit::setLayoutFrom(p.timeline, sec(seconds), preset.toStdString()); });
}

QString ProjectController::layoutAt(double seconds) const {
    return project_ ? qs(timeline::edit::layoutAt(project_->timeline, sec(seconds))) : QStringLiteral("screen.only");
}

void ProjectController::setRoleTransform(const QString& role, double x, double y, double scale) {
    if (role != QLatin1String("screen") && role != QLatin1String("camera")) return;
    const bool camera = role == QLatin1String("camera");
    mutate(QStringLiteral("Position"), [&](project::Project& p) -> Status {
        for (auto& track : p.timeline.tracks) {
            for (auto& clip : track.clips) {
                const auto* media = p.findMedia(clip.media);
                if (!media) continue;
                const bool isCamera = media->role == project::MediaRole::Camera || media->role == project::MediaRole::Phone;
                if (isCamera != camera || (!isCamera && media->role != project::MediaRole::Screen)) continue;
                clip.transform.position.value = {std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)};
                const double s = std::clamp(scale, 0.2, 2.0);
                clip.transform.scale.value = {s, s};
            }
        }
        return ok();
    }, QStringLiteral("role-transform:") + role);
}

// ---- Style ---------------------------------------------------------------

void ProjectController::setStyleValue(const QString& key, const QVariant& value) {
    mutate(QStringLiteral("Style"), [&](project::Project& p) -> Status {
        project::StyleSettings& s = p.style;
        const double d = value.toDouble();
        const std::string text = value.toString().toStdString();
        if (key == QLatin1String("screenPadding")) s.screenPadding = std::clamp(d, 0.0, 0.25);
        else if (key == QLatin1String("screenRadius")) s.screenRadius = std::clamp(d, 0.0, 0.08);
        else if (key == QLatin1String("screenShadow")) s.screenShadow = std::clamp(d, 0.0, 1.0);
        else if (key == QLatin1String("cameraShape") && (text == "rect" || text == "rounded" || text == "circle")) s.cameraShape = text;
        else if (key == QLatin1String("cameraBorder")) s.cameraBorder = std::clamp(d, 0.0, 0.02);
        else if (key == QLatin1String("cameraBorderColor") && QColor::isValidColorName(value.toString())) s.cameraBorderColor = text;
        else if (key == QLatin1String("cameraMirror")) s.cameraMirror = value.toBool();
        else if (key == QLatin1String("subtitleSize")) s.subtitleSize = std::clamp(d, 0.02, 0.1);
        else if (key == QLatin1String("subtitleColor") && QColor::isValidColorName(value.toString())) s.subtitleColor = text;
        else if (key == QLatin1String("subtitleBackground") && (text.empty() || QColor::isValidColorName(value.toString()))) s.subtitleBackground = text;
        else if (key == QLatin1String("subtitlePosition")) s.subtitlePosition = std::clamp(d, 0.1, 0.95);
        else return fail(ErrorCode::InvalidArgument, "unknown style setting " + key.toStdString());
        return ok();
    }, QStringLiteral("style:") + key);
}

// ---- Text ----------------------------------------------------------------

void ProjectController::addText(const QString& text, double start, double duration, const QString& preset) {
    const QString clean = text.trimmed();
    if (clean.isEmpty()) return;
    const timeline::ClipId id = timeline::ClipId::generate();
    if (mutate(QStringLiteral("Add text"), [&](project::Project& p) -> Status {
            const auto defaults = editor::textPresetDefaults(preset.toStdString());
            timeline::Clip clip;
            clip.id = id;
            clip.kind = timeline::ClipKind::Text;
            clip.name = "Text";
            clip.range = {sec(std::max(0.0, start)), sec(std::max(0.2, duration))};
            clip.text = timeline::TextContent{clean.toStdString(), preset.toStdString(), defaults.style, defaults.animation};
            clip.transform.position = timeline::Vec2{defaults.x, defaults.y};
            return trackWithRoom(p, timeline::TrackKind::Overlay, "Text", clip.range).insertClip(std::move(clip));
        })) {
        selectClip(qs(id.toString()));
    }
}

void ProjectController::setText(const QString& clipId, const QString& text) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Edit text"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c || !c->text) return fail(ErrorCode::NotFound, "text clip not found");
        c->text->text = text.toStdString();
        return ok();
    }, QStringLiteral("text:") + clipId);
}

void ProjectController::setTextValue(const QString& clipId, const QString& key, const QVariant& value) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Text style"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c || !c->text) return fail(ErrorCode::NotFound, "text clip not found");
        timeline::TextStyle& s = c->text->style;
        const std::string text = value.toString().toStdString();
        if (key == QLatin1String("preset")) {
            const auto defaults = editor::textPresetDefaults(text);
            c->text->preset = text;
            s = defaults.style;
            c->transform.position = timeline::Vec2{defaults.x, defaults.y};
        } else if (key == QLatin1String("size")) {
            s.size = std::clamp(value.toDouble(), 12.0, 240.0);
        } else if (key == QLatin1String("weight")) {
            s.weight = std::clamp(value.toInt(), 100, 900);
        } else if (key == QLatin1String("color") && QColor::isValidColorName(value.toString())) {
            s.color = text;
        } else if (key == QLatin1String("background") && (text.empty() || QColor::isValidColorName(value.toString()))) {
            s.background = text;
        } else if (key == QLatin1String("alignment") && (text == "left" || text == "center" || text == "right")) {
            s.alignment = text;
        } else if ((key == QLatin1String("animationIn") || key == QLatin1String("animationOut")) && isTextAnimation(text)) {
            (key == QLatin1String("animationIn") ? c->text->animation.in : c->text->animation.out) = text;
        } else if (key == QLatin1String("inDuration") || key == QLatin1String("outDuration")) {
            const Time d = sec(std::clamp(value.toDouble(), 0.0, 5.0));
            (key == QLatin1String("inDuration") ? c->text->animation.inDuration : c->text->animation.outDuration) = d;
        } else {
            return fail(ErrorCode::InvalidArgument, "unknown text setting " + key.toStdString());
        }
        return ok();
    }, QStringLiteral("text-style:") + key + clipId);
}

// ---- Clip properties -------------------------------------------------------

void ProjectController::setClipPosition(const QString& clipId, double x, double y) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Move on canvas"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->transform.position.value = {std::clamp(x, -0.5, 1.5), std::clamp(y, -0.5, 1.5)};
        return ok();
    }, QStringLiteral("position:") + clipId);
}

void ProjectController::setClipScale(const QString& clipId, double scale) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Resize"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        const double s = std::clamp(scale, 0.05, 3.0);
        c->transform.scale.value = {s, s};
        return ok();
    }, QStringLiteral("scale:") + clipId);
}

void ProjectController::setClipOpacity(const QString& clipId, double opacity) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Opacity"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->opacity.value = std::clamp(opacity, 0.0, 1.0);
        return ok();
    }, QStringLiteral("opacity:") + clipId);
}

void ProjectController::setClipEnabled(const QString& clipId, bool enabled) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(enabled ? QStringLiteral("Enable clip") : QStringLiteral("Disable clip"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->enabled = enabled;
        return ok();
    });
}

void ProjectController::setClipTiming(const QString& clipId, double start, double duration) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Timing"), [&](project::Project& p) -> Status {
        LEC_TRY(timeline::edit::moveClip(p.timeline, *id, sec(start)));
        const timeline::Clip* c = p.timeline.findClip(*id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        return timeline::edit::trimEnd(p.timeline, *id, c->range.start + sec(std::max(0.1, duration)), mediaBounds(p));
    });
}

void ProjectController::setEffectEnabled(const QString& clipId, const QString& type, bool enabled) {
    const auto id = clipIdFrom(clipId);
    const char* kind = effectType(type);
    if (!id || !kind) return;
    mutate(enabled ? QStringLiteral("Add effect") : QStringLiteral("Remove effect"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        std::erase_if(c->effects, [kind](const auto& e) { return e.type == kind; });
        if (enabled) {
            timeline::EffectInstance e{timeline::EffectId::generate(), kind, 1, true, {}};
            if (type == QLatin1String("zoom")) {
                e.params["scale"] = 1.5;
                e.params["x"] = 0.5;
                e.params["y"] = 0.5;
            } else if (type == QLatin1String("film-grain")) {
                e.params["amount"] = 0.35;
                e.params["size"] = 1.0;
            } else if (type == QLatin1String("glow")) {
                e.params["amount"] = 0.4;
                e.params["threshold"] = 0.75;
                e.params["radius"] = 0.5;
            } else if (type == QLatin1String("halation")) {
                e.params["amount"] = 0.5;
                e.params["threshold"] = 0.8;
                e.params["radius"] = 0.4;
            } else if (type == QLatin1String("film-emulation")) {
                e.params["amount"] = 1.0;
                e.params["stock"] = 0.0;
            } else {
                e.params["amount"] = type == QLatin1String("background-blur") ? 0.6 : 0.5;
            }
            c->effects.push_back(std::move(e));
        }
        return ok();
    });
}

void ProjectController::setEffectValue(const QString& clipId, const QString& type, const QString& param, double value) {
    const auto id = clipIdFrom(clipId);
    const char* kind = effectType(type);
    if (!id || !kind) return;
    mutate(QStringLiteral("Effect"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        for (auto& e : c->effects) {
            if (e.type == kind) {
                e.params[param.toStdString()] = value;
                return ok();
            }
        }
        return fail(ErrorCode::NotFound, "turn the effect on first");
    }, QStringLiteral("effect:") + type + param + clipId);
}

void ProjectController::setClipAudio(const QString& clipId, const QString& key, const QVariant& value) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Clip audio"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        const Time half = c->range.duration.scaled(Rational(1, 2));
        if (key == QLatin1String("gainDb")) c->audio.gainDb = std::clamp(value.toDouble(), -60.0, 12.0);
        else if (key == QLatin1String("muted")) c->audio.muted = value.toBool();
        else if (key == QLatin1String("fadeIn")) c->audio.fadeIn = std::clamp(sec(value.toDouble()), Time::zero(), half);
        else if (key == QLatin1String("fadeOut")) c->audio.fadeOut = std::clamp(sec(value.toDouble()), Time::zero(), half);
        else return fail(ErrorCode::InvalidArgument, "unknown audio setting " + key.toStdString());
        return ok();
    }, QStringLiteral("clip-audio:") + key + clipId);
}

void ProjectController::setTrackValue(const QString& trackId, const QString& key, const QVariant& value) {
    const auto id = trackIdFrom(trackId);
    if (!id) return;
    mutate(QStringLiteral("Track"), [&](project::Project& p) -> Status {
        timeline::Track* t = p.timeline.findTrack(*id);
        if (!t) return fail(ErrorCode::NotFound, "track not found");
        if (key == QLatin1String("gainDb")) t->gainDb = std::clamp(value.toDouble(), -60.0, 12.0);
        else if (key == QLatin1String("muted")) t->muted = value.toBool();
        else if (key == QLatin1String("solo")) t->solo = value.toBool();
        else if (key == QLatin1String("locked")) t->locked = value.toBool();
        else if (key == QLatin1String("hidden")) t->hidden = value.toBool();
        else return fail(ErrorCode::InvalidArgument, "unknown track setting " + key.toStdString());
        return ok();
    }, key == QLatin1String("gainDb") ? QStringLiteral("track-gain:") + trackId : QString());
}

// ---- Imports ---------------------------------------------------------------

void ProjectController::importMedia(const QUrl& file, const QString& purpose, double seconds) {
    if (!project_ || busy()) return;
    const std::filesystem::path source = file.toLocalFile().toStdString();
    const bool music = purpose == QLatin1String("music");
    setBusy(music ? QStringLiteral("Adding music…") : QStringLiteral("Adding overlay…"));
    worker_->post([this, source, music, seconds, dir = dir_] {
        std::error_code ec;
        const std::filesystem::path folder = dir / "media" / "imported";
        std::filesystem::create_directories(folder, ec);
        const std::filesystem::path target = uniqueTarget(folder, source);
        std::filesystem::copy_file(source, target, ec);
        Result<media::MediaInfo> info = ec ? Result<media::MediaInfo>(fail(ErrorCode::IoError, "cannot copy the file: " + ec.message()))
                                           : media::probeMedia(target);
        if (!info) std::filesystem::remove(target, ec);
        const std::string relative = std::filesystem::relative(target, dir, ec).generic_string();
        QMetaObject::invokeMethod(this, [this, info = std::move(info), relative, source, music, seconds] {
            setBusy({});
            if (!info) {
                showMessage(QStringLiteral("Could not add ") + qs(source.filename().string()) + QStringLiteral(": ") +
                            qs(info.error().message()));
                return;
            }
            if (music && !info->audio()) {
                showMessage(QStringLiteral("That file has no audio."));
                return;
            }
            if (!music && !info->video()) {
                showMessage(QStringLiteral("That file has no picture."));
                return;
            }
            const timeline::ClipId clipId = timeline::ClipId::generate();
            const bool ok = mutate(music ? QStringLiteral("Add music") : QStringLiteral("Add overlay"), [&](project::Project& p) -> Status {
                project::MediaSource m;
                m.id = project::MediaId::generate();
                m.kind = info->kind == media::MediaKind::Image ? project::MediaKind::Image
                         : music || !info->video()             ? project::MediaKind::Audio
                                                               : project::MediaKind::Video;
                m.role = project::MediaRole::Imported;
                m.name = source.stem().string();
                m.path = relative;
                m.info.container = info->container;
                m.info.start = info->start;
                m.info.duration = info->duration;
                if (const auto* v = info->video()) {
                    m.info.video = project::VideoMetadata{v->codec, v->video->width, v->video->height,
                                                          v->video->averageFrameRate, v->video->pixelFormat,
                                                          v->video->colorSpace, v->video->colorRange,
                                                          v->video->rotationDegrees, v->video->variableFrameRate,
                                                          v->video->colorPrimaries, v->video->colorTransfer};
                }
                if (const auto* a = info->audio()) m.info.audio = project::AudioMetadata{a->codec, a->audio->sampleRate, a->audio->channels};
                timeline::Clip clip;
                clip.id = clipId;
                clip.name = m.name;
                clip.media = m.id;
                clip.sourceIn = m.info.start;
                const Time timelineEnd = p.timeline.duration();
                if (music) {
                    const Time start = timelineEnd > Time::zero() ? sec(std::max(0.0, seconds)) : Time::zero();
                    const Time room = timelineEnd > start ? timelineEnd - start : m.info.duration;
                    const Time length =
                        timelineEnd > Time::zero() ? std::min(m.info.duration, room) : m.info.duration;
                    clip.range = {start, std::max(length, Time::fromSeconds(1))};
                    clip.audio.gainDb = -14;  // a bed under the voice
                    clip.audio.fadeIn = std::min(Time::fromSeconds(1), clip.range.duration.scaled(Rational(1, 4)));
                    clip.audio.fadeOut = std::min(Time::fromSeconds(2), clip.range.duration.scaled(Rational(1, 4)));
                } else {
                    const Time start = sec(std::max(0.0, seconds));
                    Time length = m.info.duration;
                    if (m.kind == project::MediaKind::Image) {
                        // A still lasts 5 s but never makes the video longer.
                        const Time room = timelineEnd - start;
                        length = room >= Time::fromSeconds(1) ? std::min(Time::fromSeconds(5), room) : Time::fromSeconds(5);
                    }
                    clip.range = {start, std::max(length, Time::fromMilliseconds(200))};
                    const bool logo = m.kind == project::MediaKind::Image;
                    clip.transform.position = logo ? timeline::Vec2{0.86, 0.14} : timeline::Vec2{0.5, 0.5};
                    clip.transform.scale = logo ? timeline::Vec2{0.18, 0.18} : timeline::Vec2{0.5, 0.5};
                }
                const auto kind = music ? timeline::TrackKind::Audio : timeline::TrackKind::Overlay;
                timeline::Track& track = trackWithRoom(p, kind, music ? "Music" : "Overlay", clip.range);
                p.media.push_back(std::move(m));
                return track.insertClip(std::move(clip));
            });
            if (ok) selectClip(qs(clipId.toString()));
        }, Qt::QueuedConnection);
    });
}

// ---- Subtitles -------------------------------------------------------------

void ProjectController::addSubtitle(const QString& text, double start, double duration) {
    const QString clean = text.trimmed();
    if (clean.isEmpty()) return;
    int added = 0;
    mutate(QStringLiteral("Add subtitle"), [&](project::Project& p) -> Status {
        added = timeline::addSubtitleCues(p.timeline, {{TimeRange{sec(std::max(0.0, start)), sec(std::max(0.3, duration))},
                                                       clean.toStdString()}});
        return added ? ok() : fail(ErrorCode::InvalidArgument, "there is already a subtitle at the playhead");
    });
    if (added && project_) {
        for (const auto& t : project_->timeline.tracks) {
            if (t.kind != timeline::TrackKind::Subtitle) continue;
            if (const timeline::Clip* c = t.clipAt(sec(std::max(0.0, start)) + Time::fromMilliseconds(1))) {
                selectClip(qs(c->id.toString()));
            }
        }
    }
}

void ProjectController::setSubtitleText(const QString& clipId, const QString& text) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Edit subtitle"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c || !c->subtitle) return fail(ErrorCode::NotFound, "subtitle not found");
        c->subtitle->text = text.toStdString();
        return ok();
    }, QStringLiteral("subtitle:") + clipId);
}

void ProjectController::importSubtitles(const QUrl& file) {
    std::ifstream in(std::filesystem::path(file.toLocalFile().toStdString()), std::ios::binary);
    if (!in) {
        showMessage(QStringLiteral("Could not open the subtitle file."));
        return;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto cues = timeline::parseSubtitles(buffer.str());
    if (!cues) {
        showMessage(qs(cues.error().message()));
        return;
    }
    int added = 0;
    mutate(QStringLiteral("Import subtitles"), [&](project::Project& p) -> Status {
        added = timeline::addSubtitleCues(p.timeline, *cues);
        return ok();
    });
    showInfo(QStringLiteral("Imported %1 of %2 subtitles.").arg(added).arg(cues->size()));
}

bool ProjectController::exportSubtitles(const QUrl& file) {
    if (!project_) return false;
    const auto cues = timeline::subtitleCues(project_->timeline);
    if (cues.empty()) {
        showMessage(QStringLiteral("There are no subtitles to export."));
        return false;
    }
    const std::filesystem::path path = file.toLocalFile().toStdString();
    const bool vtt = path.extension() == ".vtt";
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << (vtt ? timeline::writeVtt(cues) : timeline::writeSrt(cues));
    if (!out) {
        showMessage(QStringLiteral("Could not write the subtitle file."));
        return false;
    }
    showInfo(QStringLiteral("Saved %1 subtitles to %2.").arg(cues.size()).arg(qs(path.filename().string())));
    return true;
}

void ProjectController::clearSubtitles() {
    mutate(QStringLiteral("Remove subtitles"), [&](project::Project& p) -> Status {
        std::erase_if(p.timeline.tracks, [](const auto& t) { return t.kind == timeline::TrackKind::Subtitle; });
        return ok();
    });
}

}  // namespace lectern::ui
