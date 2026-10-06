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
    if (name == QLatin1String("offset")) return &c.offset;
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
        const bool unit = key == QLatin1String("pivot");
        const double v = std::clamp(value, key == QLatin1String("exposure") ? -2.0 : unit ? 0.0 : -1.0,
                                    key == QLatin1String("exposure") ? 2.0 : 1.0);
        if (key == QLatin1String("exposure")) col.exposure.value = v;
        else if (key == QLatin1String("brightness")) col.brightness.value = v;
        else if (key == QLatin1String("contrast")) col.contrast.value = v;
        else if (key == QLatin1String("saturation")) col.saturation.value = v;
        else if (key == QLatin1String("temperature")) col.temperature.value = v;
        else if (key == QLatin1String("tint")) col.tint.value = v;
        else if (key == QLatin1String("lutAmount")) col.lutAmount = std::clamp(value, 0.0, 1.0);
        else if (key == QLatin1String("pivot")) col.pivot = v;
        else if (key == QLatin1String("shadows")) col.shadows.value = v;
        else if (key == QLatin1String("highlights")) col.highlights.value = v;
        else if (key == QLatin1String("colorBoost")) col.colorBoost.value = v;
        else if (key == QLatin1String("hue")) col.hue.value = v;
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

QVariantList ProjectController::wheelChannels(const QString& wheel, double x, double y, double master) const {
    const auto c = editor::wheelChroma({x, y, master});
    // The same formulas as editor::colorCurves, shown the way Resolve labels them.
    if (wheel == QLatin1String("gain")) {
        const double base = 1.0 + 0.5 * master;
        return {base, base + 2.0 * c[0], base + 2.0 * c[1], base + 2.0 * c[2]};
    }
    if (wheel == QLatin1String("gamma")) return {master, master + 2.0 * c[0], master + 2.0 * c[1], master + 2.0 * c[2]};
    const double base = 0.25 * master;  // lift, offset
    return {base, base + c[0], base + c[1], base + c[2]};
}

namespace {
int curveIndex(const QString& channel) {
    if (channel == QLatin1String("y")) return 0;
    if (channel == QLatin1String("r")) return 1;
    if (channel == QLatin1String("g")) return 2;
    if (channel == QLatin1String("b")) return 3;
    return -1;
}
std::vector<timeline::Vec2> curvePoints(const QVariantList& list) {
    std::vector<timeline::Vec2> pts;
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        const double x = m.contains("x") ? m.value("x").toDouble() : v.toList().value(0).toDouble();
        const double y = m.contains("y") ? m.value("y").toDouble() : v.toList().value(1).toDouble();
        if (std::isfinite(x) && std::isfinite(y)) pts.push_back({std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)});
    }
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    // Points closer than 1 % in x merge (a curve is a function of x).
    std::vector<timeline::Vec2> out;
    for (const auto& p : pts) {
        if (!out.empty() && p.x - out.back().x < 0.01) out.back() = p;
        else out.push_back(p);
    }
    return out;
}
}  // namespace

void ProjectController::setCurve(const QString& clipId, const QString& channel, const QVariantList& points) {
    const auto id = clipIdFrom(clipId);
    const int index = curveIndex(channel);
    if (!id || index < 0) return;
    auto pts = curvePoints(points);
    if (pts.size() < 2) pts.clear();
    mutate(QStringLiteral("Curve"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color.curves[static_cast<std::size_t>(index)] = pts;
        return ok();
    }, QStringLiteral("curve:") + channel + clipId);
}

QVariantList ProjectController::curveSamples(const QVariantList& points, int count) const {
    const auto pts = curvePoints(points);
    QVariantList out;
    count = std::clamp(count, 2, 1024);
    for (int i = 0; i < count; ++i) {
        const double x = static_cast<double>(i) / (count - 1);
        out.append(pts.size() < 2 ? x : editor::evaluateCurve(pts, x));
    }
    return out;
}

void ProjectController::autoBalance(const QString& clipId, double meanR, double meanG, double meanB) {
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return;
    const timeline::Clip* clip = nullptr;
    for (const auto& t : project_->timeline.tracks)
        for (const auto& c : t.clips)
            if (c.id == *id) clip = &c;
    if (!clip || meanR <= 0 || meanG <= 0 || meanB <= 0) return;
    // Temp scales R up / B down (±0.15), Tint scales G down and R, B up (0.10 / 0.05) —
    // the channel gains of editor::colorCurves. Solve for equal channel averages.
    const double temp0 = clip->color.temperature.value;
    const double tint0 = clip->color.tint.value;
    auto gains = [](double t, double n) {
        return std::array<double, 3>{1.0 + 0.15 * t + 0.05 * n, 1.0 - 0.10 * n, 1.0 - 0.15 * t + 0.05 * n};
    };
    const auto g0 = gains(temp0, tint0);
    // The source's own averages, before the current Temp/Tint.
    const double r = meanR / g0[0], g = meanG / g0[1], b = meanB / g0[2];
    double best = 1e9, bestT = temp0, bestN = tint0;
    for (double t = -1.0; t <= 1.0001; t += 0.01) {
        for (double n = -1.0; n <= 1.0001; n += 0.01) {
            const auto k = gains(t, n);
            const double R = r * k[0], G = g * k[1], B = b * k[2];
            const double m = (R + G + B) / 3.0;
            const double err = (R - m) * (R - m) + (G - m) * (G - m) + (B - m) * (B - m) + 1e-4 * (t * t + n * n);
            if (err < best) { best = err; bestT = t; bestN = n; }
        }
    }
    mutate(QStringLiteral("Auto balance"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color.temperature.value = std::round(bestT * 100) / 100;
        c->color.tint.value = std::round(bestN * 100) / 100;
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

void ProjectController::setInputColorSpace(const QString& clipId, const QString& space) {
    static const QStringList known{QStringLiteral("auto"), QStringLiteral("rec709"), QStringLiteral("srgb"),
                                   QStringLiteral("display-p3"), QStringLiteral("rec2020"), QStringLiteral("rec2020-hlg"),
                                   QStringLiteral("rec2020-pq")};
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    if (!known.contains(space)) {
        showMessage(QStringLiteral("Unknown color space"));
        return;
    }
    mutate(QStringLiteral("Source color"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color.inputColorSpace = space.toStdString();
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
