// ProjectController: direct manipulation on the preview canvas — picking a
// layer under the pointer, its outline, and moving/resizing it.

#include "ui/ProjectController.h"

#include "editor/Compositor.h"
#include "editor/LayerGeometry.h"
#include "editor/RenderPlan.h"

#include <algorithm>
#include <cmath>

namespace lectern::ui {

namespace {

Time sec(double s) { return Time::fromSecondsF(s); }

timeline::Clip* clipById(project::Project& p, const timeline::ClipId& id) {
    for (auto& t : p.timeline.tracks) {
        for (auto& c : t.clips) {
            if (c.id == id) return &c;
        }
    }
    return nullptr;
}

/// Index of the clip's layer in the plan, or −1.
int layerIndex(const editor::RenderPlan& plan, const timeline::ClipId& id) {
    for (std::size_t i = 0; i < plan.layers.size(); ++i) {
        if (plan.layers[i].clip == id) return static_cast<int>(i);
    }
    return -1;
}

bool finite(double v) { return std::isfinite(v); }

}  // namespace

QString ProjectController::layerAt(double x, double y, double seconds) const {
    if (!project_ || !finite(x) || !finite(y)) return {};
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, sec(seconds));
    const int i = editor::layerAt(plan, x, y);
    return i < 0 ? QString() : QString::fromStdString(plan.layers[static_cast<std::size_t>(i)].clip.toString());
}

QVariantMap ProjectController::layerBox(const QString& clipId, double seconds) const {
    const auto id = clipIdFrom(clipId);
    if (!project_ || !id) return {};
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, sec(seconds));
    const int i = layerIndex(plan, *id);
    if (i < 0) return {};
    const editor::VisualLayer& l = plan.layers[static_cast<std::size_t>(i)];
    const editor::LayerBounds b = editor::layerBounds(plan)[static_cast<std::size_t>(i)];
    const double pixelAspect = b.rect.h > 0 ? (b.rect.w * plan.width) / (b.rect.h * plan.height) : 1.0;
    // Corner handles keep the picture's proportions; the camera (cropped to
    // fill) may also be reshaped with the edge handles.
    const bool camera = l.role == "camera";
    return {{"x", b.rect.x},
            {"y", b.rect.y},
            {"w", b.rect.w},
            {"h", b.rect.h},
            {"circle", b.circle},
            {"role", QString::fromStdString(l.role)},
            {"aspect", pixelAspect},
            {"edges", camera && !l.circle},
            {"vertical", l.kind == editor::LayerKind::Subtitle}};
}

ProjectController::Mutation ProjectController::layerRectEdit(const QString& clipId, double seconds, double x, double y,
                                                             double w, double h, QString& label) const {
    const auto id = clipIdFrom(clipId);
    if (!project_ || !id || !finite(x) || !finite(y) || !finite(w) || !finite(h) || w <= 0 || h <= 0) return {};
    const Time t = sec(seconds);
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, t);
    const int index = layerIndex(plan, *id);
    if (index < 0) return {};
    const editor::VisualLayer layer = plan.layers[static_cast<std::size_t>(index)];
    const editor::NormRect current = editor::layerBounds(plan)[static_cast<std::size_t>(index)].rect;
    const double W = plan.width;
    const double H = plan.height;
    const timeline::ClipId clip = *id;
    const std::string preset = plan.layout;

    if (layer.role == "screen" || layer.role == "camera") {
        label = QStringLiteral("Arrange layout");
        const bool camera = layer.role == "camera";
        return [=](project::Project& p) -> Status {
            const timeline::Clip* c = clipById(p, clip);
            if (!c) return fail(ErrorCode::NotFound, "clip not found");
            editor::NormRect box{x, y, w, h};
            if (!camera && p.style.screenPadding > 0) {
                // The screen is inset by the padding: grow the slot by it.
                const double dy = p.style.screenPadding;
                const double dx = dy * H / W;
                box = {box.x - dx, box.y - dy, box.w + 2 * dx, box.h + 2 * dy};
            }
            // Undo the clip's own adjustment of its slot (normally none).
            const Time local = t - c->range.start;
            const auto pos = c->transform.position.evaluate(local);
            const double scale = std::clamp(c->transform.scale.evaluate(local).x, 0.1, 3.0);
            const double sw = std::clamp(box.w / scale, 0.02, 8.0);
            const double sh = std::clamp(box.h / scale, 0.02, 8.0);
            const double cx = box.x + box.w / 2 - (pos.x - 0.5);
            const double cy = box.y + box.h / 2 - (pos.y - 0.5);
            const project::SlotRect slot{std::clamp(cx - sw / 2, -5.0, 5.0), std::clamp(cy - sh / 2, -5.0, 5.0), sw, sh};
            auto& custom = p.style.layouts[project::layoutKey(preset, p.canvas.width, p.canvas.height)];
            (camera ? custom.camera : custom.screen) = slot;
            return ok();
        };
    }

    const double cx = x + w / 2;
    const double cy = y + h / 2;
    const double ratio = current.h > 0 ? h / current.h : 1.0;
    if (layer.kind == editor::LayerKind::Subtitle) {
        label = QStringLiteral("Subtitle placement");
        return [=](project::Project& p) -> Status {
            p.style.subtitlePosition = std::clamp(cy, 0.1, 0.95);
            p.style.subtitleSize = std::clamp(p.style.subtitleSize * ratio, 0.02, 0.1);
            return ok();
        };
    }
    label = QStringLiteral("Move on canvas");
    const bool text = layer.kind == editor::LayerKind::Text;
    return [=](project::Project& p) -> Status {
        timeline::Clip* c = clipById(p, clip);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        if (text && c->text) {
            // Resizing scales the font; the outline includes this instant's
            // animation offset, which is not part of the position.
            c->text->style.size = std::clamp(c->text->style.size * ratio, 8.0, 480.0);
            c->transform.position.value = {std::clamp(cx - layer.textDx, -0.5, 1.5), std::clamp(cy - layer.textDy, -0.5, 1.5)};
        } else {
            // Overlays: centered at the position, `scale` of the canvas width wide.
            c->transform.position.value = {std::clamp(cx, -0.5, 1.5), std::clamp(cy, -0.5, 1.5)};
            const double width = std::clamp(w, 0.02, 2.0);
            c->transform.scale.value = {width, width};
        }
        return ok();
    };
}

void ProjectController::setLayerRect(const QString& clipId, double seconds, double x, double y, double w, double h) {
    QString label;
    const Mutation edit = layerRectEdit(clipId, seconds, x, y, w, h, label);
    if (edit) mutate(label, edit, QStringLiteral("layer:") + clipId);
}

void ProjectController::previewLayerRect(const QString& clipId, double seconds, double x, double y, double w, double h) {
    QString label;
    const Mutation edit = layerRectEdit(clipId, seconds, x, y, w, h, label);
    if (!edit) return;
    auto work = std::make_shared<project::Project>(*project_);
    if (!edit(*work) || !work->validate()) return;
    snapshot_ = std::move(work);
    emit snapshotChanged();
}

void ProjectController::cancelPreview() {
    if (!project_) return;
    snapshot_ = std::make_shared<const project::Project>(*project_);
    emit snapshotChanged();
}

void ProjectController::resetLayerRect(const QString& clipId, double seconds) {
    const auto id = clipIdFrom(clipId);
    if (!project_ || !id) return;
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, sec(seconds));
    const int index = layerIndex(plan, *id);
    if (index < 0) return;
    const editor::VisualLayer& layer = plan.layers[static_cast<std::size_t>(index)];
    if (layer.role == "screen" || layer.role == "camera") {
        const bool camera = layer.role == "camera";
        mutate(QStringLiteral("Reset placement"), [&](project::Project& p) -> Status {
            const auto it = p.style.layouts.find(project::layoutKey(plan.layout, p.canvas.width, p.canvas.height));
            if (it == p.style.layouts.end()) return ok();
            (camera ? it->second.camera : it->second.screen).reset();
            if (it->second.empty()) p.style.layouts.erase(it);
            return ok();
        });
        return;
    }
    mutate(QStringLiteral("Reset placement"), [&](project::Project& p) -> Status {
        if (layer.kind == editor::LayerKind::Subtitle) {
            const project::StyleSettings defaults;
            p.style.subtitlePosition = defaults.subtitlePosition;
            p.style.subtitleSize = defaults.subtitleSize;
            return ok();
        }
        timeline::Clip* c = clipById(p, *id);
        if (!c) return fail(ErrorCode::NotFound, "clip not found");
        if (c->text) {
            const auto defaults = editor::textPresetDefaults(c->text->preset);
            c->transform.position.value = {defaults.x, defaults.y};
            c->text->style.size = defaults.style.size;
        } else {
            c->transform.position.value = {0.5, 0.5};
            c->transform.scale.value = {0.3, 0.3};
        }
        return ok();
    });
}

QString ProjectController::roleClipAt(const QString& role, double seconds) const {
    if (!project_) return {};
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, sec(seconds));
    for (const auto& l : plan.layers) {
        if (QString::fromStdString(l.role) == role) return QString::fromStdString(l.clip.toString());
    }
    return {};
}

void ProjectController::setLayerWidth(const QString& clipId, double seconds, double width) {
    const QVariantMap box = layerBox(clipId, seconds);
    if (box.isEmpty() || !finite(width) || width <= 0) return;
    const double w = box.value("w").toDouble();
    const double h = box.value("h").toDouble();
    if (w <= 0) return;
    const double nh = h * width / w;
    const double cx = box.value("x").toDouble() + w / 2;
    const double cy = box.value("y").toDouble() + h / 2;
    setLayerRect(clipId, seconds, cx - width / 2, cy - nh / 2, width, nh);
}

bool ProjectController::layoutCustomized(double seconds) const {
    if (!project_) return false;
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, sec(seconds));
    const auto it =
        project_->style.layouts.find(project::layoutKey(plan.layout, project_->canvas.width, project_->canvas.height));
    return it != project_->style.layouts.end() && !it->second.empty();
}

void ProjectController::resetLayoutCustomization(double seconds) {
    if (!project_) return;
    const editor::RenderPlan plan = editor::buildRenderPlan(*project_, sec(seconds));
    mutate(QStringLiteral("Reset layout"), [&](project::Project& p) -> Status {
        p.style.layouts.erase(project::layoutKey(plan.layout, p.canvas.width, p.canvas.height));
        return ok();
    });
}

QVariantList ProjectController::textAnimations() const {
    QVariantList out;
    for (const auto& [id, name] : editor::textAnimations()) {
        out.append(QVariantMap{{"id", QString::fromStdString(id)}, {"name", QString::fromStdString(name)}});
    }
    return out;
}

bool ProjectController::segmentationAvailable() const { return editor::hasPersonSegmenter(); }

}  // namespace lectern::ui
