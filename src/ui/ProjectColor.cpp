// ProjectController: color grading — basic adjustments, color wheels and LUTs.

#include "ui/ProjectController.h"

#include "editor/ColorGrading.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace lectern::ui {

namespace {

timeline::Clip* clipById(project::Project& p, const timeline::ClipId& id) {
    for (auto& t : p.timeline.tracks) {
        for (auto& c : t.clips) {
            if (c.id == id) return &c;
        }
    }
    return nullptr;
}

timeline::ColorAdjustments::Wheel* wheelOf(timeline::ColorAdjustments& c, const QString& name) {
    if (name == QLatin1String("lift")) return &c.lift;
    if (name == QLatin1String("gamma")) return &c.gammaWheel;
    if (name == QLatin1String("gain")) return &c.gain;
    return nullptr;
}

}  // namespace

void ProjectController::setColorValue(const QString& clipId, const QString& key, double value) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Adjust color"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        timeline::ColorAdjustments& col = c->color;
        const double v = std::clamp(value, key == QLatin1String("exposure") ? -2.0 : -1.0, key == QLatin1String("exposure") ? 2.0 : 1.0);
        if (key == QLatin1String("exposure")) col.exposure.value = v;
        else if (key == QLatin1String("brightness")) col.brightness.value = v;
        else if (key == QLatin1String("contrast")) col.contrast.value = v;
        else if (key == QLatin1String("saturation")) col.saturation.value = v;
        else if (key == QLatin1String("temperature")) col.temperature.value = v;
        else if (key == QLatin1String("tint")) col.tint.value = v;
        else if (key == QLatin1String("lutAmount")) col.lutAmount = std::clamp(value, 0.0, 1.0);
        else return fail(ErrorCode::InvalidArgument, "unknown adjustment " + key.toStdString());
        return ok();
    }, QStringLiteral("color:") + key + clipId);
}

void ProjectController::resetColor(const QString& clipId) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Reset color"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color = {};
        return ok();
    });
}

void ProjectController::setColorValues(const QString& clipId, const QVariantMap& values) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Color preset"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        timeline::ColorAdjustments& col = c->color;
        // A look replaces the basic adjustments; wheels and the LUT (often a
        // log conversion the look builds on) stay.
        timeline::ColorAdjustments fresh;
        fresh.lift = col.lift;
        fresh.gammaWheel = col.gammaWheel;
        fresh.gain = col.gain;
        fresh.lut = col.lut;
        fresh.lutAmount = col.lutAmount;
        col = fresh;
        auto take = [&](const char* key, timeline::Animated<double>& target, double lo, double hi) {
            if (values.contains(QLatin1String(key))) target.value = std::clamp(values.value(QLatin1String(key)).toDouble(), lo, hi);
        };
        take("exposure", col.exposure, -2, 2);
        take("brightness", col.brightness, -1, 1);
        take("contrast", col.contrast, -1, 1);
        take("saturation", col.saturation, -1, 1);
        take("temperature", col.temperature, -1, 1);
        take("tint", col.tint, -1, 1);
        return ok();
    });
}

void ProjectController::setColorWheel(const QString& clipId, const QString& wheel, double x, double y, double master) {
    const auto id = clipIdFrom(clipId);
    if (!id || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(master)) return;
    mutate(QStringLiteral("Color wheel"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        timeline::ColorAdjustments::Wheel* w = wheelOf(c->color, wheel);
        if (!w) return fail(ErrorCode::InvalidArgument, "unknown wheel " + wheel.toStdString());
        const double r = std::hypot(x, y);
        const double k = r > 1.0 ? 1.0 / r : 1.0;  // the puck stays on the wheel
        w->x = x * k;
        w->y = y * k;
        w->master = std::clamp(master, -1.0, 1.0);
        return ok();
    }, QStringLiteral("wheel:") + wheel + clipId);
}

void ProjectController::setColorLut(const QString& clipId, const QString& ref) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    const std::string lut = ref.toStdString();
    if (lut.starts_with("builtin:") && !editor::builtinLut(lut)) {
        showMessage(QStringLiteral("Unknown color conversion"));
        return;
    }
    mutate(lut.empty() ? QStringLiteral("Remove LUT") : QStringLiteral("Apply LUT"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color.lut = lut;
        c->color.lutAmount = 1.0;
        return ok();
    });
}

void ProjectController::importLut(const QString& clipId, const QUrl& file) {
    if (!project_ || dir_.empty()) return;
    const QString local = file.isLocalFile() ? file.toLocalFile() : file.toString();
    const std::filesystem::path source = local.toStdString();
    auto parsed = editor::loadCubeLut(source);
    if (!parsed) {
        showMessage(QStringLiteral("This LUT could not be read: ") + QString::fromStdString(parsed.error().message()));
        return;
    }
    // Keep the project self-contained: LUTs live in media/luts.
    std::error_code ec;
    const std::filesystem::path folder = dir_ / "media" / "luts";
    std::filesystem::create_directories(folder, ec);
    std::filesystem::path target = folder / source.filename();
    if (std::filesystem::exists(target) && !std::filesystem::equivalent(target, source, ec)) {
        for (int i = 2; std::filesystem::exists(target); ++i) {
            target = folder / (source.stem().string() + " " + std::to_string(i) + source.extension().string());
        }
    }
    if (!std::filesystem::exists(target)) {
        std::filesystem::copy_file(source, target, ec);
        if (ec) {
            showMessage(QStringLiteral("The LUT could not be copied into the project: ") + QString::fromStdString(ec.message()));
            return;
        }
    }
    setColorLut(clipId, QString::fromStdString(std::filesystem::relative(target, dir_, ec).generic_string()));
}

void ProjectController::applyToRole(const QString& clipId, const QString& what) {
    const auto id = clipIdFrom(clipId);
    if (!id || (what != QLatin1String("effects") && what != QLatin1String("color"))) return;
    auto group = [](const project::MediaSource* m) {
        if (!m) return -1;
        if (m->role == project::MediaRole::Screen) return 0;
        if (m->role == project::MediaRole::Camera || m->role == project::MediaRole::Phone) return 1;
        return -1;
    };
    mutate(QStringLiteral("Apply to the whole recording"), [&](project::Project& p) -> Status {
        const timeline::Clip* source = clipById(p, *id);
        if (!source) return fail(ErrorCode::NotFound, "clip not found");
        const int role = group(p.findMedia(source->media));
        if (role < 0) return fail(ErrorCode::InvalidArgument, "only screen and camera clips can share their look");
        const timeline::Clip copy = *source;
        for (auto& t : p.timeline.tracks) {
            for (auto& c : t.clips) {
                if (c.id == copy.id || group(p.findMedia(c.media)) != role) continue;
                if (what == QLatin1String("color")) {
                    c.color = copy.color;
                } else {
                    c.effects = copy.effects;
                    for (auto& e : c.effects) e.id = timeline::EffectId::generate();
                }
            }
        }
        return ok();
    });
}

QVariantList ProjectController::builtinLuts() const {
    QVariantList out;
    for (const auto& lut : editor::builtinLuts()) {
        out.append(QVariantMap{{"id", QString::fromStdString(lut.id)}, {"name", QString::fromStdString(lut.name)}});
    }
    return out;
}

}  // namespace lectern::ui
