// Keyframed clip transform properties (position, scale, rotation, opacity,
// crop, anchor) at timeline times.

#include "ui/ProjectController.h"

#include <cmath>
#include <set>

namespace lectern::ui {

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

timeline::Clip* findClip(project::Project& p, const timeline::ClipId& id) {
    for (auto& t : p.timeline.tracks) {
        for (auto& c : t.clips) {
            if (c.id == id) return &c;
        }
    }
    return nullptr;
}

std::optional<Time> clipLocalTime(const timeline::Clip& c, double timelineSec) {
    const Time t = sec(timelineSec);
    if (t < c.range.start || t >= c.range.end()) return std::nullopt;
    return t - c.range.start;
}

double timelineFromLocal(const timeline::Clip& c, Time local) { return (c.range.start + local).toSecondsF(); }

}  // namespace

QVariantMap ProjectController::clipTransformAt(const QString& clipId, double timelineSec) const {
    QVariantMap out;
    if (!project_) return out;
    const auto id = clipIdFrom(clipId);
    if (!id) return out;
    const timeline::Clip* c = findClip(*project_, *id);
    if (!c) return out;
    const auto local = clipLocalTime(*c, timelineSec);
    const Time lt = local ? *local : Time::zero();
    const auto pos = c->transform.position.evaluate(lt);
    const auto scale = c->transform.scale.evaluate(lt);
    const auto anchor = c->transform.anchor.evaluate(lt);
    const auto crop = c->crop.evaluate(lt);
    out.insert(QStringLiteral("position"), pos.x != 0 || pos.y != 0 || c->transform.position.isAnimated());
    out.insert(QStringLiteral("x"), pos.x);
    out.insert(QStringLiteral("y"), pos.y);
    out.insert(QStringLiteral("scale"), scale.x);
    out.insert(QStringLiteral("rotation"), c->transform.rotation.evaluate(lt));
    out.insert(QStringLiteral("opacity"), c->opacity.evaluate(lt));
    out.insert(QStringLiteral("anchorX"), anchor.x);
    out.insert(QStringLiteral("anchorY"), anchor.y);
    out.insert(QStringLiteral("cropL"), crop.x);
    out.insert(QStringLiteral("cropT"), crop.y);
    out.insert(QStringLiteral("cropR"), crop.z);
    out.insert(QStringLiteral("cropB"), crop.w);
    out.insert(QStringLiteral("onClip"), local.has_value());
    return out;
}

QVariantList ProjectController::clipAllKeyframeTimes(const QString& clipId) const {
    std::set<double> unique;
    for (const char* prop : {"opacity", "position", "scale", "rotation", "crop", "anchor"}) {
        for (const QVariant& v : clipKeyframeTimes(clipId, QString::fromLatin1(prop))) unique.insert(v.toDouble());
    }
    QVariantList out;
    for (const double t : unique) out.append(t);
    return out;
}

QVariantList ProjectController::clipKeyframeTimes(const QString& clipId, const QString& property) const {
    QVariantList out;
    if (!project_) return out;
    const auto id = clipIdFrom(clipId);
    if (!id) return out;
    const timeline::Clip* c = findClip(*project_, *id);
    if (!c) return out;
    const auto emitKeys = [&](const auto& animated) {
        for (const auto& k : animated.keys) out.append(timelineFromLocal(*c, k.time));
    };
    const QString p = property.toLower();
    if (p == QLatin1String("position")) emitKeys(c->transform.position);
    else if (p == QLatin1String("scale")) emitKeys(c->transform.scale);
    else if (p == QLatin1String("rotation")) emitKeys(c->transform.rotation);
    else if (p == QLatin1String("opacity")) emitKeys(c->opacity);
    else if (p == QLatin1String("crop")) emitKeys(c->crop);
    else if (p == QLatin1String("anchor")) emitKeys(c->transform.anchor);
    return out;
}

void ProjectController::setClipKeyframe(const QString& clipId, const QString& property, double timelineSec,
                                        const QVariant& value, bool keyframe) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    const QString p = property.toLower();
    mutate(QStringLiteral("Keyframe"), [&](project::Project& proj) -> Status {
        timeline::Clip* c = findClip(proj, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        const auto local = clipLocalTime(*c, timelineSec);
        const bool atKey = keyframe && local.has_value();
        const Time lt = local ? *local : Time::zero();

        if (p == QLatin1String("position") && value.typeId() == QMetaType::QVariantMap) {
            double x = c->transform.position.evaluate(lt).x;
            double y = c->transform.position.evaluate(lt).y;
            const QVariantMap m = value.toMap();
            if (m.contains(QStringLiteral("x"))) x = m.value(QStringLiteral("x")).toDouble();
            if (m.contains(QStringLiteral("y"))) y = m.value(QStringLiteral("y")).toDouble();
            x = std::clamp(x, -0.5, 1.5);
            y = std::clamp(y, -0.5, 1.5);
            if (atKey) c->transform.position.setKey(*local, {x, y});
            else c->transform.position.value = {x, y};
        } else if (p == QLatin1String("positionx")) {
            const auto cur = c->transform.position.evaluate(lt);
            const double x = std::clamp(value.toDouble(), -0.5, 1.5);
            if (atKey) c->transform.position.setKey(*local, {x, cur.y});
            else c->transform.position.value = {x, cur.y};
        } else if (p == QLatin1String("positiony")) {
            const auto cur = c->transform.position.evaluate(lt);
            const double y = std::clamp(value.toDouble(), -0.5, 1.5);
            if (atKey) c->transform.position.setKey(*local, {cur.x, y});
            else c->transform.position.value = {cur.x, y};
        } else if (p == QLatin1String("scale")) {
            const double s = std::clamp(value.toDouble(), 0.02, 3.0);
            if (atKey) c->transform.scale.setKey(*local, {s, s});
            else c->transform.scale.value = {s, s};
        } else if (p == QLatin1String("rotation")) {
            const double r = value.toDouble();
            if (atKey) c->transform.rotation.setKey(*local, r);
            else c->transform.rotation.value = r;
        } else if (p == QLatin1String("opacity")) {
            const double o = std::clamp(value.toDouble(), 0.0, 1.0);
            if (atKey) c->opacity.setKey(*local, o);
            else c->opacity.value = o;
        } else if (p == QLatin1String("anchorx")) {
            const auto cur = c->transform.anchor.evaluate(lt);
            const double ax = std::clamp(value.toDouble(), 0.0, 1.0);
            if (atKey) c->transform.anchor.setKey(*local, {ax, cur.y});
            else c->transform.anchor.value = {ax, cur.y};
        } else if (p == QLatin1String("anchory")) {
            const auto cur = c->transform.anchor.evaluate(lt);
            const double ay = std::clamp(value.toDouble(), 0.0, 1.0);
            if (atKey) c->transform.anchor.setKey(*local, {cur.x, ay});
            else c->transform.anchor.value = {cur.x, ay};
        } else if (p == QLatin1String("crop")) {
            timeline::Vec4 crop = c->crop.evaluate(lt);
            if (value.typeId() == QMetaType::QVariantMap) {
                const QVariantMap m = value.toMap();
                if (m.contains(QStringLiteral("l"))) crop.x = m.value(QStringLiteral("l")).toDouble();
                if (m.contains(QStringLiteral("t"))) crop.y = m.value(QStringLiteral("t")).toDouble();
                if (m.contains(QStringLiteral("r"))) crop.z = m.value(QStringLiteral("r")).toDouble();
                if (m.contains(QStringLiteral("b"))) crop.w = m.value(QStringLiteral("b")).toDouble();
            }
            crop.x = std::clamp(crop.x, 0.0, 0.45);
            crop.y = std::clamp(crop.y, 0.0, 0.45);
            crop.z = std::clamp(crop.z, 0.0, 0.45);
            crop.w = std::clamp(crop.w, 0.0, 0.45);
            if (atKey) c->crop.setKey(*local, crop);
            else c->crop.value = crop;
        } else if (p.startsWith(QLatin1String("crop"))) {
            timeline::Vec4 crop = c->crop.evaluate(lt);
            const double v = std::clamp(value.toDouble(), 0.0, 0.45);
            if (p == QLatin1String("cropl")) crop.x = v;
            else if (p == QLatin1String("cropt")) crop.y = v;
            else if (p == QLatin1String("cropr")) crop.z = v;
            else if (p == QLatin1String("cropb")) crop.w = v;
            else return fail(ErrorCode::InvalidArgument, "unknown crop side");
            if (atKey) c->crop.setKey(*local, crop);
            else c->crop.value = crop;
        } else {
            return fail(ErrorCode::InvalidArgument, "unknown keyframe property");
        }
        return ok();
    }, QStringLiteral("kf:") + property + clipId);
}

void ProjectController::removeClipKeyframe(const QString& clipId, const QString& property, double timelineSec) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Remove keyframe"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        const auto local = clipLocalTime(*c, timelineSec);
        if (!local) return ok();
        const QString prop = property.toLower();
        auto eraseAt = [&](auto& animated) {
            auto& keys = animated.keys;
            keys.erase(std::remove_if(keys.begin(), keys.end(), [&](const auto& k) { return k.time == *local; }),
                       keys.end());
        };
        if (prop == QLatin1String("position")) eraseAt(c->transform.position);
        else if (prop == QLatin1String("scale")) eraseAt(c->transform.scale);
        else if (prop == QLatin1String("rotation")) eraseAt(c->transform.rotation);
        else if (prop == QLatin1String("opacity")) eraseAt(c->opacity);
        else if (prop == QLatin1String("crop")) eraseAt(c->crop);
        else if (prop == QLatin1String("anchor")) eraseAt(c->transform.anchor);
        else return fail(ErrorCode::InvalidArgument, "unknown property");
        return ok();
    });
}

void ProjectController::clearClipKeyframes(const QString& clipId, const QString& property) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Clear keyframes"), [&](project::Project& p) -> Status {
        timeline::Clip* c = findClip(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        const QString prop = property.toLower();
        if (prop == QLatin1String("position")) c->transform.position.keys.clear();
        else if (prop == QLatin1String("scale")) c->transform.scale.keys.clear();
        else if (prop == QLatin1String("rotation")) c->transform.rotation.keys.clear();
        else if (prop == QLatin1String("opacity")) c->opacity.keys.clear();
        else if (prop == QLatin1String("crop")) c->crop.keys.clear();
        else if (prop == QLatin1String("anchor")) c->transform.anchor.keys.clear();
        else if (prop.isEmpty() || prop == QLatin1String("all")) {
            c->transform.position.keys.clear();
            c->transform.scale.keys.clear();
            c->transform.rotation.keys.clear();
            c->transform.anchor.keys.clear();
            c->opacity.keys.clear();
            c->crop.keys.clear();
        } else return fail(ErrorCode::InvalidArgument, "unknown property");
        return ok();
    });
}

}  // namespace lectern::ui
