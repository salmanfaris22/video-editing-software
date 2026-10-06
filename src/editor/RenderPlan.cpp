#include "editor/RenderPlan.h"

#include "editor/Looks.h"
#include "timeline/EditOps.h"

#include <algorithm>
#include <cmath>

namespace lectern::editor {

namespace {

using project::MediaRole;
using timeline::Clip;
using timeline::ClipKind;
using timeline::TrackKind;

bool isCamera(MediaRole role) { return role == MediaRole::Camera || role == MediaRole::Phone; }

double param(const timeline::EffectInstance& e, const char* name, double fallback, Time local) {
    const auto it = e.params.find(name);
    return it == e.params.end() ? fallback : it->second.evaluate(local);
}

void applyTransformKeys(VisualLayer& l, const Clip& c, Time local) {
    const auto crop = c.crop.evaluate(local);
    l.cropL = std::clamp(crop.x, 0.0, 0.45);
    l.cropT = std::clamp(crop.y, 0.0, 0.45);
    l.cropR = std::clamp(crop.z, 0.0, 0.45);
    l.cropB = std::clamp(crop.w, 0.0, 0.45);
    l.rotation = c.transform.rotation.evaluate(local);
}

InputColor inputColorOf(const timeline::ColorAdjustments& c, const project::MediaSource* m) {
    const auto* v = m && m->info.video ? &*m->info.video : nullptr;
    return resolveInputColor(c.inputColorSpace, v ? v->colorTransfer : std::string_view{}, v ? v->colorPrimaries : std::string_view{});
}

void applyEffects(VisualLayer& l, const Clip& c, Time local) {
    for (const auto& e : c.effects) {
        if (!e.enabled) continue;
        if (e.type == kEffectBlur) {
            l.blur = std::clamp(param(e, "amount", 0.5, local), 0.0, 1.0);
        } else if (e.type == kEffectVignette) {
            l.vignette = std::clamp(param(e, "amount", 0.5, local), 0.0, 1.0);
        } else if (e.type == kEffectZoom) {
            l.zoom = std::clamp(param(e, "scale", 1.5, local), 1.0, 4.0);
            l.zoomX = std::clamp(param(e, "x", 0.5, local), 0.0, 1.0);
            l.zoomY = std::clamp(param(e, "y", 0.5, local), 0.0, 1.0);
        } else if (e.type == kEffectBackgroundBlur) {
            l.backgroundBlur = std::clamp(param(e, "amount", 0.6, local), 0.0, 1.0);
        } else if (e.type == kEffectDenoise) {
            l.denoiseLuma = std::clamp(param(e, "luma", 0.3, local), 0.0, 1.0);
            l.denoiseChroma = std::clamp(param(e, "chroma", 0.5, local), 0.0, 1.0);
            l.denoiseRadius = std::clamp(param(e, "radius", 0.5, local), 0.0, 1.0);
        } else if (e.type == kEffectSharpen) {
            l.sharpen = std::clamp(param(e, "amount", 0.3, local), 0.0, 1.0);
            l.sharpenRadius = std::clamp(param(e, "radius", 0.3, local), 0.0, 1.0);
            l.sharpenCoring = std::clamp(param(e, "coring", 0.2, local), 0.0, 1.0);
        } else if (e.type == kEffectFilmGrain) {
            l.grain = std::clamp(param(e, "amount", 0.35, local), 0.0, 1.0);
            l.grainSize = std::clamp(param(e, "size", 1.0, local), 0.5, 4.0);
            const double t = (c.range.start + local).toSecondsF();
            l.grainSeed = static_cast<std::uint32_t>(static_cast<std::int64_t>(std::floor(t * 24.0)) & 0xFFFFFF);
        } else if (e.type == kEffectGlow) {
            l.glow = std::clamp(param(e, "amount", 0.4, local), 0.0, 1.0);
            l.glowThreshold = std::clamp(param(e, "threshold", 0.75, local), 0.0, 0.98);
            l.glowRadius = std::clamp(param(e, "radius", 0.5, local), 0.0, 1.0);
        } else if (e.type == kEffectHalation) {
            l.halation = std::clamp(param(e, "amount", 0.5, local), 0.0, 1.0);
            l.halationThreshold = std::clamp(param(e, "threshold", 0.8, local), 0.0, 0.98);
            l.halationRadius = std::clamp(param(e, "radius", 0.4, local), 0.0, 1.0);
        } else if (e.type == kEffectFilmEmulation) {
            const double amount = std::clamp(param(e, "amount", 1.0, local), 0.0, 1.0);
            const auto& stocks = filmStocks();
            const auto index = static_cast<std::size_t>(std::clamp(std::lround(param(e, "stock", 0.0, local)), 0L,
                                                                   static_cast<long>(stocks.size()) - 1));
            if (amount > 0) {  // the print stage: a whole-picture node after the clip's own nodes
                LookSettings stock;
                static_cast<timeline::ColorAdjustments::Grade&>(stock) = stocks[index].grade;
                NodeParams print;
                print.grade = paramsOf(scaleLook(stock, amount));
                l.nodes.push_back(std::move(print));
            }
        }
    }
}

/// The clip transform is an adjustment of its layout slot: scale about the
/// slot center, then move by (position − 0.5) canvas fractions.
NormRect adjust(const NormRect& slot, const Clip& c, Time local) {
    const auto pos = c.transform.position.evaluate(local);
    const double scale = std::clamp(c.transform.scale.evaluate(local).x, 0.1, 3.0);
    const double cx = slot.x + slot.w / 2 + (pos.x - 0.5);
    const double cy = slot.y + slot.h / 2 + (pos.y - 0.5);
    return {cx - slot.w * scale / 2, cy - slot.h * scale / 2, slot.w * scale, slot.h * scale};
}

/// Insets by `amount` of the canvas height on every side.
NormRect inset(const NormRect& r, double amount, int width, int height) {
    if (amount <= 0) return r;
    const double dx = amount * height / std::max(1, width);
    return {r.x + dx, r.y + amount, std::max(0.02, r.w - 2 * dx), std::max(0.02, r.h - 2 * amount)};
}

double easeOutCubic(double t) { return 1.0 - std::pow(1.0 - t, 3.0); }

double easeOutBack(double t) {
    constexpr double c1 = 1.70158;
    constexpr double c3 = c1 + 1.0;
    return 1.0 + c3 * std::pow(t - 1.0, 3.0) + c1 * std::pow(t - 1.0, 2.0);
}

std::size_t characterCount(const std::string& utf8) {
    std::size_t n = 0;
    for (const unsigned char ch : utf8) n += (ch & 0xC0) != 0x80 ? 1 : 0;
    return n;
}

/// Applies an entrance (`entering`) or exit animation at presence `p`
/// (0 = gone, 1 = fully on screen). Exits continue the motion's direction.
void animateText(VisualLayer& l, std::string_view kind, double p, bool entering) {
    p = std::clamp(p, 0.0, 1.0);
    const double e = easeOutCubic(p);
    const double dir = entering ? 1.0 : -1.0;
    if (kind == "fade") {
        l.opacity *= p;
    } else if (kind == "slide-up") {
        l.opacity *= p;
        l.textDy = dir * 0.06 * (1.0 - e);
    } else if (kind == "slide-down") {
        l.opacity *= p;
        l.textDy = -dir * 0.06 * (1.0 - e);
    } else if (kind == "slide-left") {
        l.opacity *= p;
        l.textDx = dir * 0.08 * (1.0 - e);
    } else if (kind == "slide-right") {
        l.opacity *= p;
        l.textDx = -dir * 0.08 * (1.0 - e);
    } else if (kind == "pop") {
        l.opacity *= std::min(1.0, p * 3.0);
        l.textScale = 0.5 + 0.5 * (entering ? easeOutBack(p) : e);
    } else if (kind == "zoom") {
        l.opacity *= p;
        l.textScale = 1.0 + 0.25 * (1.0 - e);
    } else if (kind == "typewriter") {
        l.textChars = static_cast<int>(std::floor(p * static_cast<double>(characterCount(l.text)) + 1e-9));
    } else if (kind == "wipe") {
        l.textReveal = e;
    } else if (kind == "blur") {
        l.opacity *= p;
        l.textBlur = 0.02 * (1.0 - e);
    }
}

/// Entrance and exit animations of a text clip at `local` time.
void animateText(VisualLayer& l, const timeline::TextAnimation& a, Time local, Time duration) {
    const Time half = duration.scaled(Rational(1, 2));
    const Time in = std::min(a.inDuration, half);
    const Time out = std::min(a.outDuration, half);
    if (a.in != "none" && in > Time::zero() && local < in) {
        animateText(l, a.in, local.toSecondsF() / in.toSecondsF(), true);
    } else if (a.out != "none" && out > Time::zero() && local > duration - out) {
        animateText(l, a.out, (duration - local).toSecondsF() / out.toSecondsF(), false);
    }
}

}  // namespace

const std::vector<std::pair<std::string, std::string>>& textAnimations() {
    static const std::vector<std::pair<std::string, std::string>> kinds = {
        {"none", "None"},        {"fade", "Fade"},         {"slide-up", "Slide up"},   {"slide-down", "Slide down"},
        {"slide-left", "Slide left"}, {"slide-right", "Slide right"}, {"pop", "Pop"},    {"zoom", "Zoom"},
        {"typewriter", "Typewriter"}, {"wipe", "Wipe"},     {"blur", "Blur"},
    };
    return kinds;
}

LayoutSlots customizedSlots(const project::Project& p, std::string_view preset) {
    const int W = std::max(16, p.canvas.width);
    const int H = std::max(16, p.canvas.height);
    LayoutSlots slots = layoutSlots(preset, W, H);
    const auto it = p.style.layouts.find(project::layoutKey(preset, W, H));
    if (it == p.style.layouts.end()) return slots;
    auto apply = [](std::optional<NormRect>& slot, const std::optional<project::SlotRect>& custom) {
        if (slot && custom) slot = NormRect{custom->x, custom->y, custom->w, custom->h};
    };
    apply(slots.screen, it->second.screen);
    apply(slots.camera, it->second.camera);
    // A full-bleed camera that was resized is a normal camera bubble.
    if (slots.fullBleedCamera && it->second.camera) slots.fullBleedCamera = false;
    return slots;
}

TextPresetDefaults textPresetDefaults(std::string_view preset) {
    timeline::TextStyle s;
    s.font = "";  // system UI font
    auto animation = [](const char* in, double inSeconds, const char* out, double outSeconds) {
        return timeline::TextAnimation{in, out, Time::fromSecondsF(inSeconds), Time::fromSecondsF(outSeconds)};
    };
    if (preset == "lower-third") {
        s.size = 52;
        s.weight = 600;
        s.color = "#FFFFFFFF";
        s.alignment = "left";
        s.background = "#E614161C";
        return {0.28, 0.78, s, animation("slide-right", 0.45, "fade", 0.3)};  // above the subtitle band
    }
    if (preset == "caption") {
        s.size = 46;
        s.weight = 500;
        s.color = "#FFFFFFFF";
        s.background = "#A6000000";
        return {0.5, 0.86, s, animation("fade", 0.25, "fade", 0.25)};
    }
    if (preset == "callout") {
        s.size = 54;
        s.weight = 700;
        s.color = "#FF15171C";
        s.background = "#FFF5C142";
        return {0.5, 0.16, s, animation("pop", 0.35, "fade", 0.25)};
    }
    s.size = 96;  // "title"
    s.weight = 700;
    s.color = "#FFFFFFFF";
    s.shadowBlur = 12;
    s.shadowColor = "#99000000";
    return {0.5, 0.5, s, animation("zoom", 0.5, "fade", 0.35)};
}

ColorParams gradeAt(const timeline::ColorAdjustments& c, Time local, bool withLook) {
    ColorParams p;
    p.exposure = std::clamp(c.exposure.evaluate(local), -2.0, 2.0);
    p.brightness = std::clamp(c.brightness.evaluate(local), -1.0, 1.0);
    p.contrast = std::clamp(c.contrast.evaluate(local), -1.0, 1.0);
    p.saturation = std::clamp(c.saturation.evaluate(local), -1.0, 1.0);
    p.temperature = std::clamp(c.temperature.evaluate(local), -1.0, 1.0);
    p.tint = std::clamp(c.tint.evaluate(local), -1.0, 1.0);
    p.lift = c.lift;
    p.gamma = c.gammaWheel;
    p.gain = c.gain;
    p.offset = c.offset;
    p.pivot = std::clamp(c.pivot, 0.0, 1.0);
    p.shadows = std::clamp(c.shadows.evaluate(local), -1.0, 1.0);
    p.highlights = std::clamp(c.highlights.evaluate(local), -1.0, 1.0);
    p.colorBoost = std::clamp(c.colorBoost.evaluate(local), -1.0, 1.0);
    p.hue = std::clamp(c.hue.evaluate(local), -1.0, 1.0);
    for (std::size_t i = 0; i < 4; ++i) {
        // The diagonal (or a single point) is no curve at all.
        const auto& pts = c.curves[i];
        const bool identity = pts.size() < 2 || std::all_of(pts.begin(), pts.end(), [](const timeline::Vec2& v) {
                                  return std::abs(v.x - v.y) < 1e-6;
                              });
        if (!identity) p.curves[i] = pts;
    }
    for (std::size_t i = 0; i < 6; ++i) {  // HSL curves: a flat line at 0.5 changes nothing
        const auto& pts = c.hslCurves[i];
        const bool flat = pts.size() < 2 || std::all_of(pts.begin(), pts.end(), [](const timeline::Vec2& v) {
                              return std::abs(v.y - 0.5) < 1e-6;
                          });
        if (!flat) p.hsl[i] = pts;
    }
    p.lut = c.lut;
    p.lutAmount = std::clamp(c.lutAmount, 0.0, 1.0);
    return withLook ? applyLook(p, c.look) : p;
}

RenderPlan ungraded(RenderPlan plan) {
    for (auto& l : plan.layers) {  // everything the Color page adds; placement stays
        l.color = ColorParams{};
        l.nodes.clear();
        l.highlightNode = -1;
        l.grain = 0;
        l.denoiseLuma = 0;
        l.denoiseChroma = 0;
        l.sharpen = 0;
        l.glow = 0;
        l.halation = 0;
        l.vignette = 0;
        l.blur = 0;
        l.backgroundBlur = 0;
    }
    return plan;
}

int enabledNodeIndex(const timeline::ColorAdjustments& c, std::string_view nodeId) {
    int index = 0;
    for (const auto& n : c.nodes) {
        if (!n.enabled) continue;
        if (n.id == nodeId) return index < static_cast<int>(timeline::ColorAdjustments::kMaxNodes) ? index : -1;
        ++index;
    }
    return -1;
}

std::vector<NodeParams> gradeNodes(const timeline::ColorAdjustments& c) {
    std::vector<NodeParams> out;
    for (const auto& n : c.nodes) {
        if (!n.enabled || out.size() >= timeline::ColorAdjustments::kMaxNodes) continue;
        NodeParams p;
        p.grade = paramsOf(n.grade);
        p.window = n.window;
        p.qualifier = n.qualifier;
        p.subject = n.subject == "person" ? 1 : n.subject == "background" ? 2 : 0;
        p.invert = n.invert;
        out.push_back(std::move(p));
    }
    return out;
}

RenderPlan buildRenderPlan(const project::Project& p, Time t) {
    RenderPlan plan;
    plan.width = std::max(16, p.canvas.width);
    plan.height = std::max(16, p.canvas.height);
    plan.background = p.canvas.backgroundColor;
    plan.background2 = p.style.backgroundColor2;
    plan.style = p.style;

    struct Active {
        const Clip* clip;
        const project::MediaSource* media;
    };
    std::vector<Active> video;  // main video tracks, bottom → top
    bool haveScreen = false;
    bool haveCamera = false;
    for (const auto& track : p.timeline.tracks) {
        if (track.hidden || track.kind != TrackKind::Video) continue;
        const Clip* c = track.clipAt(t);
        if (!c || !c->enabled || c->kind != ClipKind::Media) continue;
        const project::MediaSource* m = p.findMedia(c->media);
        if (!m || m->kind == project::MediaKind::Audio) continue;
        video.push_back({c, m});
        (isCamera(m->role) ? haveCamera : haveScreen) = true;
    }

    std::string preset = timeline::edit::layoutAt(p.timeline, t);
    LayoutSlots defaults = layoutSlots(preset, plan.width, plan.height);
    // A layout needing a source that is not there shows the one that is.
    if (!defaults.screen && haveScreen && !haveCamera) preset = "screen.only";
    if (!defaults.camera && haveCamera && !haveScreen) preset = "camera.only";
    const LayoutSlots layout = customizedSlots(p, preset);
    plan.layout = preset;

    for (const Active& a : video) {
        const Time local = t - a.clip->range.start;
        const bool camera = isCamera(a.media->role);
        const std::optional<NormRect>& slot = camera ? layout.camera : layout.screen;
        if (!slot) continue;
        VisualLayer l;
        l.kind = LayerKind::Media;
        l.clip = a.clip->id;
        l.role = camera ? "camera" : "screen";
        l.media = a.media->id;
        l.mediaKind = a.media->kind;
        l.sourceTime = a.clip->sourceTimeAt(t);
        l.opacity = std::clamp(a.clip->opacity.evaluate(local), 0.0, 1.0);
        l.color = gradeAt(a.clip->color, local);
        l.nodes = gradeNodes(a.clip->color);
        l.input = inputColorOf(a.clip->color, a.media);
        applyTransformKeys(l, *a.clip, local);
        if (a.media->info.video && a.media->info.video->width > 0 && a.media->info.video->height > 0) {
            l.sourceAspect = static_cast<double>(a.media->info.video->width) / a.media->info.video->height;
        }
        applyEffects(l, *a.clip, local);
        NormRect box = adjust(*slot, *a.clip, local);
        if (camera) {
            l.fill = true;
            l.mirror = p.style.cameraMirror;
            if (!layout.fullBleedCamera) {
                l.circle = layout.circleCamera || p.style.cameraShape == "circle";
                const double wPx = box.w * plan.width;
                const double hPx = box.h * plan.height;
                if (l.circle) {
                    const double side = std::min(wPx, hPx);
                    const double cx = box.x + box.w / 2;
                    const double cy = box.y + box.h / 2;
                    box = {cx - side / 2 / plan.width, cy - side / 2 / plan.height, side / plan.width, side / plan.height};
                } else if (p.style.cameraShape == "rounded") {
                    l.radius = 0.12 * std::min(wPx, hPx) / plan.height;
                }
                l.border = p.style.cameraBorder;
                l.borderColor = p.style.cameraBorderColor;
                l.shadow = 0.35;
            }
        } else {
            box = inset(box, p.style.screenPadding, plan.width, plan.height);
            l.radius = p.style.screenRadius;
            l.shadow = p.style.screenShadow;
        }
        l.box = box;
        plan.layers.push_back(std::move(l));
    }

    for (const auto& track : p.timeline.tracks) {
        if (track.hidden || track.kind != TrackKind::Overlay) continue;
        const Clip* c = track.clipAt(t);
        if (!c || !c->enabled) continue;
        const Time local = t - c->range.start;
        if (c->kind == ClipKind::Text && c->text && !c->text->text.empty()) {
            VisualLayer l;
            l.kind = LayerKind::Text;
            l.role = "text";
            l.clip = c->id;
            l.text = c->text->text;
            l.textStyle = c->text->style;
            l.textPreset = c->text->preset;
            const auto pos = c->transform.position.evaluate(local);
            l.anchorX = pos.x;
            l.anchorY = pos.y;
            l.opacity = std::clamp(c->opacity.evaluate(local), 0.0, 1.0);
            applyTransformKeys(l, *c, local);
            animateText(l, c->text->animation, local, c->range.duration);
            plan.layers.push_back(std::move(l));
        } else if (c->kind == ClipKind::Media) {
            const project::MediaSource* m = p.findMedia(c->media);
            if (!m || m->kind == project::MediaKind::Audio) continue;
            VisualLayer l;
            l.kind = LayerKind::Media;
            l.role = "overlay";
            l.clip = c->id;
            l.media = m->id;
            l.mediaKind = m->kind;
            l.sourceTime = c->sourceTimeAt(t);
            l.opacity = std::clamp(c->opacity.evaluate(local), 0.0, 1.0);
            l.color = gradeAt(c->color, local);
            l.nodes = gradeNodes(c->color);
            l.input = inputColorOf(c->color, m);
            applyEffects(l, *c, local);
            applyTransformKeys(l, *c, local);
            // Free placement: centered at `position`, `scale` of the canvas width wide.
            const auto pos = c->transform.position.evaluate(local);
            const double width = std::clamp(c->transform.scale.evaluate(local).x, 0.02, 2.0);
            double aspect = 16.0 / 9.0;
            if (m->info.video && m->info.video->width > 0 && m->info.video->height > 0) {
                aspect = static_cast<double>(m->info.video->width) / m->info.video->height;
            }
            l.sourceAspect = aspect;
            const double height = width * plan.width / aspect / plan.height;
            l.box = {pos.x - width / 2, pos.y - height / 2, width, height};
            plan.layers.push_back(std::move(l));
        }
    }

    for (const auto& track : p.timeline.tracks) {
        if (track.hidden || track.kind != TrackKind::Subtitle) continue;
        const Clip* c = track.clipAt(t);
        if (c && c->enabled && c->subtitle && !c->subtitle->text.empty()) {
            VisualLayer l;
            l.kind = LayerKind::Subtitle;
            l.role = "subtitle";
            l.clip = c->id;
            l.text = c->subtitle->text;
            l.anchorX = 0.5;
            l.anchorY = p.style.subtitlePosition;
            plan.layers.push_back(std::move(l));
            break;  // one subtitle line at a time
        }
    }
    return plan;
}

InputColor resolveInputColor(std::string_view override, std::string_view transfer, std::string_view primaries) {
    if (override == "rec709") return {};
    if (override == "srgb") return {Transfer::Srgb, Primaries::Bt709};
    if (override == "display-p3") return {Transfer::Srgb, Primaries::DisplayP3};
    if (override == "rec2020") return {Transfer::Bt709, Primaries::Bt2020};
    if (override == "rec2020-hlg") return {Transfer::Hlg, Primaries::Bt2020};
    if (override == "rec2020-pq") return {Transfer::Pq, Primaries::Bt2020};
    // "auto": the file's tags (FFmpeg names); untagged files are Rec.709.
    InputColor c;
    if (transfer == "smpte2084") c.transfer = Transfer::Pq;
    else if (transfer == "arib-std-b67") c.transfer = Transfer::Hlg;
    else if (transfer == "iec61966-2-1") c.transfer = Transfer::Srgb;
    if (primaries == "bt2020") c.primaries = Primaries::Bt2020;
    else if (primaries == "smpte432") c.primaries = Primaries::DisplayP3;
    return c;
}

}  // namespace lectern::editor
