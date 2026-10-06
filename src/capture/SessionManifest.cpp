#include "capture/SessionManifest.h"

#include "core/FileSystem.h"

namespace lectern::capture {

std::string hostClockName() {
#if defined(__APPLE__)
    return "mach_absolute_time";
#elif defined(_WIN32)
    return "qpc";
#else
    return "clock_monotonic";
#endif
}

namespace {

json::Json optTime(const std::optional<Time>& t) { return t ? json::toJson(*t) : json::Json(nullptr); }

Result<std::optional<Time>> readOptTime(const json::Json& obj, const char* key, const std::string& path) {
    const auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) return std::optional<Time>{};
    auto t = json::timeFrom(*it, path + "." + key);
    if (!t) return fail(std::move(t).error());
    return std::optional<Time>(*t);
}

json::Json trackToJson(const ManifestTrack& t) {
    json::Json j{{"id", t.id},
                 {"role", std::string(toString(t.role))},
                 {"mediaType", std::string(toString(t.mediaType))},
                 {"file", t.file},
                 {"sourceName", t.sourceName},
                 {"deviceId", t.deviceId},
                 {"codec", t.codec},
                 {"encoder", t.encoder},
                 {"hardwareEncoder", t.hardwareEncoder},
                 {"state", std::string(toString(t.state))},
                 {"start", optTime(t.start)},
                 {"end", optTime(t.end)},
                 {"bytes", t.bytes},
                 {"error", t.error}};
    if (t.mediaType == MediaType::Video) {
        j["width"] = t.width;
        j["height"] = t.height;
        j["frameRate"] = json::toJson(t.frameRate.rational());
        j["framesEncoded"] = t.framesEncoded;
        j["framesDuplicated"] = t.framesDuplicated;
        j["framesDropped"] = t.framesDropped;
    } else {
        j["sampleRate"] = t.sampleRate;
        j["channels"] = t.channels;
        j["driftPpm"] = t.driftPpm;
        j["hardCorrections"] = t.hardCorrections;
        j["latencyCompensationNs"] = t.latencyCompensationNs;
    }
    return j;
}

Result<ManifestTrack> trackFromJson(const json::Json& j, const std::string& path) {
    ManifestTrack t;
    auto id = json::require<std::string>(j, "id", path);
    if (!id) return fail(std::move(id).error());
    t.id = *id;
    auto role = json::require<std::string>(j, "role", path);
    if (!role) return fail(std::move(role).error());
    const auto r = trackRoleFromString(*role);
    if (!r) return fail(ErrorCode::ParseError, path + ".role: unknown role '" + *role + "'");
    t.role = *r;
    t.mediaType = mediaTypeOf(t.role);
    auto file = json::require<std::string>(j, "file", path);
    if (!file) return fail(std::move(file).error());
    t.file = *file;
    t.sourceName = j.value("sourceName", "");
    t.deviceId = j.value("deviceId", "");
    t.codec = j.value("codec", "");
    t.encoder = j.value("encoder", "");
    t.hardwareEncoder = j.value("hardwareEncoder", false);
    t.state = trackStateFromString(j.value("state", "pending")).value_or(TrackState::Pending);
    auto start = readOptTime(j, "start", path);
    if (!start) return fail(std::move(start).error());
    t.start = *start;
    auto end = readOptTime(j, "end", path);
    if (!end) return fail(std::move(end).error());
    t.end = *end;
    t.bytes = j.value("bytes", std::uint64_t{0});
    t.error = j.value("error", "");
    t.width = j.value("width", 0);
    t.height = j.value("height", 0);
    if (j.contains("frameRate")) {
        if (auto fr = json::rationalFrom(j["frameRate"], path + ".frameRate")) t.frameRate = FrameRate(*fr);
    }
    t.framesEncoded = j.value("framesEncoded", std::uint64_t{0});
    t.framesDuplicated = j.value("framesDuplicated", std::uint64_t{0});
    t.framesDropped = j.value("framesDropped", std::uint64_t{0});
    t.sampleRate = j.value("sampleRate", 0);
    t.channels = j.value("channels", 0);
    t.driftPpm = j.value("driftPpm", 0.0);
    t.hardCorrections = j.value("hardCorrections", std::uint64_t{0});
    t.latencyCompensationNs = j.value("latencyCompensationNs", std::int64_t{0});
    return t;
}

}  // namespace

json::Json toJson(const SessionManifest& m) {
    json::Json pauses = json::Json::array();
    for (const auto& p : m.pauses) {
        pauses.push_back({{"hostBeginNs", p.hostBeginNs},
                          {"hostEndNs", p.hostEndNs ? json::Json(*p.hostEndNs) : json::Json(nullptr)},
                          {"sessionTime", json::toJson(p.sessionTime)}});
    }
    json::Json tracks = json::Json::array();
    for (const auto& t : m.tracks) tracks.push_back(trackToJson(t));
    json::Json j{{"format", "lectern.recording-session"},
                 {"formatVersion", m.formatVersion},
                 {"timebase", Time::kTicksPerSecond},
                 {"sessionId", m.sessionId},
                 {"title", m.title},
                 {"state", std::string(toString(m.state))},
                 {"recovered", m.recovered},
                 {"stopReason", std::string(toString(m.stopReason))},
                 {"createdAt", m.createdAtUtc},
                 {"finishedAt", m.finishedAtUtc},
                 {"appVersion", m.appVersion},
                 {"hostClock", m.hostClock},
                 {"startHostNs", m.startHostNs},
                 {"stopHostNs", m.stopHostNs ? json::Json(*m.stopHostNs) : json::Json(nullptr)},
                 {"duration", json::toJson(m.duration)},
                 {"checkpointHostNs", m.checkpointHostNs},
                 {"pauses", std::move(pauses)},
                 {"tracks", std::move(tracks)}};
    if (!m.inputEventsFile.empty()) {
        j["inputEvents"] = m.inputEventsFile;
        j["pointerEventCount"] = m.pointerEventCount;
        j["keyEventCount"] = m.keyEventCount;
    }
    return j;
}

Result<SessionManifest> manifestFromJson(const json::Json& j) {
    const std::string root = "session";
    if (!j.is_object()) return fail(ErrorCode::ParseError, "session: expected object");
    SessionManifest m;
    m.formatVersion = j.value("formatVersion", 0);
    if (m.formatVersion < 1 || m.formatVersion > SessionManifest::kFormatVersion) {
        return fail(ErrorCode::Unsupported, "session manifest version " + std::to_string(m.formatVersion) +
                                                " is not supported");
    }
    auto sid = json::require<std::string>(j, "sessionId", root);
    if (!sid) return fail(std::move(sid).error());
    m.sessionId = *sid;
    m.title = j.value("title", "");
    m.state = sessionStateFromString(j.value("state", "idle")).value_or(SessionState::Idle);
    m.recovered = j.value("recovered", false);
    const std::string reason = j.value("stopReason", "none");
    for (auto r : {StopReason::None, StopReason::User, StopReason::DiskFull, StopReason::AllTracksFailed,
                   StopReason::Error, StopReason::Cancelled}) {
        if (toString(r) == reason) m.stopReason = r;
    }
    m.createdAtUtc = j.value("createdAt", "");
    m.finishedAtUtc = j.value("finishedAt", "");
    m.appVersion = j.value("appVersion", "");
    m.hostClock = j.value("hostClock", "");
    m.startHostNs = j.value("startHostNs", std::int64_t{0});
    if (j.contains("stopHostNs") && j["stopHostNs"].is_number_integer()) m.stopHostNs = j["stopHostNs"].get<std::int64_t>();
    if (j.contains("duration")) {
        auto d = json::timeFrom(j["duration"], root + ".duration");
        if (!d) return fail(std::move(d).error());
        m.duration = *d;
    }
    m.checkpointHostNs = j.value("checkpointHostNs", std::int64_t{0});
    if (const auto it = j.find("pauses"); it != j.end() && it->is_array()) {
        for (const auto& p : *it) {
            ManifestPause mp;
            mp.hostBeginNs = p.value("hostBeginNs", std::int64_t{0});
            if (p.contains("hostEndNs") && p["hostEndNs"].is_number_integer()) mp.hostEndNs = p["hostEndNs"].get<std::int64_t>();
            if (p.contains("sessionTime")) {
                if (auto t = json::timeFrom(p["sessionTime"], root + ".pauses")) mp.sessionTime = *t;
            }
            m.pauses.push_back(mp);
        }
    }
    if (const auto it = j.find("tracks"); it != j.end() && it->is_array()) {
        std::size_t i = 0;
        for (const auto& t : *it) {
            auto track = trackFromJson(t, root + ".tracks[" + std::to_string(i++) + "]");
            if (!track) return fail(std::move(track).error());
            m.tracks.push_back(std::move(*track));
        }
    }
    m.inputEventsFile = j.value("inputEvents", "");
    m.pointerEventCount = j.value("pointerEventCount", std::uint64_t{0});
    m.keyEventCount = j.value("keyEventCount", std::uint64_t{0});
    return m;
}

Status writeManifest(const std::filesystem::path& path, const SessionManifest& manifest) {
    return fs::writeFileAtomic(path, json::dump(toJson(manifest)), {.fsync = true, .keepBackup = false});
}

Result<SessionManifest> readManifest(const std::filesystem::path& path) {
    auto text = fs::readFile(path, 16u << 20);
    if (!text) return fail(std::move(text).error());
    auto parsed = json::parse(*text, path.string());
    if (!parsed) return fail(std::move(parsed).error());
    return manifestFromJson(*parsed);
}

json::Json toJson(const InputEventLog& log) {
    json::Json pointer = json::Json::array();
    for (const ManifestPointerEvent& e : log.pointer) {
        pointer.push_back({{"t", json::toJson(e.sessionTime)},
                           {"type", e.type},
                           {"x", e.x},
                           {"y", e.y},
                           {"button", e.button}});
    }
    json::Json keys = json::Json::array();
    for (const ManifestKeyEvent& e : log.keys) {
        keys.push_back({{"t", json::toJson(e.sessionTime)},
                        {"type", e.type},
                        {"key", e.key},
                        {"modifiers", e.modifiers}});
    }
    return json::Json{{"format", "lectern.input-events"},
                      {"formatVersion", log.formatVersion},
                      {"timebase", Time::kTicksPerSecond},
                      {"captureWidth", log.captureWidth},
                      {"captureHeight", log.captureHeight},
                      {"pointer", std::move(pointer)},
                      {"keys", std::move(keys)}};
}

Result<InputEventLog> inputEventLogFromJson(const json::Json& j) {
    InputEventLog log;
    log.formatVersion = j.value("formatVersion", 0);
    if (log.formatVersion < 1 || log.formatVersion > InputEventLog::kFormatVersion) {
        return fail(ErrorCode::Unsupported, "input-events version not supported");
    }
    log.captureWidth = j.value("captureWidth", 0);
    log.captureHeight = j.value("captureHeight", 0);
    if (const auto it = j.find("pointer"); it != j.end() && it->is_array()) {
        for (const auto& p : *it) {
            ManifestPointerEvent e;
            if (p.contains("t")) {
                if (auto t = json::timeFrom(p["t"], "pointer.t")) e.sessionTime = *t;
            }
            e.type = p.value("type", "move");
            e.x = p.value("x", 0.0);
            e.y = p.value("y", 0.0);
            e.button = p.value("button", 0);
            log.pointer.push_back(e);
        }
    }
    if (const auto it = j.find("keys"); it != j.end() && it->is_array()) {
        for (const auto& k : *it) {
            ManifestKeyEvent e;
            if (k.contains("t")) {
                if (auto t = json::timeFrom(k["t"], "keys.t")) e.sessionTime = *t;
            }
            e.type = k.value("type", "down");
            e.key = k.value("key", "");
            e.modifiers = k.value("modifiers", std::uint32_t{0});
            log.keys.push_back(e);
        }
    }
    return log;
}

Status writeInputEventLog(const std::filesystem::path& path, const InputEventLog& log) {
    return fs::writeFileAtomic(path, json::dump(toJson(log)), {.fsync = true, .keepBackup = false});
}

Result<InputEventLog> readInputEventLog(const std::filesystem::path& path) {
    auto text = fs::readFile(path, 64u << 20);
    if (!text) return fail(std::move(text).error());
    auto parsed = json::parse(*text, path.string());
    if (!parsed) return fail(std::move(parsed).error());
    return inputEventLogFromJson(*parsed);
}

}  // namespace lectern::capture
