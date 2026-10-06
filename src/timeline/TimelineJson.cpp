#include "timeline/TimelineJson.h"

#include <algorithm>
#include <utility>

namespace lectern::timeline {

namespace {

using json::Json;

// ---- enums ------------------------------------------------------------------

template <class E, std::size_t N>
std::optional<E> enumFrom(std::string_view s, const E (&values)[N]) {
    for (E v : values) {
        if (toString(v) == s) return v;
    }
    return std::nullopt;
}

constexpr TrackKind kTrackKinds[] = {TrackKind::Video, TrackKind::Audio, TrackKind::Overlay, TrackKind::Subtitle};
constexpr ClipKind kClipKinds[] = {ClipKind::Media, ClipKind::Text, ClipKind::Subtitle, ClipKind::Color};
constexpr Interpolation kInterps[] = {Interpolation::Hold, Interpolation::Linear, Interpolation::EaseIn,
                                      Interpolation::EaseOut, Interpolation::EaseInOut, Interpolation::Bezier};
constexpr FitMode kFits[] = {FitMode::Fit, FitMode::Fill, FitMode::Stretch, FitMode::None};
constexpr MarkerKind kMarkerKinds[] = {MarkerKind::User, MarkerKind::Pause, MarkerKind::Chapter};

// ---- values -----------------------------------------------------------------

Json valueJson(double v) { return v; }
Json valueJson(const Vec2& v) { return Json::array({v.x, v.y}); }
Json valueJson(const Vec4& v) { return Json::array({v.x, v.y, v.z, v.w}); }

Result<double> valueFrom(const Json& j, const std::string& path, double*) {
    if (!j.is_number()) return fail(ErrorCode::ParseError, path + ": expected number");
    return j.get<double>();
}
Result<Vec2> valueFrom(const Json& j, const std::string& path, Vec2*) {
    if (!j.is_array() || j.size() != 2 || !j[0].is_number() || !j[1].is_number()) {
        return fail(ErrorCode::ParseError, path + ": expected [x, y]");
    }
    return Vec2{j[0].get<double>(), j[1].get<double>()};
}
Result<Vec4> valueFrom(const Json& j, const std::string& path, Vec4*) {
    if (!j.is_array() || j.size() != 4) return fail(ErrorCode::ParseError, path + ": expected [a, b, c, d]");
    for (const auto& e : j) {
        if (!e.is_number()) return fail(ErrorCode::ParseError, path + ": expected numbers");
    }
    return Vec4{j[0].get<double>(), j[1].get<double>(), j[2].get<double>(), j[3].get<double>()};
}

template <class T>
Json animatedJson(const Animated<T>& a) {
    Json j{{"value", valueJson(a.value)}};
    if (!a.keys.empty()) {
        Json keys = Json::array();
        for (const auto& k : a.keys) {
            Json kj{{"t", json::toJson(k.time)}, {"v", valueJson(k.value)}, {"interp", std::string(toString(k.interp))}};
            if (k.interp == Interpolation::Bezier) {
                kj["out"] = Json::array({k.handles.x1, k.handles.y1});
                kj["in"] = Json::array({k.handles.x2, k.handles.y2});
            }
            keys.push_back(std::move(kj));
        }
        j["keys"] = std::move(keys);
    }
    return j;
}

template <class T>
Result<Animated<T>> animatedFrom(const Json& j, const std::string& path) {
    Animated<T> a;
    if (!j.is_object() || !j.contains("value")) return fail(ErrorCode::ParseError, path + ": expected {value, keys?}");
    auto v = valueFrom(j["value"], path + ".value", static_cast<T*>(nullptr));
    if (!v) return fail(std::move(v).error());
    a.value = *v;
    if (const auto it = j.find("keys"); it != j.end()) {
        if (!it->is_array()) return fail(ErrorCode::ParseError, path + ".keys: expected array");
        std::size_t i = 0;
        for (const auto& kj : *it) {
            const std::string kp = path + ".keys[" + std::to_string(i++) + "]";
            if (!kj.is_object() || !kj.contains("t") || !kj.contains("v")) {
                return fail(ErrorCode::ParseError, kp + ": expected {t, v, interp}");
            }
            Keyframe<T> k;
            auto t = json::timeFrom(kj["t"], kp + ".t");
            if (!t) return fail(std::move(t).error());
            k.time = *t;
            auto kv = valueFrom(kj["v"], kp + ".v", static_cast<T*>(nullptr));
            if (!kv) return fail(std::move(kv).error());
            k.value = *kv;
            const std::string interp = kj.value("interp", "linear");
            const auto ip = enumFrom(interp, kInterps);
            if (!ip) return fail(ErrorCode::ParseError, kp + ".interp: unknown '" + interp + "'");
            k.interp = *ip;
            if (k.interp == Interpolation::Bezier) {
                const auto out = kj.value("out", Json::array({0.42, 0.0}));
                const auto in = kj.value("in", Json::array({0.58, 1.0}));
                if (out.size() == 2 && in.size() == 2) {
                    k.handles = {out[0].get<double>(), out[1].get<double>(), in[0].get<double>(), in[1].get<double>()};
                }
            }
            if (!a.keys.empty() && k.time <= a.keys.back().time) {
                return fail(ErrorCode::ParseError, kp + ".t: keys must be strictly increasing");
            }
            a.keys.push_back(k);
        }
    }
    return a;
}

#define LEC_READ_ANIM(type, field, jsonObj, key, pathStr)                                     \
    if (const auto it_ = (jsonObj).find(key); it_ != (jsonObj).end()) {                       \
        auto r_ = animatedFrom<type>(*it_, (pathStr) + "." + (key));                          \
        if (!r_) return fail(std::move(r_).error());                                          \
        (field) = std::move(*r_);                                                             \
    }

Json transformJson(const Transform& t) {
    return Json{{"fit", std::string(toString(t.fit))},
                {"position", animatedJson(t.position)},
                {"scale", animatedJson(t.scale)},
                {"rotation", animatedJson(t.rotation)},
                {"anchor", animatedJson(t.anchor)},
                {"flipH", t.flipH},
                {"flipV", t.flipV}};
}

Result<Transform> transformFrom(const Json& j, const std::string& path) {
    Transform t;
    if (!j.is_object()) return fail(ErrorCode::ParseError, path + ": expected object");
    const std::string fit = j.value("fit", "fit");
    const auto f = enumFrom(fit, kFits);
    if (!f) return fail(ErrorCode::ParseError, path + ".fit: unknown '" + fit + "'");
    t.fit = *f;
    LEC_READ_ANIM(Vec2, t.position, j, "position", path)
    LEC_READ_ANIM(Vec2, t.scale, j, "scale", path)
    LEC_READ_ANIM(double, t.rotation, j, "rotation", path)
    if (const auto it = j.find("anchor"); it != j.end()) {
        if (it->is_object() && (it->contains("value") || it->contains("keys"))) {
            auto a = animatedFrom<Vec2>(*it, path + ".anchor");
            if (!a) return fail(std::move(a).error());
            t.anchor = std::move(*a);
        } else {
            auto a = valueFrom(*it, path + ".anchor", static_cast<Vec2*>(nullptr));
            if (!a) return fail(std::move(a).error());
            t.anchor.value = *a;
        }
    }
    t.flipH = j.value("flipH", false);
    t.flipV = j.value("flipV", false);
    return t;
}

Json wheelJson(const ColorAdjustments::Wheel& w) { return Json::array({w.x, w.y, w.master}); }

ColorAdjustments::Wheel wheelFrom(const Json& j) {
    ColorAdjustments::Wheel w;
    if (!j.is_array() || j.size() != 3 || !j[0].is_number() || !j[1].is_number() || !j[2].is_number()) return w;
    w.x = std::clamp(j[0].get<double>(), -1.0, 1.0);
    w.y = std::clamp(j[1].get<double>(), -1.0, 1.0);
    w.master = std::clamp(j[2].get<double>(), -1.0, 1.0);
    return w;
}

constexpr const char* kCurveKeys[4] = {"curveY", "curveR", "curveG", "curveB"};

/// Custom curves as "curveY": [[in, out], …] (absent = no curve).
void writeCurves(Json& j, const std::array<std::vector<Vec2>, 4>& curves) {
    for (std::size_t i = 0; i < 4; ++i) {
        if (curves[i].empty()) continue;
        Json pts = Json::array();
        for (const Vec2& p : curves[i]) pts.push_back(Json::array({p.x, p.y}));
        j[kCurveKeys[i]] = pts;
    }
}

void readCurves(const Json& j, std::array<std::vector<Vec2>, 4>& curves) {
    for (std::size_t i = 0; i < 4; ++i) {
        const auto it = j.find(kCurveKeys[i]);
        if (it == j.end() || !it->is_array()) continue;
        for (const Json& p : *it) {
            if (p.is_array() && p.size() == 2 && p[0].is_number() && p[1].is_number())
                curves[i].push_back({std::clamp(p[0].get<double>(), 0.0, 1.0), std::clamp(p[1].get<double>(), 0.0, 1.0)});
        }
        std::sort(curves[i].begin(), curves[i].end(), [](const Vec2& a, const Vec2& b) { return a.x < b.x; });
    }
}

std::string textOr(const Json& j, const char* key, std::string fallback = {}) {
    const auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : fallback;
}

double numberOr(const Json& j, const char* key, double fallback, double lo, double hi) {
    const auto it = j.find(key);
    return it != j.end() && it->is_number() ? std::clamp(it->get<double>(), lo, hi) : fallback;
}

bool boolOr(const Json& j, const char* key, bool fallback) {
    const auto it = j.find(key);
    return it != j.end() && it->is_boolean() ? it->get<bool>() : fallback;
}

/// Grade fields (looks and nodes); neutral values are left out.
void writeGrade(Json& j, const ColorAdjustments::Grade& g) {
    const auto put = [&j](const char* key, double v, double neutral) {
        if (v != neutral) j[key] = v;
    };
    put("exposure", g.exposure, 0.0);
    put("brightness", g.brightness, 0.0);
    put("contrast", g.contrast, 0.0);
    put("pivot", g.pivot, 0.5);
    put("shadows", g.shadows, 0.0);
    put("highlights", g.highlights, 0.0);
    put("saturation", g.saturation, 0.0);
    put("colorBoost", g.colorBoost, 0.0);
    put("hue", g.hue, 0.0);
    put("temperature", g.temperature, 0.0);
    put("tint", g.tint, 0.0);
    if (!g.lift.isIdentity()) j["lift"] = wheelJson(g.lift);
    if (!g.gammaWheel.isIdentity()) j["gammaWheel"] = wheelJson(g.gammaWheel);
    if (!g.gain.isIdentity()) j["gain"] = wheelJson(g.gain);
    if (!g.offset.isIdentity()) j["offset"] = wheelJson(g.offset);
    writeCurves(j, g.curves);
}

/// Lenient: unknown or out-of-range values fall back to neutral.
ColorAdjustments::Grade readGrade(const Json& j) {
    ColorAdjustments::Grade g;
    if (!j.is_object()) return g;
    g.exposure = numberOr(j, "exposure", 0.0, -2.0, 2.0);
    g.brightness = numberOr(j, "brightness", 0.0, -1.0, 1.0);
    g.contrast = numberOr(j, "contrast", 0.0, -1.0, 1.0);
    g.pivot = numberOr(j, "pivot", 0.5, 0.0, 1.0);
    g.shadows = numberOr(j, "shadows", 0.0, -1.0, 1.0);
    g.highlights = numberOr(j, "highlights", 0.0, -1.0, 1.0);
    g.saturation = numberOr(j, "saturation", 0.0, -1.0, 1.0);
    g.colorBoost = numberOr(j, "colorBoost", 0.0, -1.0, 1.0);
    g.hue = numberOr(j, "hue", 0.0, -1.0, 1.0);
    g.temperature = numberOr(j, "temperature", 0.0, -1.0, 1.0);
    g.tint = numberOr(j, "tint", 0.0, -1.0, 1.0);
    if (const auto it = j.find("lift"); it != j.end()) g.lift = wheelFrom(*it);
    if (const auto it = j.find("gammaWheel"); it != j.end()) g.gammaWheel = wheelFrom(*it);
    if (const auto it = j.find("gain"); it != j.end()) g.gain = wheelFrom(*it);
    if (const auto it = j.find("offset"); it != j.end()) g.offset = wheelFrom(*it);
    readCurves(j, g.curves);
    return g;
}

Json windowJson(const ColorAdjustments::Window& w) {
    return Json{{"shape", w.shape},       {"x", w.x},
                {"y", w.y},               {"width", w.width},
                {"height", w.height},     {"rotation", w.rotation},
                {"softness", w.softness}, {"invert", w.invert}};
}

ColorAdjustments::Window windowFrom(const Json& j) {
    ColorAdjustments::Window w;
    if (!j.is_object()) return w;
    const std::string shape = textOr(j, "shape");
    w.shape = shape == "circle" || shape == "rectangle" || shape == "gradient" ? shape : std::string();
    w.x = numberOr(j, "x", 0.5, -1.0, 2.0);
    w.y = numberOr(j, "y", 0.5, -1.0, 2.0);
    w.width = numberOr(j, "width", 0.5, 0.002, 4.0);
    w.height = numberOr(j, "height", 0.5, 0.002, 4.0);
    w.rotation = numberOr(j, "rotation", 0.0, -360.0, 360.0);
    w.softness = numberOr(j, "softness", 0.2, 0.0, 1.0);
    w.invert = boolOr(j, "invert", false);
    return w;
}

Json qualifierJson(const ColorAdjustments::Qualifier& q) {
    return Json{{"enabled", q.enabled}, {"hue", q.hue},         {"hueWidth", q.hueWidth}, {"hueSoft", q.hueSoft},
                {"satLow", q.satLow},   {"satHigh", q.satHigh}, {"satSoft", q.satSoft},   {"lumLow", q.lumLow},
                {"lumHigh", q.lumHigh}, {"lumSoft", q.lumSoft}, {"invert", q.invert}};
}

ColorAdjustments::Qualifier qualifierFrom(const Json& j) {
    ColorAdjustments::Qualifier q;
    if (!j.is_object()) return q;
    q.enabled = boolOr(j, "enabled", false);
    q.hue = numberOr(j, "hue", 0.0, 0.0, 1.0);
    q.hueWidth = numberOr(j, "hueWidth", 0.08, 0.0, 0.5);
    q.hueSoft = numberOr(j, "hueSoft", 0.04, 0.0, 0.5);
    q.satLow = numberOr(j, "satLow", 0.15, 0.0, 1.0);
    q.satHigh = numberOr(j, "satHigh", 1.0, 0.0, 1.0);
    q.satSoft = numberOr(j, "satSoft", 0.05, 0.0, 0.5);
    q.lumLow = numberOr(j, "lumLow", 0.0, 0.0, 1.0);
    q.lumHigh = numberOr(j, "lumHigh", 1.0, 0.0, 1.0);
    q.lumSoft = numberOr(j, "lumSoft", 0.05, 0.0, 0.5);
    q.invert = boolOr(j, "invert", false);
    return q;
}

Json nodeJson(const ColorAdjustments::Node& n) {
    Json grade = Json::object();
    writeGrade(grade, n.grade);
    Json j{{"id", n.id}, {"label", n.label}, {"enabled", n.enabled}, {"grade", grade}};
    if (!n.window.isNone()) j["window"] = windowJson(n.window);
    if (n.qualifier.enabled) j["qualifier"] = qualifierJson(n.qualifier);
    if (!n.subject.empty()) j["subject"] = n.subject;
    if (n.invert) j["invert"] = true;
    return j;
}

ColorAdjustments::Node nodeFrom(const Json& j) {
    ColorAdjustments::Node n;
    if (!j.is_object()) return n;
    n.id = textOr(j, "id");
    n.label = textOr(j, "label");
    n.enabled = boolOr(j, "enabled", true);
    if (const auto it = j.find("grade"); it != j.end()) n.grade = readGrade(*it);
    if (const auto it = j.find("window"); it != j.end()) n.window = windowFrom(*it);
    if (const auto it = j.find("qualifier"); it != j.end()) n.qualifier = qualifierFrom(*it);
    const std::string subject = textOr(j, "subject");
    n.subject = subject == "person" || subject == "background" ? subject : std::string();
    n.invert = boolOr(j, "invert", false);
    return n;
}

Json colorJson(const ColorAdjustments& c) {
    Json j{{"exposure", animatedJson(c.exposure)},     {"brightness", animatedJson(c.brightness)},
           {"contrast", animatedJson(c.contrast)},     {"highlights", animatedJson(c.highlights)},
           {"shadows", animatedJson(c.shadows)},       {"saturation", animatedJson(c.saturation)},
           {"temperature", animatedJson(c.temperature)}, {"tint", animatedJson(c.tint)},
           {"gamma", animatedJson(c.gamma)},           {"sharpness", animatedJson(c.sharpness)}};
    // Optional (absent in older files): wheels and LUT.
    if (!c.lift.isIdentity()) j["lift"] = wheelJson(c.lift);
    if (!c.gammaWheel.isIdentity()) j["gammaWheel"] = wheelJson(c.gammaWheel);
    if (!c.gain.isIdentity()) j["gain"] = wheelJson(c.gain);
    if (!c.offset.isIdentity()) j["offset"] = wheelJson(c.offset);
    if (c.pivot != 0.5) j["pivot"] = c.pivot;
    if (c.colorBoost.value != 0.0 || !c.colorBoost.keys.empty()) j["colorBoost"] = animatedJson(c.colorBoost);
    if (c.hue.value != 0.0 || !c.hue.keys.empty()) j["hue"] = animatedJson(c.hue);
    writeCurves(j, c.curves);
    if (!c.lut.empty()) {
        j["lut"] = c.lut;
        j["lutAmount"] = c.lutAmount;
    }
    if (c.inputColorSpace != "auto") j["inputColorSpace"] = c.inputColorSpace;
    if (!c.look.isNone()) j["look"] = toJson(c.look);
    if (!c.nodes.empty()) {
        Json nodes = Json::array();
        for (const auto& n : c.nodes) nodes.push_back(nodeJson(n));
        j["nodes"] = nodes;
    }
    return j;
}

Result<ColorAdjustments> colorFrom(const Json& j, const std::string& path) {
    ColorAdjustments c;
    if (!j.is_object()) return fail(ErrorCode::ParseError, path + ": expected object");
    LEC_READ_ANIM(double, c.exposure, j, "exposure", path)
    LEC_READ_ANIM(double, c.brightness, j, "brightness", path)
    LEC_READ_ANIM(double, c.contrast, j, "contrast", path)
    LEC_READ_ANIM(double, c.highlights, j, "highlights", path)
    LEC_READ_ANIM(double, c.shadows, j, "shadows", path)
    LEC_READ_ANIM(double, c.saturation, j, "saturation", path)
    LEC_READ_ANIM(double, c.temperature, j, "temperature", path)
    LEC_READ_ANIM(double, c.tint, j, "tint", path)
    LEC_READ_ANIM(double, c.gamma, j, "gamma", path)
    LEC_READ_ANIM(double, c.sharpness, j, "sharpness", path)
    if (const auto it = j.find("lift"); it != j.end()) c.lift = wheelFrom(*it);
    if (const auto it = j.find("gammaWheel"); it != j.end()) c.gammaWheel = wheelFrom(*it);
    if (const auto it = j.find("gain"); it != j.end()) c.gain = wheelFrom(*it);
    if (const auto it = j.find("offset"); it != j.end()) c.offset = wheelFrom(*it);
    c.pivot = std::clamp(j.value("pivot", 0.5), 0.0, 1.0);
    if (j.contains("colorBoost")) { LEC_READ_ANIM(double, c.colorBoost, j, "colorBoost", path) }
    if (j.contains("hue")) { LEC_READ_ANIM(double, c.hue, j, "hue", path) }
    readCurves(j, c.curves);
    c.lut = j.value("lut", "");
    c.lutAmount = std::clamp(j.value("lutAmount", 1.0), 0.0, 1.0);
    c.inputColorSpace = j.value("inputColorSpace", "auto");
    if (const auto it = j.find("look"); it != j.end()) c.look = lookFromJson(*it);
    if (const auto it = j.find("nodes"); it != j.end() && it->is_array()) {
        for (const Json& n : *it) {
            if (c.nodes.size() >= ColorAdjustments::kMaxNodes) break;
            ColorAdjustments::Node node = nodeFrom(n);
            if (node.id.empty()) node.id = "n" + std::to_string(c.nodes.size() + 1);
            c.nodes.push_back(std::move(node));
        }
    }
    return c;
}

Json textStyleJson(const TextStyle& s) {
    return Json{{"font", s.font},
                {"size", s.size},
                {"weight", s.weight},
                {"color", s.color},
                {"alignment", s.alignment},
                {"lineSpacing", s.lineSpacing},
                {"letterSpacing", s.letterSpacing},
                {"background", s.background},
                {"borderWidth", s.borderWidth},
                {"borderColor", s.borderColor},
                {"shadowBlur", s.shadowBlur},
                {"shadowColor", s.shadowColor}};
}

TextStyle textStyleFrom(const Json& j) {
    TextStyle s;
    s.font = j.value("font", s.font);
    s.size = j.value("size", s.size);
    s.weight = j.value("weight", s.weight);
    s.color = j.value("color", s.color);
    s.alignment = j.value("alignment", s.alignment);
    s.lineSpacing = j.value("lineSpacing", s.lineSpacing);
    s.letterSpacing = j.value("letterSpacing", s.letterSpacing);
    s.background = j.value("background", s.background);
    s.borderWidth = j.value("borderWidth", s.borderWidth);
    s.borderColor = j.value("borderColor", s.borderColor);
    s.shadowBlur = j.value("shadowBlur", s.shadowBlur);
    s.shadowColor = j.value("shadowColor", s.shadowColor);
    return s;
}

template <class IdT>
Result<IdT> idFrom(const Json& obj, const char* key, const std::string& path) {
    const auto it = obj.find(key);
    if (it == obj.end()) return fail(ErrorCode::ParseError, path + "." + key + ": missing");
    auto u = json::uuidFrom(*it, path + "." + key);
    if (!u) return fail(std::move(u).error());
    return IdT(*u);
}

}  // namespace

json::Json toJson(const Clip& c) {
    Json j{{"id", c.id.toString()},
           {"kind", std::string(toString(c.kind))},
           {"name", c.name},
           {"enabled", c.enabled},
           {"start", json::toJson(c.range.start)},
           {"duration", json::toJson(c.range.duration)}};
    if (c.linkGroup) j["linkGroup"] = c.linkGroup->toString();
    if (c.kind == ClipKind::Media) {
        j["mediaId"] = c.media.toString();
        j["sourceIn"] = json::toJson(c.sourceIn);
        j["speed"] = json::toJson(c.speed);
    }
    j["transform"] = transformJson(c.transform);
    j["crop"] = animatedJson(c.crop);
    j["opacity"] = animatedJson(c.opacity);
    j["audio"] = Json{{"gainDb", c.audio.gainDb},
                      {"volume", animatedJson(c.audio.volume)},
                      {"fadeIn", json::toJson(c.audio.fadeIn)},
                      {"fadeOut", json::toJson(c.audio.fadeOut)},
                      {"muted", c.audio.muted}};
    j["color"] = colorJson(c.color);
    Json effects = Json::array();
    for (const auto& e : c.effects) {
        Json params = Json::object();
        for (const auto& [name, value] : e.params) params[name] = animatedJson(value);
        effects.push_back(Json{{"id", e.id.toString()},
                               {"type", e.type},
                               {"version", e.version},
                               {"enabled", e.enabled},
                               {"params", std::move(params)}});
    }
    j["effects"] = std::move(effects);
    if (c.text) {
        const TextAnimation& a = c.text->animation;
        j["text"] = Json{{"text", c.text->text},
                         {"preset", c.text->preset},
                         {"style", textStyleJson(c.text->style)},
                         {"animation", Json{{"in", a.in},
                                            {"out", a.out},
                                            {"inDuration", json::toJson(a.inDuration)},
                                            {"outDuration", json::toJson(a.outDuration)}}}};
    }
    if (c.subtitle) j["subtitle"] = Json{{"text", c.subtitle->text}, {"styleId", c.subtitle->styleId}};
    if (c.kind == ClipKind::Color) j["fillColor"] = c.fillColor;
    return j;
}

Result<Clip> clipFromJson(const Json& j, const std::string& path) {
    if (!j.is_object()) return fail(ErrorCode::ParseError, path + ": expected object");
    Clip c;
    auto id = idFrom<ClipId>(j, "id", path);
    if (!id) return fail(std::move(id).error());
    c.id = *id;
    const std::string kind = j.value("kind", "media");
    const auto k = enumFrom(kind, kClipKinds);
    if (!k) return fail(ErrorCode::ParseError, path + ".kind: unknown '" + kind + "'");
    c.kind = *k;
    c.name = j.value("name", "");
    c.enabled = j.value("enabled", true);
    auto start = json::timeFrom(j.value("start", Json()), path + ".start");
    if (!start) return fail(std::move(start).error());
    auto dur = json::timeFrom(j.value("duration", Json()), path + ".duration");
    if (!dur) return fail(std::move(dur).error());
    c.range = {*start, *dur};
    if (j.contains("linkGroup")) {
        auto lg = idFrom<LinkGroupId>(j, "linkGroup", path);
        if (!lg) return fail(std::move(lg).error());
        c.linkGroup = *lg;
    }
    if (c.kind == ClipKind::Media) {
        auto media = idFrom<MediaId>(j, "mediaId", path);
        if (!media) return fail(std::move(media).error());
        c.media = *media;
        auto in = json::timeFrom(j.value("sourceIn", Json(0)), path + ".sourceIn");
        if (!in) return fail(std::move(in).error());
        c.sourceIn = *in;
        if (j.contains("speed")) {
            auto sp = json::rationalFrom(j["speed"], path + ".speed");
            if (!sp) return fail(std::move(sp).error());
            c.speed = *sp;
        }
    }
    if (j.contains("transform")) {
        auto t = transformFrom(j["transform"], path + ".transform");
        if (!t) return fail(std::move(t).error());
        c.transform = std::move(*t);
    }
    LEC_READ_ANIM(Vec4, c.crop, j, "crop", path)
    LEC_READ_ANIM(double, c.opacity, j, "opacity", path)
    if (const auto it = j.find("audio"); it != j.end() && it->is_object()) {
        const Json& a = *it;
        c.audio.gainDb = a.value("gainDb", 0.0);
        LEC_READ_ANIM(double, c.audio.volume, a, "volume", path + ".audio")
        if (a.contains("fadeIn")) {
            if (auto t = json::timeFrom(a["fadeIn"], path + ".audio.fadeIn")) c.audio.fadeIn = *t;
        }
        if (a.contains("fadeOut")) {
            if (auto t = json::timeFrom(a["fadeOut"], path + ".audio.fadeOut")) c.audio.fadeOut = *t;
        }
        c.audio.muted = a.value("muted", false);
    }
    if (j.contains("color")) {
        auto col = colorFrom(j["color"], path + ".color");
        if (!col) return fail(std::move(col).error());
        c.color = std::move(*col);
    }
    if (const auto it = j.find("effects"); it != j.end() && it->is_array()) {
        std::size_t i = 0;
        for (const auto& ej : *it) {
            const std::string ep = path + ".effects[" + std::to_string(i++) + "]";
            EffectInstance e;
            auto eid = idFrom<EffectId>(ej, "id", ep);
            if (!eid) return fail(std::move(eid).error());
            e.id = *eid;
            e.type = ej.value("type", "");
            e.version = ej.value("version", 1);
            e.enabled = ej.value("enabled", true);
            if (const auto pit = ej.find("params"); pit != ej.end() && pit->is_object()) {
                for (const auto& [name, pv] : pit->items()) {
                    auto a = animatedFrom<double>(pv, ep + ".params." + name);
                    if (!a) return fail(std::move(a).error());
                    e.params[name] = std::move(*a);
                }
            }
            c.effects.push_back(std::move(e));
        }
    }
    if (const auto it = j.find("text"); it != j.end() && it->is_object()) {
        c.text = TextContent{it->value("text", ""), it->value("preset", ""),
                             textStyleFrom(it->value("style", Json::object())), TextAnimation{}};
        if (const auto ait = it->find("animation"); ait != it->end() && ait->is_object()) {
            TextAnimation& a = c.text->animation;
            a.in = ait->value("in", a.in);
            a.out = ait->value("out", a.out);
            for (auto [key, field] : {std::pair{"inDuration", &a.inDuration}, std::pair{"outDuration", &a.outDuration}}) {
                if (!ait->contains(key)) continue;
                auto t = json::timeFrom((*ait)[key], path + ".text.animation." + key);
                if (!t) return fail(std::move(t).error());
                *field = std::clamp(*t, Time::zero(), Time::fromSeconds(10));
            }
        }
    }
    if (const auto it = j.find("subtitle"); it != j.end() && it->is_object()) {
        c.subtitle = SubtitleContent{it->value("text", ""), it->value("styleId", "")};
    }
    c.fillColor = j.value("fillColor", "");
    return c;
}

json::Json toJson(const Timeline& tl) {
    Json tracks = Json::array();
    for (const Track& t : tl.tracks) {
        Json clips = Json::array();
        for (const Clip& c : t.clips) clips.push_back(toJson(c));
        tracks.push_back(Json{{"id", t.id.toString()},
                              {"kind", std::string(toString(t.kind))},
                              {"name", t.name},
                              {"locked", t.locked},
                              {"hidden", t.hidden},
                              {"muted", t.muted},
                              {"solo", t.solo},
                              {"gainDb", t.gainDb},
                              {"clips", std::move(clips)}});
    }
    Json markers = Json::array();
    for (const Marker& m : tl.markers) {
        markers.push_back(Json{{"id", m.id.toString()},
                               {"time", json::toJson(m.time)},
                               {"label", m.label},
                               {"kind", std::string(toString(m.kind))},
                               {"color", m.color}});
    }
    Json layout = Json::array();
    for (const LayoutRegion& r : tl.layout) {
        layout.push_back(Json{{"id", r.id.toString()},
                              {"start", json::toJson(r.range.start)},
                              {"duration", json::toJson(r.range.duration)},
                              {"preset", r.preset}});
    }
    return Json{{"tracks", std::move(tracks)}, {"markers", std::move(markers)}, {"layout", std::move(layout)}};
}

Result<Timeline> timelineFromJson(const Json& j, const std::string& path) {
    if (!j.is_object()) return fail(ErrorCode::ParseError, path + ": expected object");
    Timeline tl;
    if (const auto it = j.find("tracks"); it != j.end()) {
        if (!it->is_array()) return fail(ErrorCode::ParseError, path + ".tracks: expected array");
        std::size_t ti = 0;
        for (const auto& tj : *it) {
            const std::string tp = path + ".tracks[" + std::to_string(ti++) + "]";
            Track t;
            auto id = idFrom<TrackId>(tj, "id", tp);
            if (!id) return fail(std::move(id).error());
            t.id = *id;
            const std::string kind = tj.value("kind", "video");
            const auto k = enumFrom(kind, kTrackKinds);
            if (!k) return fail(ErrorCode::ParseError, tp + ".kind: unknown '" + kind + "'");
            t.kind = *k;
            t.name = tj.value("name", "");
            t.locked = tj.value("locked", false);
            t.hidden = tj.value("hidden", false);
            t.muted = tj.value("muted", false);
            t.solo = tj.value("solo", false);
            t.gainDb = tj.value("gainDb", 0.0);
            if (const auto cit = tj.find("clips"); cit != tj.end() && cit->is_array()) {
                std::size_t ci = 0;
                for (const auto& cj : *cit) {
                    auto clip = clipFromJson(cj, tp + ".clips[" + std::to_string(ci++) + "]");
                    if (!clip) return fail(std::move(clip).error());
                    t.clips.push_back(std::move(*clip));
                }
            }
            tl.tracks.push_back(std::move(t));
        }
    }
    if (const auto it = j.find("markers"); it != j.end() && it->is_array()) {
        std::size_t i = 0;
        for (const auto& mj : *it) {
            const std::string mp = path + ".markers[" + std::to_string(i++) + "]";
            Marker m;
            auto id = idFrom<MarkerId>(mj, "id", mp);
            if (!id) return fail(std::move(id).error());
            m.id = *id;
            auto t = json::timeFrom(mj.value("time", Json()), mp + ".time");
            if (!t) return fail(std::move(t).error());
            m.time = *t;
            m.label = mj.value("label", "");
            m.color = mj.value("color", m.color);
            m.kind = enumFrom(mj.value("kind", std::string("user")), kMarkerKinds).value_or(MarkerKind::User);
            tl.markers.push_back(std::move(m));
        }
    }
    if (const auto it = j.find("layout"); it != j.end() && it->is_array()) {
        std::size_t i = 0;
        for (const auto& rj : *it) {
            const std::string rp = path + ".layout[" + std::to_string(i++) + "]";
            LayoutRegion r;
            auto id = idFrom<LayoutRegionId>(rj, "id", rp);
            if (!id) return fail(std::move(id).error());
            r.id = *id;
            auto s = json::timeFrom(rj.value("start", Json()), rp + ".start");
            auto d = json::timeFrom(rj.value("duration", Json()), rp + ".duration");
            if (!s) return fail(std::move(s).error());
            if (!d) return fail(std::move(d).error());
            r.range = {*s, *d};
            r.preset = rj.value("preset", "");
            tl.layout.push_back(std::move(r));
        }
    }
    return tl;
}

json::Json toJson(const ColorAdjustments& c) { return colorJson(c); }

Result<ColorAdjustments> colorAdjustmentsFromJson(const Json& j, const std::string& path) { return colorFrom(j, path); }

json::Json toJson(const ColorAdjustments::Look& l) {
    Json j{{"id", l.id}, {"name", l.name}, {"amount", l.amount}};
    writeGrade(j, l);
    return j;
}

ColorAdjustments::Look lookFromJson(const Json& j) {
    ColorAdjustments::Look l;
    if (!j.is_object()) return l;
    static_cast<ColorAdjustments::Grade&>(l) = readGrade(j);
    l.id = textOr(j, "id");
    l.name = textOr(j, "name");
    l.amount = numberOr(j, "amount", 1.0, 0.0, 1.0);
    return l;
}

}  // namespace lectern::timeline
