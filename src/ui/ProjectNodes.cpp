// ProjectController: color nodes — secondary corrections that grade part of
// the picture (a power window, a picked color, the person or the
// background), like Resolve's serial nodes after the primary grade.

#include "ui/ProjectController.h"

#include "core/Log.h"
#include "editor/ColorGrading.h"
#include "editor/LayerGeometry.h"
#include "editor/RenderPlan.h"
#include "ui/FrameGrab.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace lectern::ui {

namespace {

using Node = timeline::ColorAdjustments::Node;
using Grade = timeline::ColorAdjustments::Grade;

QString qs(const std::string& s) { return QString::fromStdString(s); }

timeline::Clip* clipById(project::Project& p, const timeline::ClipId& id) {
    for (auto& t : p.timeline.tracks) {
        for (auto& c : t.clips) {
            if (c.id == id) return &c;
        }
    }
    return nullptr;
}

Node* nodeById(timeline::Clip& c, const QString& nodeId) {
    const std::string key = nodeId.toStdString();
    for (auto& n : c.color.nodes) {
        if (n.id == key) return &n;
    }
    return nullptr;
}

QVariantMap wheelView(const timeline::ColorAdjustments::Wheel& w) { return {{"x", w.x}, {"y", w.y}, {"master", w.master}}; }

QVariantList curveView(const std::vector<timeline::Vec2>& pts) {
    QVariantList out;
    for (const auto& p : pts) out.append(QVariantMap{{"x", p.x}, {"y", p.y}});
    return out;
}

/// Sets one grade field by the Color page's key; false for unknown keys.
bool setGradeField(Grade& g, const QString& key, double value) {
    const double v = std::clamp(value, key == QLatin1String("exposure") ? -2.0 : key == QLatin1String("pivot") ? 0.0 : -1.0,
                                key == QLatin1String("exposure") ? 2.0 : 1.0);
    if (key == QLatin1String("exposure")) g.exposure = v;
    else if (key == QLatin1String("brightness")) g.brightness = v;
    else if (key == QLatin1String("contrast")) g.contrast = v;
    else if (key == QLatin1String("pivot")) g.pivot = v;
    else if (key == QLatin1String("shadows")) g.shadows = v;
    else if (key == QLatin1String("highlights")) g.highlights = v;
    else if (key == QLatin1String("saturation")) g.saturation = v;
    else if (key == QLatin1String("colorBoost")) g.colorBoost = v;
    else if (key == QLatin1String("hue")) g.hue = v;
    else if (key == QLatin1String("temperature")) g.temperature = v;
    else if (key == QLatin1String("tint")) g.tint = v;
    else return false;
    return true;
}

timeline::ColorAdjustments::Wheel* gradeWheel(Grade& g, const QString& name) {
    if (name == QLatin1String("lift")) return &g.lift;
    if (name == QLatin1String("gamma")) return &g.gammaWheel;
    if (name == QLatin1String("gain")) return &g.gain;
    if (name == QLatin1String("offset")) return &g.offset;
    return nullptr;
}

int channelIndex(const QString& channel) {
    if (channel == QLatin1String("y")) return 0;
    if (channel == QLatin1String("r")) return 1;
    if (channel == QLatin1String("g")) return 2;
    if (channel == QLatin1String("b")) return 3;
    return -1;
}

std::vector<timeline::Vec2> pointsFrom(const QVariantList& list) {
    std::vector<timeline::Vec2> pts;
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        const double x = m.contains("x") ? m.value("x").toDouble() : v.toList().value(0).toDouble();
        const double y = m.contains("y") ? m.value("y").toDouble() : v.toList().value(1).toDouble();
        if (std::isfinite(x) && std::isfinite(y)) pts.push_back({std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)});
    }
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    std::vector<timeline::Vec2> out;
    for (const auto& p : pts) {
        if (!out.empty() && p.x - out.back().x < 0.01) out.back() = p;
        else out.push_back(p);
    }
    if (out.size() < 2) out.clear();
    return out;
}

double number(const QVariantMap& values, const char* key, double fallback, double lo, double hi) {
    if (!values.contains(QLatin1String(key))) return fallback;
    bool ok = false;
    const double v = values.value(QLatin1String(key)).toDouble(&ok);
    return ok && std::isfinite(v) ? std::clamp(v, lo, hi) : fallback;
}

QString kindLabel(const QString& kind, int number) {
    if (kind == QLatin1String("person")) return QStringLiteral("Person");
    if (kind == QLatin1String("background")) return QStringLiteral("Background");
    if (kind == QLatin1String("circle")) return QStringLiteral("Circle");
    if (kind == QLatin1String("rectangle")) return QStringLiteral("Rectangle");
    if (kind == QLatin1String("gradient")) return QStringLiteral("Gradient");
    if (kind == QLatin1String("color")) return QStringLiteral("Color key");
    return QStringLiteral("Node %1").arg(number, 2, 10, QLatin1Char('0'));
}

}  // namespace

QVariantList ProjectController::nodesView(const timeline::ColorAdjustments& c) {
    QVariantList out;
    for (const Node& n : c.nodes) {
        const Grade& g = n.grade;
        const auto& w = n.window;
        const auto& q = n.qualifier;
        out.append(QVariantMap{
            {"id", qs(n.id)},
            {"label", qs(n.label)},
            {"enabled", n.enabled},
            {"invert", n.invert},
            {"subject", qs(n.subject)},
            {"window", QVariantMap{{"shape", qs(w.shape)}, {"x", w.x}, {"y", w.y}, {"width", w.width}, {"height", w.height},
                                   {"rotation", w.rotation}, {"softness", w.softness}, {"invert", w.invert}}},
            {"qualifier", QVariantMap{{"enabled", q.enabled}, {"hue", q.hue}, {"hueWidth", q.hueWidth}, {"hueSoft", q.hueSoft},
                                      {"satLow", q.satLow}, {"satHigh", q.satHigh}, {"satSoft", q.satSoft}, {"lumLow", q.lumLow},
                                      {"lumHigh", q.lumHigh}, {"lumSoft", q.lumSoft}, {"invert", q.invert}}},
            // The grade, with the same keys as the clip's primary correction in `selection`.
            {"exposure", g.exposure},
            {"brightness", g.brightness},
            {"contrast", g.contrast},
            {"pivot", g.pivot},
            {"shadows", g.shadows},
            {"highlights", g.highlights},
            {"saturation", g.saturation},
            {"colorBoost", g.colorBoost},
            {"hue", g.hue},
            {"temperature", g.temperature},
            {"tint", g.tint},
            {"lift", wheelView(g.lift)},
            {"gammaWheel", wheelView(g.gammaWheel)},
            {"gain", wheelView(g.gain)},
            {"offset", wheelView(g.offset)},
            {"curveY", curveView(g.curves[0])},
            {"curveR", curveView(g.curves[1])},
            {"curveG", curveView(g.curves[2])},
            {"curveB", curveView(g.curves[3])},
            {"hueVsHue", curveView(g.hslCurves[timeline::kHueVsHue])},
            {"hueVsSat", curveView(g.hslCurves[timeline::kHueVsSat])},
            {"hueVsLum", curveView(g.hslCurves[timeline::kHueVsLum])},
            {"lumVsSat", curveView(g.hslCurves[timeline::kLumVsSat])},
            {"satVsSat", curveView(g.hslCurves[timeline::kSatVsSat])},
            {"satVsLum", curveView(g.hslCurves[timeline::kSatVsLum])},
        });
    }
    return out;
}

QString ProjectController::addNode(const QString& clipId, const QString& kind) {
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return {};
    static const QStringList kKinds{QString(), QStringLiteral("whole"), QStringLiteral("person"), QStringLiteral("background"),
                                    QStringLiteral("circle"), QStringLiteral("rectangle"), QStringLiteral("gradient"),
                                    QStringLiteral("color")};
    if (!kKinds.contains(kind)) {
        showMessage(QStringLiteral("Unknown node kind “%1”").arg(kind));
        return {};
    }
    QString created;
    mutate(QStringLiteral("Add node: ") + kindLabel(kind, 0).section(QLatin1Char(' '), 0, 0), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        if (c->color.nodes.size() >= timeline::ColorAdjustments::kMaxNodes) {
            return fail(ErrorCode::InvalidArgument, "a clip can have at most 8 nodes");
        }
        std::set<std::string> used;
        for (const auto& n : c->color.nodes) used.insert(n.id);
        int number = 2;  // node 01 is the clip's primary correction
        while (used.contains("n" + std::to_string(number))) ++number;
        Node n;
        n.id = "n" + std::to_string(number);
        n.label = kindLabel(kind, static_cast<int>(c->color.nodes.size()) + 2).toStdString();
        if (kind == QLatin1String("person") || kind == QLatin1String("background")) n.subject = kind.toStdString();
        if (kind == QLatin1String("circle") || kind == QLatin1String("rectangle") || kind == QLatin1String("gradient")) {
            n.window.shape = kind.toStdString();
            if (kind == QLatin1String("circle")) {  // a round spot on the subject
                const project::MediaSource* m = p.findMedia(c->media);
                const double aspect = m && m->info.video && m->info.video->height > 0
                                          ? static_cast<double>(m->info.video->width) / m->info.video->height
                                          : 16.0 / 9.0;
                n.window.width = 0.35;
                n.window.height = 0.35 * aspect;  // round on the picture
                n.window.softness = 0.35;
            } else if (kind == QLatin1String("gradient")) {  // the sky
                n.window.y = 0.25;
                n.window.height = 0.6;
            } else {
                n.window.width = 0.6;
                n.window.height = 0.6;
                n.window.softness = 0.15;
            }
        }
        if (kind == QLatin1String("color")) n.qualifier.enabled = true;  // picked next
        c->color.nodes.push_back(n);
        created = qs(n.id);
        return ok();
    });
    return created;
}

void ProjectController::removeNode(const QString& clipId, const QString& nodeId) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Delete node"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        auto& nodes = c->color.nodes;
        const auto before = nodes.size();
        std::erase_if(nodes, [&](const Node& n) { return n.id == nodeId.toStdString(); });
        return nodes.size() == before ? fail(ErrorCode::NotFound, "node not found") : ok();
    });
}

void ProjectController::moveNode(const QString& clipId, const QString& nodeId, int direction) {
    const auto id = clipIdFrom(clipId);
    if (!id || direction == 0) return;
    mutate(QStringLiteral("Reorder nodes"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        auto& nodes = c->color.nodes;
        const auto it = std::find_if(nodes.begin(), nodes.end(), [&](const Node& n) { return n.id == nodeId.toStdString(); });
        if (it == nodes.end()) return fail(ErrorCode::NotFound, "node not found");
        const auto at = static_cast<std::ptrdiff_t>(it - nodes.begin());
        const auto to = std::clamp<std::ptrdiff_t>(at + (direction > 0 ? 1 : -1), 0, static_cast<std::ptrdiff_t>(nodes.size()) - 1);
        std::swap(nodes[static_cast<std::size_t>(at)], nodes[static_cast<std::size_t>(to)]);
        return ok();
    });
}

void ProjectController::setNodeEnabled(const QString& clipId, const QString& nodeId, bool enabled) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(enabled ? QStringLiteral("Enable node") : QStringLiteral("Disable node"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        n->enabled = enabled;
        return ok();
    });
}

void ProjectController::setNodeLabel(const QString& clipId, const QString& nodeId, const QString& label) {
    const auto id = clipIdFrom(clipId);
    const QString text = label.trimmed().left(40);
    if (!id || text.isEmpty()) return;
    mutate(QStringLiteral("Rename node"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        n->label = text.toStdString();
        return ok();
    });
}

void ProjectController::resetNode(const QString& clipId, const QString& nodeId) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Reset node grade"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        n->grade = {};
        return ok();
    });
}

void ProjectController::setNodeValue(const QString& clipId, const QString& nodeId, const QString& key, double value) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Adjust node"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        if (!setGradeField(n->grade, key, value)) return fail(ErrorCode::InvalidArgument, "unknown adjustment " + key.toStdString());
        return ok();
    }, QStringLiteral("node:") + nodeId + key + clipId);
}

void ProjectController::setNodeWheel(const QString& clipId, const QString& nodeId, const QString& wheel, double x, double y,
                                     double master) {
    const auto id = clipIdFrom(clipId);
    if (!id || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(master)) return;
    mutate(QStringLiteral("Node color wheel"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        auto* w = gradeWheel(n->grade, wheel);
        if (!w) return fail(ErrorCode::InvalidArgument, "unknown wheel " + wheel.toStdString());
        const double r = std::hypot(x, y);
        w->x = r > 1 ? x / r : x;
        w->y = r > 1 ? y / r : y;
        w->master = std::clamp(master, -1.0, 1.0);
        return ok();
    }, QStringLiteral("nodewheel:") + nodeId + wheel + clipId);
}

void ProjectController::setNodeCurve(const QString& clipId, const QString& nodeId, const QString& channel, const QVariantList& points) {
    const auto id = clipIdFrom(clipId);
    const int index = channelIndex(channel);
    if (!id || index < 0) return;
    const auto pts = pointsFrom(points);
    mutate(QStringLiteral("Node curve"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        n->grade.curves[static_cast<std::size_t>(index)] = pts;
        return ok();
    }, QStringLiteral("nodecurve:") + nodeId + channel + clipId);
}

void ProjectController::setNodeWindow(const QString& clipId, const QString& nodeId, const QVariantMap& values) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Node window"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        auto& w = n->window;
        if (values.contains(QStringLiteral("shape"))) {
            const QString shape = values.value(QStringLiteral("shape")).toString();
            if (!shape.isEmpty() && shape != QLatin1String("circle") && shape != QLatin1String("rectangle") &&
                shape != QLatin1String("gradient")) {
                return fail(ErrorCode::InvalidArgument, "unknown window shape " + shape.toStdString());
            }
            w.shape = shape.toStdString();
        }
        w.x = number(values, "x", w.x, -1.0, 2.0);
        w.y = number(values, "y", w.y, -1.0, 2.0);
        w.width = number(values, "width", w.width, 0.002, 4.0);
        w.height = number(values, "height", w.height, 0.002, 4.0);
        w.rotation = number(values, "rotation", w.rotation, -360.0, 360.0);
        w.softness = number(values, "softness", w.softness, 0.0, 1.0);
        if (values.contains(QStringLiteral("invert"))) w.invert = values.value(QStringLiteral("invert")).toBool();
        return ok();
    }, QStringLiteral("nodewindow:") + nodeId + clipId);
}

void ProjectController::setNodeQualifier(const QString& clipId, const QString& nodeId, const QVariantMap& values) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Node qualifier"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        auto& q = n->qualifier;
        if (values.contains(QStringLiteral("enabled"))) q.enabled = values.value(QStringLiteral("enabled")).toBool();
        q.hue = number(values, "hue", q.hue, 0.0, 1.0);
        q.hueWidth = number(values, "hueWidth", q.hueWidth, 0.0, 0.5);
        q.hueSoft = number(values, "hueSoft", q.hueSoft, 0.0, 0.5);
        q.satLow = number(values, "satLow", q.satLow, 0.0, 1.0);
        q.satHigh = number(values, "satHigh", q.satHigh, 0.0, 1.0);
        q.satSoft = number(values, "satSoft", q.satSoft, 0.0, 0.5);
        q.lumLow = number(values, "lumLow", q.lumLow, 0.0, 1.0);
        q.lumHigh = number(values, "lumHigh", q.lumHigh, 0.0, 1.0);
        q.lumSoft = number(values, "lumSoft", q.lumSoft, 0.0, 0.5);
        if (values.contains(QStringLiteral("invert"))) q.invert = values.value(QStringLiteral("invert")).toBool();
        if (q.satLow > q.satHigh) std::swap(q.satLow, q.satHigh);
        if (q.lumLow > q.lumHigh) std::swap(q.lumLow, q.lumHigh);
        return ok();
    }, QStringLiteral("nodequal:") + nodeId + clipId);
}

void ProjectController::setNodeSubject(const QString& clipId, const QString& nodeId, const QString& subject) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    if (!subject.isEmpty() && subject != QLatin1String("person") && subject != QLatin1String("background")) return;
    mutate(subject.isEmpty() ? QStringLiteral("Node: whole picture") : QStringLiteral("Node: ") + subject,
           [&](project::Project& p) -> Status {
               timeline::Clip* c = clipById(p, *id);
               Node* n = c ? nodeById(*c, nodeId) : nullptr;
               if (!n) return fail(ErrorCode::NotFound, "node not found");
               n->subject = subject.toStdString();
               return ok();
           });
}

void ProjectController::setNodeInvert(const QString& clipId, const QString& nodeId, bool invert) {
    const auto id = clipIdFrom(clipId);
    if (!id) return;
    mutate(QStringLiteral("Invert node"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        n->invert = invert;
        return ok();
    });
}

QVariantMap ProjectController::sourceFrame(const QString& clipId, double seconds) const {
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return {};
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, Time::fromSecondsF(seconds));
    for (const auto& l : plan.layers) {
        if (l.kind != editor::LayerKind::Media || l.clip != *id) continue;
        const editor::NormRect full = editor::sourceFrameRect(l, plan.width, plan.height);
        const editor::NormRect shown = editor::visibleMediaRect(l, plan.width, plan.height);
        return {{"x", full.x}, {"y", full.y}, {"w", full.w}, {"h", full.h},
                {"mirror", l.mirror}, {"rotation", l.rotation},
                {"aspect", l.sourceAspect > 0 ? l.sourceAspect : 16.0 / 9.0},
                {"visibleX", shown.x}, {"visibleY", shown.y}, {"visibleW", shown.w}, {"visibleH", shown.h},
                {"centerX", l.box.x + l.box.w / 2}, {"centerY", l.box.y + l.box.h / 2}};
    }
    return {};
}

std::optional<std::array<double, 3>> ProjectController::pictureColorAt(const QString& clipId, double x, double y, double seconds,
                                                                       int nodes, bool withoutHsl) const {
    const auto id = clipIdFrom(clipId);
    if (!id || !project_) return std::nullopt;
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, Time::fromSecondsF(seconds));
    const editor::VisualLayer* layer = nullptr;
    for (const auto& l : plan.layers) {
        if (l.kind == editor::LayerKind::Media && l.clip == *id) layer = &l;
    }
    if (!layer) return std::nullopt;
    // Canvas point → source point (undo the layer's rotation about its box center).
    const double W = plan.width;
    const double H = plan.height;
    double px = x * W;
    double py = y * H;
    if (std::abs(layer->rotation) > 0.01) {
        const double cx = (layer->box.x + layer->box.w / 2) * W;
        const double cy = (layer->box.y + layer->box.h / 2) * H;
        const double a = -layer->rotation * 3.14159265358979323846 / 180.0;
        const double dx = px - cx;
        const double dy = py - cy;
        px = cx + dx * std::cos(a) - dy * std::sin(a);
        py = cy + dx * std::sin(a) + dy * std::cos(a);
    }
    const editor::NormRect full = editor::sourceFrameRect(*layer, plan.width, plan.height);
    double u = (px / W - full.x) / full.w;
    const double v = (py / H - full.y) / full.h;
    if (layer->mirror) u = 1.0 - u;
    if (u < 0 || u > 1 || v < 0 || v > 1) return std::nullopt;

    // Source → input color → the clip's grade → the first `nodes` nodes.
    const project::MediaSource* m = project_->findMedia(layer->media);
    if (!m) return std::nullopt;
    QImage frame = grabFrame(qs((dir_ / m->path).string()), layer->sourceTime.toSecondsF(), 360);
    if (frame.isNull()) return std::nullopt;
    frame = frame.convertToFormat(QImage::Format_RGB32);
    editor::applyInputColor(frame, layer->input);
    static editor::LutCache luts;
    const auto lut = layer->color.lut.empty() ? nullptr : luts.get(layer->color.lut, dir_);
    editor::ColorParams grade = layer->color;
    if (withoutHsl) grade.hsl = {};
    editor::applyColor(frame, grade, lut.get());
    if (nodes > 0 && !layer->nodes.empty()) {
        std::vector<editor::NodeParams> before(layer->nodes.begin(), layer->nodes.begin() + std::min<std::size_t>(nodes, layer->nodes.size()));
        editor::SourceMap map;
        map.du = 1.0 / frame.width();
        map.dv = 1.0 / frame.height();
        map.aspect = static_cast<double>(frame.width()) / frame.height();
        editor::applyNodes(frame, before, map);
    }
    const int fx = std::clamp(static_cast<int>(u * frame.width()), 0, frame.width() - 1);
    const int fy = std::clamp(static_cast<int>(v * frame.height()), 0, frame.height() - 1);
    double r = 0, g = 0, b = 0;
    int count = 0;
    for (int yy = std::max(0, fy - 2); yy <= std::min(frame.height() - 1, fy + 2); ++yy) {
        for (int xx = std::max(0, fx - 2); xx <= std::min(frame.width() - 1, fx + 2); ++xx) {
            const QRgb c = frame.pixel(xx, yy);
            r += qRed(c);
            g += qGreen(c);
            b += qBlue(c);
            ++count;
        }
    }
    return std::array<double, 3>{r / count / 255.0, g / count / 255.0, b / count / 255.0};
}

QVariantMap ProjectController::colorAt(const QString& clipId, double x, double y, double seconds, const QString& nodeId) const {
    const auto id = clipIdFrom(clipId);
    const timeline::Clip* clip = id && project_ ? project_->timeline.findClip(*id) : nullptr;
    if (!clip) return {};
    const int nodes = nodeId.isEmpty() ? 0 : std::max(0, editor::enabledNodeIndex(clip->color, nodeId.toStdString()));
    const auto rgb = pictureColorAt(clipId, x, y, seconds, nodes, true);
    if (!rgb) return {};
    const auto [hue, sat, lum] = editor::qualifierAxes((*rgb)[0], (*rgb)[1], (*rgb)[2]);
    return {{"r", (*rgb)[0]}, {"g", (*rgb)[1]}, {"b", (*rgb)[2]}, {"hue", hue}, {"sat", sat}, {"lum", lum}};
}

bool ProjectController::pickNodeColor(const QString& clipId, const QString& nodeId, double x, double y, double seconds) {
    const auto id = clipIdFrom(clipId);
    const timeline::Clip* clip = id && project_ ? project_->timeline.findClip(*id) : nullptr;
    if (!clip) return false;
    // The picture this node receives there: the clip's grade and the nodes before it.
    const int index = std::max(0, editor::enabledNodeIndex(clip->color, nodeId.toStdString()));
    const auto rgb = pictureColorAt(clipId, x, y, seconds, index, false);
    if (!rgb) return false;
    const auto q = editor::qualifierAround((*rgb)[0], (*rgb)[1], (*rgb)[2]);
    LEC_INFO("color", "picked rgb({:.0f}, {:.0f}, {:.0f}) for node {}", (*rgb)[0] * 255, (*rgb)[1] * 255, (*rgb)[2] * 255,
             nodeId.toStdString());
    return mutate(QStringLiteral("Pick color"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        const bool inverted = n->qualifier.invert;
        n->qualifier = q;
        n->qualifier.invert = inverted;
        return ok();
    });
}

// ---- HSL curves --------------------------------------------------------------------

namespace {
int hslIndex(const QString& curve) {
    static const QStringList kNames{QStringLiteral("hueVsHue"), QStringLiteral("hueVsSat"), QStringLiteral("hueVsLum"),
                                    QStringLiteral("lumVsSat"), QStringLiteral("satVsSat"), QStringLiteral("satVsLum")};
    return static_cast<int>(kNames.indexOf(curve));
}

/// HSL curve points: sorted, clamped, merged when closer than 1 % in x; fewer
/// than two points is no curve. A flat line stays (the editor is adding points);
/// the renderers ignore it.
std::vector<timeline::Vec2> hslPoints(const QVariantList& list) {
    std::vector<timeline::Vec2> pts;
    for (const QVariant& v : list) {
        const QVariantMap m = v.toMap();
        const double x = m.contains("x") ? m.value("x").toDouble() : v.toList().value(0).toDouble();
        const double y = m.contains("y") ? m.value("y").toDouble() : v.toList().value(1).toDouble();
        if (std::isfinite(x) && std::isfinite(y)) pts.push_back({std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0)});
    }
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    std::vector<timeline::Vec2> out;
    for (const auto& p : pts) {
        if (!out.empty() && p.x - out.back().x < 0.01) out.back() = p;
        else out.push_back(p);
    }
    if (out.size() < 2) out.clear();
    return out;
}
}  // namespace

void ProjectController::setHslCurve(const QString& clipId, const QString& curve, const QVariantList& points) {
    const auto id = clipIdFrom(clipId);
    const int index = hslIndex(curve);
    if (!id || index < 0) return;
    const auto pts = hslPoints(points);
    mutate(QStringLiteral("HSL curve"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        c->color.hslCurves[static_cast<std::size_t>(index)] = pts;
        return ok();
    }, QStringLiteral("hsl:") + curve + clipId);
}

void ProjectController::setNodeHslCurve(const QString& clipId, const QString& nodeId, const QString& curve, const QVariantList& points) {
    const auto id = clipIdFrom(clipId);
    const int index = hslIndex(curve);
    if (!id || index < 0) return;
    const auto pts = hslPoints(points);
    mutate(QStringLiteral("Node HSL curve"), [&](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, *id);
        Node* n = c ? nodeById(*c, nodeId) : nullptr;
        if (!n) return fail(ErrorCode::NotFound, "node not found");
        n->grade.hslCurves[static_cast<std::size_t>(index)] = pts;
        return ok();
    }, QStringLiteral("nodehsl:") + nodeId + curve + clipId);
}

QVariantList ProjectController::hslCurveSamples(const QVariantList& points, const QString& curve, int count) const {
    std::vector<timeline::Vec2> pts;
    for (const QVariant& v : points) {
        const QVariantMap m = v.toMap();
        pts.push_back({std::clamp(m.value("x").toDouble(), 0.0, 1.0), std::clamp(m.value("y").toDouble(), 0.0, 1.0)});
    }
    std::sort(pts.begin(), pts.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    const bool periodic = hslIndex(curve) >= 0 && hslIndex(curve) <= timeline::kHueVsLum;
    QVariantList out;
    count = std::clamp(count, 2, 1024);
    for (int i = 0; i < count; ++i) out.append(editor::evaluateHslCurve(pts, static_cast<double>(i) / (count - 1), periodic));
    return out;
}

}  // namespace lectern::ui
