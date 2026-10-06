#include "project/ProjectJson.h"

#include "core/Log.h"
#include "timeline/TimelineJson.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace lectern::project {

namespace {

using json::Json;

constexpr MediaKind kMediaKinds[] = {MediaKind::Video, MediaKind::Audio, MediaKind::Image};
constexpr MediaRole kMediaRoles[] = {MediaRole::Screen, MediaRole::Camera, MediaRole::Phone,
                                     MediaRole::Microphone, MediaRole::SystemAudio, MediaRole::Imported};

template <class E, std::size_t N>
std::optional<E> enumFrom(std::string_view s, const E (&values)[N]) {
    for (E v : values) {
        if (toString(v) == s) return v;
    }
    return std::nullopt;
}

Json mediaJson(const MediaSource& m) {
    Json info{{"container", m.info.container},
              {"start", json::toJson(m.info.start)},
              {"duration", json::toJson(m.info.duration)},
              {"video", nullptr},
              {"audio", nullptr}};
    if (m.info.video) {
        const auto& v = *m.info.video;
        info["video"] = Json{{"codec", v.codec},
                             {"width", v.width},
                             {"height", v.height},
                             {"frameRate", json::toJson(v.frameRate.rational())},
                             {"pixelFormat", v.pixelFormat},
                             {"colorSpace", v.colorSpace},
                             {"colorRange", v.colorRange},
                             {"rotation", v.rotation},
                             {"vfr", v.variableFrameRate}};
        if (!v.colorPrimaries.empty()) info["video"]["colorPrimaries"] = v.colorPrimaries;
        if (!v.colorTransfer.empty()) info["video"]["colorTransfer"] = v.colorTransfer;
    }
    if (m.info.audio) {
        info["audio"] = Json{{"codec", m.info.audio->codec},
                             {"sampleRate", m.info.audio->sampleRate},
                             {"channels", m.info.audio->channels}};
    }
    Json j{{"id", m.id.toString()},
           {"kind", std::string(toString(m.kind))},
           {"role", std::string(toString(m.role))},
           {"name", m.name},
           {"path", m.path},
           {"fingerprint", Json{{"size", m.fingerprint.size}, {"partialHash", m.fingerprint.partialHash}}},
           {"info", std::move(info)},
           {"recording", nullptr},
           {"proxy", m.proxyPath ? Json(*m.proxyPath) : Json(nullptr)},
           {"syncOffset", json::toJson(m.syncOffset)}};
    if (m.recording) j["recording"] = Json{{"sessionId", m.recording->sessionId}, {"trackId", m.recording->trackId}};
    return j;
}

Result<MediaSource> mediaFrom(const Json& j, const std::string& path) {
    if (!j.is_object()) return fail(ErrorCode::ParseError, path + ": expected object");
    MediaSource m;
    auto id = json::uuidFrom(j.value("id", Json()), path + ".id");
    if (!id) return fail(std::move(id).error());
    m.id = MediaId(*id);
    const auto kind = enumFrom(j.value("kind", std::string("video")), kMediaKinds);
    if (!kind) return fail(ErrorCode::ParseError, path + ".kind: unknown");
    m.kind = *kind;
    m.role = enumFrom(j.value("role", std::string("imported")), kMediaRoles).value_or(MediaRole::Imported);
    m.name = j.value("name", "");
    auto p = json::require<std::string>(j, "path", path);
    if (!p) return fail(std::move(p).error());
    m.path = *p;
    if (const auto it = j.find("fingerprint"); it != j.end() && it->is_object()) {
        m.fingerprint.size = it->value("size", std::uint64_t{0});
        m.fingerprint.partialHash = it->value("partialHash", "");
    }
    if (const auto it = j.find("info"); it != j.end() && it->is_object()) {
        const Json& ij = *it;
        m.info.container = ij.value("container", "");
        if (auto s = json::timeFrom(ij.value("start", Json(0)), path + ".info.start")) m.info.start = *s;
        if (auto d = json::timeFrom(ij.value("duration", Json(0)), path + ".info.duration")) m.info.duration = *d;
        if (const auto vit = ij.find("video"); vit != ij.end() && vit->is_object()) {
            VideoMetadata v;
            v.codec = vit->value("codec", "");
            v.width = vit->value("width", 0);
            v.height = vit->value("height", 0);
            if (vit->contains("frameRate")) {
                if (auto fr = json::rationalFrom((*vit)["frameRate"], path + ".info.video.frameRate")) {
                    v.frameRate = FrameRate(*fr);
                }
            }
            v.pixelFormat = vit->value("pixelFormat", "");
            v.colorSpace = vit->value("colorSpace", "");
            v.colorRange = vit->value("colorRange", "");
            v.rotation = vit->value("rotation", 0);
            v.variableFrameRate = vit->value("vfr", false);
            v.colorPrimaries = vit->value("colorPrimaries", "");
            v.colorTransfer = vit->value("colorTransfer", "");
            m.info.video = v;
        }
        if (const auto ait = ij.find("audio"); ait != ij.end() && ait->is_object()) {
            m.info.audio = AudioMetadata{ait->value("codec", ""), ait->value("sampleRate", 0), ait->value("channels", 0)};
        }
    }
    if (const auto it = j.find("recording"); it != j.end() && it->is_object()) {
        m.recording = RecordingRef{it->value("sessionId", ""), it->value("trackId", "")};
    }
    if (const auto it = j.find("proxy"); it != j.end() && it->is_string()) m.proxyPath = it->get<std::string>();
    if (j.contains("syncOffset")) {
        auto so = json::timeFrom(j["syncOffset"], path + ".syncOffset");
        if (!so) return fail(std::move(so).error());
        m.syncOffset = *so;
    }
    return m;
}

}  // namespace

namespace {

Json slotJson(const SlotRect& r) { return Json::array({r.x, r.y, r.w, r.h}); }

std::optional<SlotRect> slotFrom(const Json& j) {
    if (!j.is_array() || j.size() != 4) return std::nullopt;
    for (const auto& v : j) {
        if (!v.is_number()) return std::nullopt;
    }
    SlotRect r{j[0].get<double>(), j[1].get<double>(), j[2].get<double>(), j[3].get<double>()};
    if (!std::isfinite(r.x) || !std::isfinite(r.y) || !(r.w > 0.0) || !(r.h > 0.0)) return std::nullopt;
    r.x = std::clamp(r.x, -5.0, 5.0);
    r.y = std::clamp(r.y, -5.0, 5.0);
    r.w = std::clamp(r.w, 0.02, 8.0);
    r.h = std::clamp(r.h, 0.02, 8.0);
    return r;
}

Json styleJson(const StyleSettings& s) {
    Json layouts = Json::object();
    for (const auto& [key, custom] : s.layouts) {
        Json c = Json::object();
        if (custom.screen) c["screen"] = slotJson(*custom.screen);
        if (custom.camera) c["camera"] = slotJson(*custom.camera);
        if (!c.empty()) layouts[key] = std::move(c);
    }
    return Json{{"layouts", std::move(layouts)},
                {"backgroundColor2", s.backgroundColor2},
                {"screenPadding", s.screenPadding},
                {"screenRadius", s.screenRadius},
                {"screenShadow", s.screenShadow},
                {"cameraShape", s.cameraShape},
                {"cameraBorder", s.cameraBorder},
                {"cameraBorderColor", s.cameraBorderColor},
                {"cameraMirror", s.cameraMirror},
                {"subtitleSize", s.subtitleSize},
                {"subtitleColor", s.subtitleColor},
                {"subtitleBackground", s.subtitleBackground},
                {"subtitlePosition", s.subtitlePosition}};
}

StyleSettings styleFrom(const Json& j) {
    StyleSettings s;
    if (!j.is_object()) return s;
    s.backgroundColor2 = j.value("backgroundColor2", s.backgroundColor2);
    s.screenPadding = std::clamp(j.value("screenPadding", s.screenPadding), 0.0, 0.25);
    s.screenRadius = std::clamp(j.value("screenRadius", s.screenRadius), 0.0, 0.08);
    s.screenShadow = std::clamp(j.value("screenShadow", s.screenShadow), 0.0, 1.0);
    s.cameraShape = j.value("cameraShape", s.cameraShape);
    s.cameraBorder = std::clamp(j.value("cameraBorder", s.cameraBorder), 0.0, 0.02);
    s.cameraBorderColor = j.value("cameraBorderColor", s.cameraBorderColor);
    s.cameraMirror = j.value("cameraMirror", s.cameraMirror);
    s.subtitleSize = std::clamp(j.value("subtitleSize", s.subtitleSize), 0.02, 0.1);
    s.subtitleColor = j.value("subtitleColor", s.subtitleColor);
    s.subtitleBackground = j.value("subtitleBackground", s.subtitleBackground);
    s.subtitlePosition = std::clamp(j.value("subtitlePosition", s.subtitlePosition), 0.1, 0.95);
    if (const auto it = j.find("layouts"); it != j.end() && it->is_object()) {
        for (const auto& [key, value] : it->items()) {
            if (!value.is_object()) continue;
            LayoutCustomization c;
            if (const auto sc = value.find("screen"); sc != value.end()) c.screen = slotFrom(*sc);
            if (const auto cam = value.find("camera"); cam != value.end()) c.camera = slotFrom(*cam);
            if (!c.empty()) s.layouts[key] = c;
        }
    }
    return s;
}

/// v1 → v2: the importer used to store an absolute picture-in-picture
/// placement on camera clips (position 0.84/0.80, scale 0.28). From v2 the
/// layout preset places the camera and the transform is a relative
/// adjustment, so that legacy default becomes the identity.
void migrateV1ToV2(Json& value) {
    std::set<std::string> cameraMedia;
    if (const auto it = value.find("media"); it != value.end() && it->is_array()) {
        for (const auto& m : *it) {
            const std::string role = m.value("role", "");
            if (role == "camera" || role == "phone") cameraMedia.insert(m.value("id", ""));
        }
    }
    auto near = [](const Json& v, double x, double y) {
        return v.is_array() && v.size() == 2 && v[0].is_number() && v[1].is_number() &&
               std::abs(v[0].get<double>() - x) < 1e-6 && std::abs(v[1].get<double>() - y) < 1e-6;
    };
    auto tl = value.find("timeline");
    if (tl == value.end() || !tl->is_object()) return;
    auto tracks = tl->find("tracks");
    if (tracks == tl->end() || !tracks->is_array()) return;
    for (auto& track : *tracks) {
        auto clips = track.find("clips");
        if (clips == track.end() || !clips->is_array()) continue;
        for (auto& clip : *clips) {
            if (!cameraMedia.contains(clip.value("mediaId", ""))) continue;
            auto transform = clip.find("transform");
            if (transform == clip.end() || !transform->is_object()) continue;
            auto pos = transform->find("position");
            auto scale = transform->find("scale");
            if (pos == transform->end() || scale == transform->end() || !pos->is_object() || !scale->is_object()) continue;
            if (pos->contains("keys") || scale->contains("keys")) continue;
            if (near((*pos)["value"], 0.84, 0.80) && near((*scale)["value"], 0.28, 0.28)) {
                (*pos)["value"] = Json::array({0.5, 0.5});
                (*scale)["value"] = Json::array({1.0, 1.0});
            }
        }
    }
}

}  // namespace

Json toJson(const Project& p) {
    Json media = Json::array();
    for (const auto& m : p.media) media.push_back(mediaJson(m));
    Json recordings = Json::array();
    for (const auto& r : p.recordings) {
        Json entry{{"sessionId", r.sessionId},
                   {"manifest", r.manifest},
                   {"startedAt", r.startedAt},
                   {"duration", json::toJson(r.duration)},
                   {"state", r.state}};
        if (!r.inputEvents.empty()) entry["inputEvents"] = r.inputEvents;
        recordings.push_back(std::move(entry));
    }
    Json gallery = Json::array();
    for (const auto& st : p.gallery) {
        gallery.push_back(Json{{"id", st.id}, {"label", st.label}, {"image", st.image}, {"clipName", st.clipName},
                               {"time", st.time}, {"grade", timeline::toJson(st.grade)}});
    }
    const auto& e = p.exportSettings;
    return Json{{"format", "lectern.project"},
                {"formatVersion", Project::kFormatVersion},
                {"timebase", Time::kTicksPerSecond},
                {"id", p.id.toString()},
                {"title", p.title},
                {"description", p.description},
                {"createdAt", p.createdAt},
                {"modifiedAt", p.modifiedAt},
                {"app", Json{{"name", LECTERN_PRODUCT_NAME}, {"version", LECTERN_VERSION}}},
                {"canvas", Json{{"width", p.canvas.width},
                                {"height", p.canvas.height},
                                {"frameRate", json::toJson(p.canvas.frameRate.rational())},
                                {"aspect", p.canvas.aspect},
                                {"background", Json{{"type", p.canvas.backgroundType},
                                                    {"color", p.canvas.backgroundColor}}}}},
                {"style", styleJson(p.style)},
                {"media", std::move(media)},
                {"timeline", timeline::toJson(p.timeline)},
                {"recordings", std::move(recordings)},
                {"gallery", std::move(gallery)},
                {"export", Json{{"container", e.container},
                                {"videoCodec", e.videoCodec},
                                {"width", e.width},
                                {"height", e.height},
                                {"frameRate", e.frameRate ? json::toJson(e.frameRate->rational()) : Json(nullptr)},
                                {"quality", e.quality},
                                {"audioCodec", e.audioCodec},
                                {"audioBitrate", e.audioBitrate}}}};
}

Result<json::Json> migrateProjectJson(json::Json value) {
    if (!value.is_object()) return fail(ErrorCode::ParseError, "project: expected object");
    if (value.value("format", "") != "lectern.project") {
        return fail(ErrorCode::Unsupported, "not a Lectern project file");
    }
    const int version = value.value("formatVersion", 0);
    if (version > Project::kFormatVersion) {
        return fail(ErrorCode::Unsupported, "This project was created by a newer version of " +
                                                std::string(LECTERN_PRODUCT_NAME) + " (format " +
                                                std::to_string(version) + ")");
    }
    if (version < 1) return fail(ErrorCode::Corrupt, "project: invalid formatVersion");
    if (version < 2) migrateV1ToV2(value);
    value["formatVersion"] = Project::kFormatVersion;
    if (value.value("timebase", Time::kTicksPerSecond) != Time::kTicksPerSecond) {
        return fail(ErrorCode::Unsupported, "project: unsupported timebase");
    }
    return value;
}

Result<Project> projectFromJson(const json::Json& raw) {
    auto migrated = migrateProjectJson(raw);
    if (!migrated) return fail(std::move(migrated).error());
    const Json& j = *migrated;
    Project p;
    auto id = json::uuidFrom(j.value("id", Json()), "project.id");
    if (!id) return fail(std::move(id).error());
    p.id = ProjectId(*id);
    p.title = j.value("title", "");
    p.description = j.value("description", "");
    p.createdAt = j.value("createdAt", "");
    p.modifiedAt = j.value("modifiedAt", "");
    if (const auto it = j.find("canvas"); it != j.end() && it->is_object()) {
        p.canvas.width = it->value("width", p.canvas.width);
        p.canvas.height = it->value("height", p.canvas.height);
        if (it->contains("frameRate")) {
            auto fr = json::rationalFrom((*it)["frameRate"], "project.canvas.frameRate");
            if (!fr) return fail(std::move(fr).error());
            p.canvas.frameRate = FrameRate(*fr);
        }
        p.canvas.aspect = it->value("aspect", p.canvas.aspect);
        if (const auto bg = it->find("background"); bg != it->end() && bg->is_object()) {
            p.canvas.backgroundType = bg->value("type", p.canvas.backgroundType);
            p.canvas.backgroundColor = bg->value("color", p.canvas.backgroundColor);
        }
    }
    if (const auto it = j.find("style"); it != j.end()) p.style = styleFrom(*it);
    if (const auto it = j.find("media"); it != j.end() && it->is_array()) {
        std::size_t i = 0;
        for (const auto& mj : *it) {
            auto m = mediaFrom(mj, "project.media[" + std::to_string(i++) + "]");
            if (!m) return fail(std::move(m).error());
            p.media.push_back(std::move(*m));
        }
    }
    if (const auto it = j.find("timeline"); it != j.end()) {
        auto tl = timeline::timelineFromJson(*it, "project.timeline");
        if (!tl) return fail(std::move(tl).error());
        p.timeline = std::move(*tl);
    }
    if (const auto it = j.find("recordings"); it != j.end() && it->is_array()) {
        for (const auto& rj : *it) {
            RecordingEntry r;
            r.sessionId = rj.value("sessionId", "");
            r.manifest = rj.value("manifest", "");
            r.inputEvents = rj.value("inputEvents", "");
            r.startedAt = rj.value("startedAt", "");
            if (auto d = json::timeFrom(rj.value("duration", Json(0)), "project.recordings.duration")) r.duration = *d;
            r.state = rj.value("state", "");
            p.recordings.push_back(std::move(r));
        }
    }
    if (const auto it = j.find("gallery"); it != j.end() && it->is_array()) {
        for (const auto& sj : *it) {
            if (!sj.is_object()) continue;
            Still st;
            st.id = sj.value("id", "");
            st.label = sj.value("label", "");
            st.image = sj.value("image", "");
            st.clipName = sj.value("clipName", "");
            st.time = sj.value("time", 0.0);
            if (const auto g = sj.find("grade"); g != sj.end()) {
                auto grade = timeline::colorAdjustmentsFromJson(*g, "project.gallery.grade");
                if (!grade) continue;  // a damaged still is dropped, the project still opens
                st.grade = std::move(*grade);
            }
            if (!st.id.empty()) p.gallery.push_back(std::move(st));
        }
    }
    if (const auto it = j.find("export"); it != j.end() && it->is_object()) {
        auto& e = p.exportSettings;
        e.container = it->value("container", e.container);
        e.videoCodec = it->value("videoCodec", e.videoCodec);
        e.width = it->value("width", e.width);
        e.height = it->value("height", e.height);
        if (const auto fr = it->find("frameRate"); fr != it->end() && fr->is_object()) {
            if (auto r = json::rationalFrom(*fr, "project.export.frameRate")) e.frameRate = FrameRate(*r);
        }
        e.quality = it->value("quality", e.quality);
        e.audioCodec = it->value("audioCodec", e.audioCodec);
        e.audioBitrate = it->value("audioBitrate", e.audioBitrate);
    }
    LEC_TRY(p.validate());
    return p;
}

}  // namespace lectern::project
